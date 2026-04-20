/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "def.h"
#include "netsim.h"
#include "netsim_platform.h"
#include "netsim_sip.h"

#if NETSIM_PLATFORM_SUPPORTED

#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "digger_log.h"

#define NETSIM_QUEUE_LEN 32
#define NETSIM_FRAME_WINDOW 32
#define NETSIM_RETRY_LIMIT 100
#define NETSIM_RETRY_MS 10
#define NETSIM_RTP_VERSION 2U
#define NETSIM_RTP_PT 96U
#define NETSIM_RTP_VPXCC ((uint8_t)(NETSIM_RTP_VERSION << 6))
#define NETSIM_RTP_VPXCC_FLAGS_MASK 0x3fU
#define NETSIM_RTP_MPT_PT_MASK 0x7fU
#define NETSIM_RTP_SSRC_FALLBACK 0x4e53494dU
#define NETSIM_RX_BUFSIZE 4096
#define NETSIM_IDLE_WAKE_MS 100
enum netsim_pkt_type {
  NETSIM_PKT_HELLO = 1,
  NETSIM_PKT_FRAME = 2,
  NETSIM_PKT_EXIT = 3,
  NETSIM_PKT_START = 4,
  NETSIM_PKT_START_ACK = 5
};

enum netsim_out_type {
  NETSIM_OUT_FRAME = 101,
  NETSIM_OUT_EXIT = 102,
  NETSIM_OUT_STOP = 103,
  NETSIM_OUT_START = 104,
  NETSIM_OUT_START_ACK = 105,
  NETSIM_OUT_SHUTDOWN = 106,
  NETSIM_OUT_RX_PACKET = 107,
  NETSIM_OUT_RX_ERROR = 108
};

enum netsim_in_type {
  NETSIM_IN_START = 201,
  NETSIM_IN_FRAME = 202,
  NETSIM_IN_EXIT = 203,
  NETSIM_IN_ERROR = 204,
  NETSIM_IN_GAMESTART = 205,
  NETSIM_IN_START_ACK = 206
};

struct netsim_pkt {
  uint8_t type;
  uint8_t player;
  uint32_t tx_seq;
  uint32_t frame;
  uint32_t bits;
  uint64_t nonce;
  uint32_t rtp_ssrc;
  uint32_t stream_ssrc;
};

struct netsim_rtp_header {
  uint8_t vpxcc;
  uint8_t mpt;
  uint16_t seq;
  uint32_t timestamp;
  uint32_t ssrc;
};

/* App subtype and high tx_seq bits stay in payload; RTP carries observability. */
struct netsim_rtp_payload {
  uint8_t subtype;
  uint8_t player;
  uint16_t tx_seq_hi;
  uint32_t bits;
  uint64_t nonce;
  uint32_t stream_ssrc;
  uint32_t reserved;
};

struct netsim_out_ev {
  int type;
  uint32_t frame;
  uint8_t bits;
  uint64_t nonce;
  int err;
  size_t pkt_len;
  netsim_sockaddr_t peer_addr;
  uint8_t *pkt_buf;
};

struct netsim_in_ev {
  int type;
  uint32_t frame;
  uint8_t bits;
  int remote_lead_ms;
  int player;
  uint64_t nonce;
};

struct netsim_out_queue {
  netsim_mutex_t mutex;
  netsim_cond_t cond;
  struct netsim_out_ev items[NETSIM_QUEUE_LEN];
  size_t head;
  size_t tail;
  size_t len;
};

struct netsim_in_queue {
  netsim_mutex_t mutex;
  netsim_cond_t cond;
  struct netsim_in_ev items[NETSIM_QUEUE_LEN];
  size_t head;
  size_t tail;
  size_t len;
};

struct remote_frame_slot {
  bool valid;
  uint32_t frame;
  uint8_t bits;
  uint64_t first_recv_ns;
};

struct peer_frame_seen_slot {
  bool valid;
  uint32_t frame;
  int count;
};

struct pending_tx {
  bool active;
  bool matched;
  int type;
  uint32_t seq;
  uint32_t frame;
  uint8_t bits;
  uint64_t nonce;
  int retries;
  int send_count;
  uint64_t first_send_ns;
  int peer_frame_base;
  int peer_frame_count;
  netsim_deadline_t next_tx;
};

struct netsim_config {
  bool configured;
  struct netsim_sip_config sip;
};

enum netsim_session_state {
  NETSIM_SESSION_WAITING = 0,
  NETSIM_SESSION_ASSIGNED,
  NETSIM_SESSION_REMOTE_START,
  NETSIM_SESSION_READY_TO_ACK,
  NETSIM_SESSION_START_ACK,
  NETSIM_SESSION_STARTED,
  NETSIM_SESSION_ERROR,
  NETSIM_SESSION_PEER_EXITED
};

struct netsim_session {
  bool running;
  enum netsim_session_state state;
  int local_player;
  uint64_t session_nonce;
  netsim_thread_t thr;
  struct netsim_out_queue outq;
  struct netsim_in_queue inq;
};

struct thread_ctx {
  struct netsim_config cfg;
  netsim_socket_t sock;
  netsim_sockaddr_t local_addr;
  char local_host[64];
  char local_port[16];
  atomic_bool stop_requested;
  netsim_thread_t rx_thr;
  bool rx_started;
  struct netsim_sip *sip;
  struct netsim_out_queue *outq;
  struct netsim_in_queue *inq;
};

static struct netsim_config g_cfg = {.configured = false};
static struct netsim_session g_session = {.running = false,
  .state = NETSIM_SESSION_WAITING, .local_player = 0};
static bool g_debug_ready = false, g_debug_enabled = false;
static atomic_uint_fast64_t g_nonce_seq = 1;
#if defined(DIGGER_DEBUG)
static bool g_proto_debug_ready = false, g_proto_debug_enabled = false;
#endif

static void netsim_log(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
static void netsim_err(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
static void netsim_out_ev_cleanup(struct netsim_out_ev *evp);
static void netsim_reset_state(enum netsim_session_state state);
static bool netsim_is_started(void);
static bool netsim_is_error(void);
static bool netsim_is_peer_exited(void);
static bool netsim_is_ready_to_ack(void);
static bool netsim_has_start_ack(void);
static bool netsim_has_remote_start(void);
static void queue_out_clear_session(struct netsim_out_queue *qp);
static void queue_in_clear_session(struct netsim_in_queue *qp);
static int ns_delta_to_ms(int64_t delta_ns);

static void
netsim_debug_init(void)
{

  if (g_debug_ready)
    return;
  g_debug_enabled = getenv("DIGGER_NETSIM_DEBUG") != NULL;
  g_debug_ready = true;
}

#if defined(DIGGER_DEBUG)
static void
netsim_proto_debug_init(void)
{

  if (g_proto_debug_ready)
    return;
  g_proto_debug_enabled = getenv("DIGGER_NETSIM_PROTO_DEBUG") != NULL;
  g_proto_debug_ready = true;
}

static void
netsim_proto_log(const char *fmt, ...)
{
  va_list ap;
  char buf[1024];

  netsim_proto_debug_init();
  if (!g_proto_debug_enabled)
    return;
  va_start(ap, fmt);
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif
  vsnprintf(buf, sizeof(buf), fmt, ap);
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
  va_end(ap);
  digger_log_printf("netsim: %s\n", buf);
}
#endif

static void
netsim_reset_state(enum netsim_session_state state)
{

  g_session.state = state;
  g_session.local_player = 0;
  g_session.session_nonce = 0;
}

static bool
netsim_is_started(void)
{

  return (g_session.state == NETSIM_SESSION_STARTED);
}

static bool
netsim_is_error(void)
{

  return (g_session.state == NETSIM_SESSION_ERROR);
}

static bool
netsim_is_peer_exited(void)
{

  return (g_session.state == NETSIM_SESSION_PEER_EXITED);
}

static bool
netsim_is_ready_to_ack(void)
{

  return (g_session.state == NETSIM_SESSION_READY_TO_ACK);
}

static bool
netsim_has_start_ack(void)
{

  return (g_session.state == NETSIM_SESSION_START_ACK);
}

static bool
netsim_has_remote_start(void)
{

  return (g_session.state == NETSIM_SESSION_REMOTE_START ||
    g_session.state == NETSIM_SESSION_READY_TO_ACK ||
    g_session.state == NETSIM_SESSION_START_ACK ||
    g_session.state == NETSIM_SESSION_STARTED);
}

static void
netsim_log(const char *fmt, ...)
{
  va_list ap;
  char buf[1024];

  netsim_debug_init();
  if (!g_debug_enabled)
    return;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  digger_log_printf("netsim: %s\n", buf);
}

static void
netsim_err(const char *fmt, ...)
{
  va_list ap;
  char buf[1024];

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  digger_log_printf("netsim: %s\n", buf);
}

static bool
parse_sip_host_port(const char *spec, char *host, size_t host_len, char *port,
  size_t port_len)
{
  const char *portp;
  size_t len;

  portp = strrchr(spec, ':');
  if (portp == NULL || strchr(portp + 1, ':') != NULL) {
    if (strlen(spec) >= host_len)
      return (false);
    strcpy(host, spec);
    strcpy(port, "5060");
    return (host[0] != '\0');
  }
  if (portp == spec || portp[1] == '\0')
    return (false);
  len = (size_t)(portp - spec);
  if (len >= host_len || strlen(portp + 1) >= port_len)
    return (false);
  memcpy(host, spec, len);
  host[len] = '\0';
  strcpy(port, portp + 1);
  return (true);
}

static const char *
pending_name(int type)
{

  if (type == NETSIM_PKT_HELLO)
    return ("hello");
  if (type == NETSIM_OUT_FRAME)
    return ("frame");
  if (type == NETSIM_OUT_EXIT)
    return ("exit");
  if (type == NETSIM_OUT_STOP)
    return ("stop");
  if (type == NETSIM_OUT_START)
    return ("start");
  if (type == NETSIM_OUT_START_ACK)
    return ("start-ack");
  return ("unknown");
}

static bool
pending_log_retries(int type)
{

  return (type != NETSIM_PKT_HELLO && type != NETSIM_OUT_START &&
    type != NETSIM_OUT_START_ACK);
}

static uint64_t
htonll(uint64_t v)
{
  uint32_t hi, lo;

  hi = htonl((uint32_t)(v >> 32));
  lo = htonl((uint32_t)(v & 0xffffffffU));
  return (((uint64_t)lo) << 32) | hi;
}

static uint64_t
ntohll(uint64_t v)
{
  uint32_t hi, lo;

  hi = ntohl((uint32_t)(v >> 32));
  lo = ntohl((uint32_t)(v & 0xffffffffU));
  return (((uint64_t)lo) << 32) | hi;
}

static uint32_t
make_ssrc(uint64_t nonce)
{
  uint32_t ssrc;

  ssrc = (uint32_t)(nonce ^ (nonce >> 32));
  if (ssrc == 0)
    ssrc = NETSIM_RTP_SSRC_FALLBACK;
  return (ssrc);
}

static uint32_t
make_stream_ssrc(uint64_t local_nonce, uint64_t session_nonce)
{

  return (make_ssrc(local_nonce ^ session_nonce ^ 0x5354524dU));
}

static void
queue_out_init(struct netsim_out_queue *qp)
{
  netsim_mutex_init(&qp->mutex);
  netsim_cond_init(&qp->cond);
  qp->head = qp->tail = qp->len = 0;
}

static void
queue_in_init(struct netsim_in_queue *qp)
{
  netsim_mutex_init(&qp->mutex);
  netsim_cond_init(&qp->cond);
  qp->head = qp->tail = qp->len = 0;
}

static void
queue_out_destroy(struct netsim_out_queue *qp)
{
  size_t i;

  netsim_mutex_lock(&qp->mutex);
  for (i = 0; i < qp->len; i++)
    netsim_out_ev_cleanup(&qp->items[(qp->head + i) % NETSIM_QUEUE_LEN]);
  qp->head = qp->tail = qp->len = 0;
  netsim_mutex_unlock(&qp->mutex);
  netsim_cond_destroy(&qp->cond);
  netsim_mutex_destroy(&qp->mutex);
}

static void
queue_in_destroy(struct netsim_in_queue *qp)
{
  netsim_cond_destroy(&qp->cond);
  netsim_mutex_destroy(&qp->mutex);
}

static void
queue_out_put(struct netsim_out_queue *qp, const struct netsim_out_ev *evp)
{
  netsim_mutex_lock(&qp->mutex);
  while (qp->len == NETSIM_QUEUE_LEN)
    netsim_cond_wait(&qp->cond, &qp->mutex);
  qp->items[qp->tail] = *evp;
  qp->tail = (qp->tail + 1) % NETSIM_QUEUE_LEN;
  qp->len++;
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
}

static void
queue_out_wake(struct netsim_out_queue *qp)
{

  netsim_mutex_lock(&qp->mutex);
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
}

static bool
queue_out_tryget(struct netsim_out_queue *qp, struct netsim_out_ev *evp)
{
  bool ok = false;

  netsim_mutex_lock(&qp->mutex);
  if (qp->len > 0) {
    *evp = qp->items[qp->head];
    qp->head = (qp->head + 1) % NETSIM_QUEUE_LEN;
    qp->len--;
    netsim_cond_broadcast(&qp->cond);
    ok = true;
  }
  netsim_mutex_unlock(&qp->mutex);
  return (ok);
}

static void
queue_out_clear_session(struct netsim_out_queue *qp)
{
  struct netsim_out_ev tmp[NETSIM_QUEUE_LEN];
  size_t i, out_len;

  netsim_mutex_lock(&qp->mutex);
  out_len = 0;
  for (i = 0; i < qp->len; i++) {
    struct netsim_out_ev cur;

    cur = qp->items[(qp->head + i) % NETSIM_QUEUE_LEN];
    if (cur.type == NETSIM_OUT_SHUTDOWN) {
      tmp[out_len++] = cur;
      continue;
    }
    netsim_out_ev_cleanup(&cur);
  }
  if (out_len > 0)
    memcpy(qp->items, tmp, out_len * sizeof(tmp[0]));
  qp->head = 0;
  qp->len = out_len;
  qp->tail = out_len % NETSIM_QUEUE_LEN;
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
}

static void
queue_in_put(struct netsim_in_queue *qp, const struct netsim_in_ev *evp)
{
  netsim_mutex_lock(&qp->mutex);
  while (qp->len == NETSIM_QUEUE_LEN)
    netsim_cond_wait(&qp->cond, &qp->mutex);
  qp->items[qp->tail] = *evp;
  qp->tail = (qp->tail + 1) % NETSIM_QUEUE_LEN;
  qp->len++;
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
}

static void
queue_in_clear_session(struct netsim_in_queue *qp)
{

  netsim_mutex_lock(&qp->mutex);
  qp->head = 0;
  qp->tail = 0;
  qp->len = 0;
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
}

static void
queue_in_get(struct netsim_in_queue *qp, struct netsim_in_ev *evp)
{
  netsim_mutex_lock(&qp->mutex);
  while (qp->len == 0)
    netsim_cond_wait(&qp->cond, &qp->mutex);
  *evp = qp->items[qp->head];
  qp->head = (qp->head + 1) % NETSIM_QUEUE_LEN;
  qp->len--;
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
}

static bool
queue_in_timedget(struct netsim_in_queue *qp, struct netsim_in_ev *evp, int timeout_ms)
{
  netsim_deadline_t deadline;
  bool ok = true;

  deadline = netsim_deadline_after_ms(timeout_ms);
  netsim_mutex_lock(&qp->mutex);
  while (qp->len == 0 && ok)
    ok = netsim_cond_timedwait(&qp->cond, &qp->mutex, deadline);
  if (qp->len == 0) {
    netsim_mutex_unlock(&qp->mutex);
    return (false);
  }
  *evp = qp->items[qp->head];
  qp->head = (qp->head + 1) % NETSIM_QUEUE_LEN;
  qp->len--;
  netsim_cond_broadcast(&qp->cond);
  netsim_mutex_unlock(&qp->mutex);
  return (true);
}

static void
queue_out_wait_until(struct netsim_out_queue *qp, netsim_deadline_t deadline)
{

  netsim_mutex_lock(&qp->mutex);
  if (qp->len == 0)
    (void)netsim_cond_timedwait(&qp->cond, &qp->mutex, deadline);
  netsim_mutex_unlock(&qp->mutex);
}

static bool
timespec_due(netsim_deadline_t deadline)
{
  return (netsim_deadline_due(deadline));
}

static int
ns_delta_to_ms(int64_t delta_ns)
{
  int64_t delta_ms;

  delta_ms = delta_ns / 1000000LL;
  if (delta_ms > INT_MAX)
    return (INT_MAX);
  if (delta_ms < INT_MIN)
    return (INT_MIN);
  return ((int)delta_ms);
}

static void
netsim_out_ev_cleanup(struct netsim_out_ev *evp)
{

  if (evp->type == NETSIM_OUT_RX_PACKET && evp->pkt_buf != NULL) {
    free(evp->pkt_buf);
    evp->pkt_buf = NULL;
  }
  evp->pkt_len = 0;
}

static void
pending_schedule(struct pending_tx *ptx)
{
  int retry_ms;

  retry_ms = NETSIM_RETRY_MS;
  if (ptx->next_tx != 0) {
    ptx->next_tx += retry_ms;
    return;
  }
  ptx->next_tx = netsim_deadline_after_ms(retry_ms);
}

static uint64_t
make_nonce(void)
{
  uint64_t nonce;
  uint64_t seq;

  nonce = netsim_monotonic_ns();
  nonce ^= ((uint64_t)netsim_process_id() << 32);
  seq = atomic_fetch_add_explicit(&g_nonce_seq, 1, memory_order_relaxed);
  nonce ^= seq * 0x9e3779b97f4a7c15ULL;
  nonce ^= (uint64_t)(uintptr_t)&g_nonce_seq;
  return (nonce);
}

static bool
open_socket(const struct netsim_config *cfgp, netsim_socket_t *sockp,
  netsim_sockaddr_t *local_addrp, char *local_host, size_t local_host_len,
  char *local_port, size_t local_port_len)
{
  char bind_host[64], local_desc[64], errbuf[160];
  const struct sockaddr_in *sin;
  const bool peer_mode = (cfgp->sip.server_host[0] == '\0');

  if (!peer_mode &&
      !netsim_sockaddr_local_for_peer(cfgp->sip.server_host, cfgp->sip.server_port,
        bind_host, sizeof(bind_host), errbuf, sizeof(errbuf))) {
    netsim_err("%s", errbuf);
    return (false);
  }
  if (peer_mode)
    bind_host[0] = '\0';
  if (!netsim_socket_open_bound_udp(bind_host, peer_mode ? "5060" : "0", sockp, local_desc,
        sizeof(local_desc), errbuf, sizeof(errbuf))) {
    netsim_err("%s", errbuf);
    return (false);
  }
  if (!netsim_socket_getsockname(*sockp, local_addrp)) {
    netsim_socket_close(*sockp);
    *sockp = NETSIM_SOCKET_INVALID;
    return (false);
  }
  sin = (const struct sockaddr_in *)&local_addrp->ss;
  if (inet_ntop(AF_INET, &sin->sin_addr, local_host, local_host_len) == NULL) {
    netsim_socket_close(*sockp);
    *sockp = NETSIM_SOCKET_INVALID;
    return (false);
  }
  snprintf(local_port, local_port_len, "%u", (unsigned int)ntohs(sin->sin_port));
  netsim_log("socket ready local=%s server=%s%s%s", local_desc,
    peer_mode ? "<peer>" : cfgp->sip.server_host,
    peer_mode ? "" : ":",
    peer_mode ? "" : cfgp->sip.server_port);
  return (true);
}

static int
send_packet(netsim_socket_t sock, const netsim_sockaddr_t *addrp,
  uint32_t rtp_ssrc, uint32_t stream_ssrc,
  int type, int player, uint32_t tx_seq, uint32_t frame, uint8_t bits,
  uint64_t nonce)
{
  struct netsim_rtp_header hdr;
  struct netsim_rtp_payload payload;
  uint8_t buf[sizeof(hdr) + sizeof(payload)];

  memset(&hdr, '\0', sizeof(hdr));
  hdr.vpxcc = NETSIM_RTP_VPXCC;
  hdr.mpt = NETSIM_RTP_PT;
  hdr.seq = htons((uint16_t)(tx_seq & 0xffffU));
  hdr.timestamp = htonl(frame);
  hdr.ssrc = htonl(rtp_ssrc);

  memset(&payload, '\0', sizeof(payload));
  payload.subtype = (uint8_t)type;
  payload.player = (uint8_t)player;
  payload.tx_seq_hi = htons((uint16_t)(tx_seq >> 16));
  payload.bits = htonl(bits);
  payload.nonce = htonll(nonce);
  payload.stream_ssrc = htonl(stream_ssrc);

  memcpy(buf, &hdr, sizeof(hdr));
  memcpy(buf + sizeof(hdr), &payload, sizeof(payload));
  return (netsim_socket_sendto(sock, buf, sizeof(buf), addrp));
}

static bool
decode_packet(const void *buf, size_t len, struct netsim_pkt *pktp)
{
  const uint8_t *bp;
  struct netsim_rtp_header hdr;
  struct netsim_rtp_payload payload;
  struct netsim_pkt pkt;

  if (len != sizeof(hdr) + sizeof(payload))
    return (false);
  bp = buf;
  memcpy(&hdr, buf, sizeof(hdr));
  if ((hdr.vpxcc >> 6) != NETSIM_RTP_VERSION ||
      (hdr.vpxcc & NETSIM_RTP_VPXCC_FLAGS_MASK) != 0)
    return (false);
  if ((hdr.mpt & NETSIM_RTP_MPT_PT_MASK) != NETSIM_RTP_PT)
    return (false);
  memcpy(&payload, bp + sizeof(hdr), sizeof(payload));
  pkt.type = payload.subtype;
  pkt.player = payload.player;
  pkt.tx_seq = (((uint32_t)ntohs(payload.tx_seq_hi)) << 16) |
    (uint32_t)ntohs(hdr.seq);
  pkt.frame = ntohl(hdr.timestamp);
  pkt.bits = ntohl(payload.bits);
  pkt.nonce = ntohll(payload.nonce);
  pkt.rtp_ssrc = ntohl(hdr.ssrc);
  pkt.stream_ssrc = ntohl(payload.stream_ssrc);
  *pktp = pkt;
  return (true);
}

static void
push_error(struct netsim_in_queue *inqp)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_ERROR;
  queue_in_put(inqp, &inev);
}

static void
push_start(struct netsim_in_queue *inqp, int player, uint64_t nonce)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_START;
  inev.player = player;
  inev.nonce = nonce;
  queue_in_put(inqp, &inev);
}

static void
push_frame(struct netsim_in_queue *inqp, uint32_t frame, uint8_t bits,
  int remote_lead_ms)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_FRAME;
  inev.frame = frame;
  inev.bits = bits;
  inev.remote_lead_ms = remote_lead_ms;
  queue_in_put(inqp, &inev);
}

static void
push_exit(struct netsim_in_queue *inqp)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_EXIT;
  queue_in_put(inqp, &inev);
}

static void
push_gamestart(struct netsim_in_queue *inqp)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_GAMESTART;
  queue_in_put(inqp, &inev);
}

static void
push_start_ack(struct netsim_in_queue *inqp)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_START_ACK;
  queue_in_put(inqp, &inev);
}

static void
clear_frame_slots(struct remote_frame_slot *slots)
{

  memset(slots, '\0', sizeof(*slots) * NETSIM_FRAME_WINDOW);
}

static void
clear_peer_seen_slots(struct peer_frame_seen_slot *slots)
{

  memset(slots, '\0', sizeof(*slots) * NETSIM_FRAME_WINDOW);
}

static void
set_pending(struct pending_tx *ptx, int type, uint32_t frame, uint8_t bits,
  uint64_t nonce, uint32_t seq)
{
  ptx->active = true;
  ptx->type = type;
  ptx->seq = seq;
  ptx->frame = frame;
  ptx->bits = bits;
  ptx->nonce = nonce;
  ptx->matched = false;
  ptx->retries = 0;
  ptx->send_count = 0;
  ptx->first_send_ns = 0;
  ptx->peer_frame_base = 0;
  ptx->peer_frame_count = 0;
  ptx->next_tx = 0;
  netsim_log("queue %s seq=%u frame=%u bits=0x%02x", pending_name(type),
    (unsigned int)seq, (unsigned int)frame, (unsigned int)bits);
}

static int
pending_pkt_type(int pending_type)
{

  if (pending_type == NETSIM_PKT_HELLO)
    return (NETSIM_PKT_HELLO);
  if (pending_type == NETSIM_OUT_FRAME)
    return (NETSIM_PKT_FRAME);
  if (pending_type == NETSIM_OUT_EXIT)
    return (NETSIM_PKT_EXIT);
  if (pending_type == NETSIM_OUT_START)
    return (NETSIM_PKT_START);
  if (pending_type == NETSIM_OUT_START_ACK)
    return (NETSIM_PKT_START_ACK);
  return (0);
}

static uint32_t
pending_rtp_ssrc(uint32_t control_ssrc, uint32_t stream_ssrc, int pending_type)
{

  if (pending_type == NETSIM_PKT_HELLO || pending_type == NETSIM_OUT_START ||
      pending_type == NETSIM_OUT_START_ACK || stream_ssrc == 0)
    return (control_ssrc);
  return (stream_ssrc);
}

static uint32_t
pending_stream_ssrc(uint32_t stream_ssrc, int pending_type)
{

  if (pending_type == NETSIM_PKT_HELLO)
    return (0);
  return (stream_ssrc);
}

static void
pending_unschedule(struct pending_tx *ptx)
{

  ptx->next_tx = UINT64_MAX;
}

static bool
send_pending_now(netsim_socket_t sock, const netsim_sockaddr_t *addrp,
  uint32_t control_ssrc,
  uint32_t stream_ssrc, struct pending_tx *ptx, int player)
{
  int pkt_type;
  uint32_t rtp_ssrc;

  pkt_type = pending_pkt_type(ptx->type);
  if (pkt_type == 0)
    return (false);
  rtp_ssrc = pending_rtp_ssrc(control_ssrc, stream_ssrc, ptx->type);
  if (send_packet(sock, addrp, rtp_ssrc, pending_stream_ssrc(stream_ssrc, ptx->type),
        pkt_type, player, ptx->seq, ptx->frame, ptx->bits, ptx->nonce) < 0)
    return (false);
  if (ptx->first_send_ns == 0)
    ptx->first_send_ns = netsim_monotonic_ns();
  ptx->send_count++;
  return (true);
}

static bool
send_pending(netsim_socket_t sock, const netsim_sockaddr_t *addrp,
  uint32_t control_ssrc, uint32_t stream_ssrc,
  struct pending_tx *ptx, int player, bool *sentp)
{
  int type, err;
  char errbuf[128];
  uint32_t rtp_ssrc;

  *sentp = false;
  if (!ptx->active)
    return (true);
  if (!timespec_due(ptx->next_tx))
    return (true);
  if (ptx->type == NETSIM_OUT_FRAME && ptx->matched) {
    pending_unschedule(ptx);
    return (true);
  }
  type = pending_pkt_type(ptx->type);
  if (type == 0)
    return (false);
  rtp_ssrc = pending_rtp_ssrc(control_ssrc, stream_ssrc, ptx->type);
  if (send_packet(sock, addrp, rtp_ssrc, pending_stream_ssrc(stream_ssrc, ptx->type),
        type, player, ptx->seq, ptx->frame, ptx->bits, ptx->nonce) < 0) {
    err = netsim_socket_last_error();
    ptx->retries++;
    if (pending_log_retries(ptx->type) &&
        (ptx->retries == 1 || ptx->retries == 10 || ptx->retries == 50)) {
      netsim_log("send %s seq=%u frame=%u failed: %s (retry=%d)",
        pending_name(ptx->type), (unsigned int)ptx->seq,
        (unsigned int)ptx->frame,
        netsim_socket_strerror(err, errbuf, sizeof(errbuf)),
        ptx->retries);
    }
    if ((ptx->type == NETSIM_OUT_FRAME || ptx->type == NETSIM_OUT_EXIT) &&
        !ptx->matched &&
        ptx->retries > NETSIM_RETRY_LIMIT) {
      netsim_log("retry limit exceeded for %s seq=%u frame=%u after send failure",
        pending_name(ptx->type), (unsigned int)ptx->seq,
        (unsigned int)ptx->frame);
      return (false);
    }
    if (!netsim_socket_err_transient(err))
      return (false);
    pending_schedule(ptx);
    return (true);
  }
  ptx->retries++;
  *sentp = true;
  if (ptx->first_send_ns == 0)
    ptx->first_send_ns = netsim_monotonic_ns();
  ptx->send_count++;
  if (ptx->retries == 1) {
    netsim_log("send %s seq=%u frame=%u player=%d bits=0x%02x",
      pending_name(ptx->type), (unsigned int)ptx->seq,
      (unsigned int)ptx->frame, player + 1, (unsigned int)ptx->bits);
  } else if (pending_log_retries(ptx->type) &&
      (ptx->retries == 10 || ptx->retries == 50)) {
    netsim_log("retransmit %s seq=%u frame=%u retries=%d",
      pending_name(ptx->type), (unsigned int)ptx->seq,
      (unsigned int)ptx->frame, ptx->retries);
  }
  if ((ptx->type == NETSIM_OUT_FRAME || ptx->type == NETSIM_OUT_EXIT) &&
      !ptx->matched &&
      ptx->retries > NETSIM_RETRY_LIMIT) {
    netsim_log("retry limit exceeded for %s seq=%u frame=%u",
      pending_name(ptx->type), (unsigned int)ptx->seq,
      (unsigned int)ptx->frame);
    return (false);
  }
  if (ptx->type == NETSIM_OUT_FRAME && ptx->matched) {
    pending_unschedule(ptx);
    return (true);
  }
  if (ptx->type == NETSIM_OUT_FRAME && ptx->peer_frame_base > 0 &&
      ptx->send_count >=
      (ptx->peer_frame_count - ptx->peer_frame_base + 1)) {
    pending_unschedule(ptx);
    return (true);
  }
  pending_schedule(ptx);
  return (true);
}

struct netsim_thread_state {
  struct remote_frame_slot slots[NETSIM_FRAME_WINDOW];
  struct peer_frame_seen_slot peer_seen[NETSIM_FRAME_WINDOW];
  struct pending_tx hello_tx;
  struct pending_tx prev_tx;
  struct pending_tx tx;
  uint32_t pending_frame;
  uint8_t pending_frame_bits;
  uint64_t local_nonce;
  uint64_t peer_nonce;
  uint64_t session_nonce;
  uint32_t local_ctrl_ssrc;
  uint32_t local_stream_ssrc;
  uint32_t peer_stream_ssrc;
  netsim_sockaddr_t media_target;
  netsim_sockaddr_t media_source;
  uint32_t last_delivered;
  uint32_t last_peer_tx_seq;
  uint32_t next_tx_seq;
  netsim_socket_t sock;
  int local_player;
  int remote_player;
  bool peer_nonce_seen;
  bool peer_frame_seen;
  bool media_source_valid;
  bool exiting;
  bool acking_peer_exit;
  bool session_active;
  bool session_offer_local;
  bool game_start_notified;
  bool pending_frame_valid;
  bool prev_tx_valid;
};

static void thread_set_tx(struct netsim_thread_state *tsp, int type,
  uint32_t frame, uint8_t bits);

_Static_assert(sizeof(struct netsim_rtp_header) == 12,
  "netsim_rtp_header must match the RTP fixed header size");
_Static_assert(sizeof(struct netsim_rtp_payload) == 24,
  "netsim_rtp_payload must remain fixed-width on the wire");

static netsim_deadline_t
thread_next_wakeup(const struct netsim_thread_state *tsp)
{
  netsim_deadline_t deadline;

  deadline = netsim_deadline_after_ms(NETSIM_IDLE_WAKE_MS);
  if (tsp->tx.active && tsp->tx.next_tx < deadline)
    deadline = tsp->tx.next_tx;
  return (deadline);
}

static void
thread_clear_session(struct netsim_thread_state *tsp)
{

  memset(&tsp->hello_tx, '\0', sizeof(tsp->hello_tx));
  tsp->tx.active = false;
  memset(&tsp->tx, '\0', sizeof(tsp->tx));
  memset(&tsp->prev_tx, '\0', sizeof(tsp->prev_tx));
  tsp->prev_tx_valid = false;
  tsp->session_nonce = 0;
  tsp->local_stream_ssrc = 0;
  tsp->peer_stream_ssrc = 0;
  memset(&tsp->media_target, '\0', sizeof(tsp->media_target));
  memset(&tsp->media_source, '\0', sizeof(tsp->media_source));
  tsp->last_delivered = 0;
  tsp->last_peer_tx_seq = 0;
  tsp->next_tx_seq = 1;
  tsp->session_offer_local = false;
  tsp->session_active = false;
  tsp->exiting = false;
  tsp->acking_peer_exit = false;
  tsp->game_start_notified = false;
  tsp->pending_frame_valid = false;
  tsp->pending_frame = 0;
  tsp->pending_frame_bits = 0;
  tsp->media_source_valid = false;
  clear_frame_slots(tsp->slots);
  clear_peer_seen_slots(tsp->peer_seen);
  tsp->local_player = 0;
  tsp->remote_player = 1;
}

static bool
thread_tx_blocks_frame(const struct netsim_thread_state *tsp)
{

  return (tsp->tx.active &&
    !(tsp->tx.type == NETSIM_OUT_FRAME && tsp->tx.matched));
}

static void
thread_queue_pending_frame(struct netsim_thread_state *tsp, uint32_t frame,
  uint8_t bits)
{

  if (tsp->pending_frame_valid) {
    netsim_log("drop extra local frame frame=%u while frame=%u pending",
      (unsigned int)frame, (unsigned int)tsp->pending_frame);
    return;
  }
  tsp->pending_frame_valid = true;
  tsp->pending_frame = frame;
  tsp->pending_frame_bits = bits;
}

static void
thread_promote_pending_frame(struct netsim_thread_state *tsp)
{

  if (!tsp->pending_frame_valid || thread_tx_blocks_frame(tsp))
    return;
  thread_set_tx(tsp, NETSIM_OUT_FRAME, tsp->pending_frame,
    tsp->pending_frame_bits);
  tsp->pending_frame_valid = false;
}

static void
thread_reset_session(struct netsim_thread_state *tsp,
  struct netsim_out_queue *outq)
{

  thread_clear_session(tsp);
  queue_out_clear_session(outq);
}

static void
thread_begin_session(struct netsim_thread_state *tsp, uint64_t session_nonce,
  int local_player, int remote_player, bool session_offer_local)
{

  if (tsp->tx.active || tsp->prev_tx_valid) {
    netsim_log("reset stale tx state at session begin cur_active=%d cur_seq=%u cur_frame=%u prev_valid=%d prev_seq=%u prev_frame=%u",
      tsp->tx.active, (unsigned int)tsp->tx.seq,
      (unsigned int)tsp->tx.frame, tsp->prev_tx_valid,
      (unsigned int)tsp->prev_tx.seq, (unsigned int)tsp->prev_tx.frame);
  }
  thread_clear_session(tsp);
  tsp->session_offer_local = session_offer_local;
  tsp->session_nonce = session_nonce;
  tsp->local_player = local_player;
  tsp->remote_player = remote_player;
}

static void
thread_fail_session(struct netsim_thread_state *tsp, struct netsim_in_queue *inq,
  struct netsim_out_queue *outq, uint32_t frame)
{

  push_error(inq);
  if (!tsp->session_active || tsp->session_nonce == 0) {
    thread_reset_session(tsp, outq);
    return;
  }
  tsp->exiting = true;
  queue_out_clear_session(outq);
  tsp->tx.active = false;
  (void)frame;
}

static void
thread_note_peer_frame_seen(struct netsim_thread_state *tsp, uint32_t peer_frame)
{
  struct peer_frame_seen_slot *ssp;
#if defined(DIGGER_DEBUG)
  bool has_local_match;
#endif

#if defined(DIGGER_DEBUG)
  has_local_match = ((tsp->tx.active && tsp->tx.type == NETSIM_OUT_FRAME &&
      tsp->tx.frame == peer_frame) ||
    (tsp->prev_tx_valid && tsp->prev_tx.type == NETSIM_OUT_FRAME &&
      tsp->prev_tx.frame == peer_frame));
#endif
  ssp = &tsp->peer_seen[peer_frame % NETSIM_FRAME_WINDOW];
  if (!ssp->valid || ssp->frame != peer_frame) {
    ssp->valid = true;
    ssp->frame = peer_frame;
    ssp->count = 1;
    return;
  }
  ssp->count++;
#if defined(DIGGER_DEBUG)
  if (!has_local_match && ssp->count > 1) {
    netsim_proto_log("peer ahead frame=%u parity=0/%d cur_frame=%u cur_active=%d prev_frame=%u prev_valid=%d",
      (unsigned int)peer_frame, ssp->count, (unsigned int)tsp->tx.frame,
      tsp->tx.active, (unsigned int)tsp->prev_tx.frame, tsp->prev_tx_valid);
  }
#endif
}

static int
thread_peer_frame_seen_count(const struct netsim_thread_state *tsp,
  uint32_t peer_frame)
{
  const struct peer_frame_seen_slot *ssp;

  ssp = &tsp->peer_seen[peer_frame % NETSIM_FRAME_WINDOW];
  if (!ssp->valid || ssp->frame != peer_frame)
    return (0);
  return (ssp->count);
}

static void
thread_ack_tx(struct netsim_thread_state *tsp, uint32_t peer_seq)
{

  if (tsp->prev_tx_valid && tsp->prev_tx.seq + 1 == peer_seq) {
    netsim_log("recv ack %s seq=%u frame=%u via peer seq=%u",
      pending_name(tsp->prev_tx.type), (unsigned int)tsp->prev_tx.seq,
      (unsigned int)tsp->prev_tx.frame, (unsigned int)peer_seq);
    tsp->prev_tx_valid = false;
  }
  if (tsp->tx.active && peer_seq > tsp->tx.seq) {
    netsim_log("recv ack %s seq=%u frame=%u via peer seq=%u",
      pending_name(tsp->tx.type), (unsigned int)tsp->tx.seq,
      (unsigned int)tsp->tx.frame, (unsigned int)peer_seq);
    tsp->tx.active = false;
  }
}

static void
thread_set_tx(struct netsim_thread_state *tsp, int type, uint32_t frame,
  uint8_t bits)
{
  int peer_seen;

  if (tsp->tx.seq != 0) {
    tsp->prev_tx = tsp->tx;
    tsp->prev_tx_valid = true;
  }
  set_pending(&tsp->tx, type, frame, bits, tsp->session_nonce,
    tsp->next_tx_seq++);
  if (type == NETSIM_OUT_FRAME) {
    peer_seen = thread_peer_frame_seen_count(tsp, frame);
    tsp->tx.peer_frame_base = peer_seen;
    tsp->tx.peer_frame_count = peer_seen;
#if defined(DIGGER_DEBUG)
    if (peer_seen > 1) {
      netsim_proto_log("local frame catches up frame=%u seq=%u inherited parity=%d/%d",
        (unsigned int)frame, (unsigned int)tsp->tx.seq, tsp->tx.send_count,
        peer_seen);
    }
#endif
  }
}

enum peer_seq_result {
  PEER_SEQ_DUPLICATE = 0,
  PEER_SEQ_ACCEPTED,
  PEER_SEQ_GAP
};

static enum peer_seq_result
thread_accept_peer_seq(struct netsim_thread_state *tsp, uint32_t peer_seq)
{

  if (peer_seq == 0)
    return (PEER_SEQ_DUPLICATE);
  thread_ack_tx(tsp, peer_seq);
  if (peer_seq <= tsp->last_peer_tx_seq)
    return (PEER_SEQ_DUPLICATE);
  if (peer_seq != tsp->last_peer_tx_seq + 1)
    return (PEER_SEQ_GAP);
  tsp->last_peer_tx_seq = peer_seq;
  return (PEER_SEQ_ACCEPTED);
}

static void
thread_note_peer_frame(struct pending_tx *ptx, uint32_t peer_frame,
  const char *which)
{

  (void)which;
  if (ptx->type != NETSIM_OUT_FRAME || ptx->frame != peer_frame)
    return;
  if (ptx->matched)
    return;
  ptx->peer_frame_count++;
#if defined(DIGGER_DEBUG)
  if (ptx->peer_frame_count > ptx->send_count + 1) {
    netsim_proto_log("peer parity drift frame=%u %s seq=%u parity=%d/%d",
      (unsigned int)peer_frame, which, (unsigned int)ptx->seq,
      ptx->send_count, ptx->peer_frame_count);
  }
#endif
}

static void
thread_note_matching_peer_frame(struct netsim_thread_state *tsp, uint32_t peer_frame)
{

  if (tsp->tx.active)
    thread_note_peer_frame(&tsp->tx, peer_frame, "current");
  if (tsp->prev_tx_valid)
    thread_note_peer_frame(&tsp->prev_tx, peer_frame, "previous");
}

static void
thread_mark_matching_peer_tx(struct netsim_thread_state *tsp, uint32_t peer_seq,
  uint32_t peer_frame)
{

  if (tsp->tx.active && tsp->tx.type == NETSIM_OUT_FRAME &&
      tsp->tx.seq == peer_seq && tsp->tx.frame == peer_frame) {
    tsp->tx.matched = true;
    pending_unschedule(&tsp->tx);
  }
  if (tsp->prev_tx_valid && tsp->prev_tx.type == NETSIM_OUT_FRAME &&
      tsp->prev_tx.seq == peer_seq && tsp->prev_tx.frame == peer_frame)
    tsp->prev_tx.matched = true;
}

static void
thread_resend_to_match(struct pending_tx *ptx, netsim_socket_t sock,
  const netsim_sockaddr_t *addrp, int player, uint32_t control_ssrc,
  uint32_t stream_ssrc, uint32_t peer_frame, const char *which)
{

  if (ptx->type != NETSIM_OUT_FRAME || ptx->frame != peer_frame)
    return;
  if (ptx->matched)
    return;
  if (ptx->send_count >= ptx->peer_frame_count)
    return;
  if (send_pending_now(sock, addrp, control_ssrc, stream_ssrc, ptx, player))
    netsim_log("peer repeated frame=%u, resend %s seq=%u parity=%d/%d",
      (unsigned int)peer_frame, which, (unsigned int)ptx->seq,
      ptx->send_count, ptx->peer_frame_count);
}

static void
thread_resend_matching_frame(struct netsim_thread_state *tsp,
  uint32_t peer_frame)
{

  if (tsp->tx.active)
    thread_resend_to_match(&tsp->tx, tsp->sock, &tsp->media_target,
      tsp->local_player,
      tsp->local_ctrl_ssrc, tsp->local_stream_ssrc,
      peer_frame, "current");
  if (tsp->prev_tx_valid)
    thread_resend_to_match(&tsp->prev_tx, tsp->sock, &tsp->media_target,
      tsp->local_player,
      tsp->local_ctrl_ssrc, tsp->local_stream_ssrc,
      peer_frame, "previous");
}

enum thread_step_result {
  THREAD_STEP_NEXT = 0,
  THREAD_STEP_CONTINUE,
  THREAD_STEP_STOP
};

static void
thread_apply_sip_event(struct netsim_thread_state *tsp, struct thread_ctx *ctxp,
  const struct netsim_sip_event *evp)
{
  switch (evp->type) {
    case NETSIM_SIP_EVENT_REMOTE_START:
      thread_begin_session(tsp, evp->session.session_nonce,
        evp->session.local_player, evp->session.remote_player, false);
      tsp->local_stream_ssrc = evp->session.local_stream_ssrc;
      tsp->peer_stream_ssrc = evp->session.peer_stream_ssrc;
      tsp->media_target = evp->session.media_addr;
      push_start(ctxp->inq, tsp->local_player, tsp->session_nonce);
      push_gamestart(ctxp->inq);
      break;
    case NETSIM_SIP_EVENT_CONNECTED:
      if (evp->session.session_nonce != 0 && evp->session.session_nonce != tsp->session_nonce) {
        thread_begin_session(tsp, evp->session.session_nonce,
          evp->session.local_player, evp->session.remote_player, false);
      }
      tsp->local_stream_ssrc = evp->session.local_stream_ssrc;
      tsp->peer_stream_ssrc = evp->session.peer_stream_ssrc;
      tsp->media_target = evp->session.media_addr;
      tsp->session_active = true;
      netsim_log("session connected local_player=%d session=0x%016llx",
        tsp->local_player + 1, (unsigned long long)tsp->session_nonce);
      push_start_ack(ctxp->inq);
      break;
    case NETSIM_SIP_EVENT_DISCONNECTED:
      if (!tsp->exiting)
        push_exit(ctxp->inq);
      thread_reset_session(tsp, ctxp->outq);
      break;
    case NETSIM_SIP_EVENT_ERROR:
      push_error(ctxp->inq);
      thread_reset_session(tsp, ctxp->outq);
      break;
    default:
      break;
  }
}

static void
thread_drain_sip_events(struct netsim_thread_state *tsp, struct thread_ctx *ctxp)
{
  struct netsim_sip_event ev;

  while (netsim_sip_pop_event(ctxp->sip, &ev))
    thread_apply_sip_event(tsp, ctxp, &ev);
}

static void
thread_process_rx_packet(struct netsim_thread_state *tsp, struct thread_ctx *ctxp,
  const struct netsim_out_ev *outevp)
{
  struct netsim_pkt pkt;
  const netsim_sockaddr_t *peer_addrp;
  enum peer_seq_result seq_rval;

  peer_addrp = &outevp->peer_addr;
  if (netsim_sip_packet_looks_like(outevp->pkt_buf, outevp->pkt_len)) {
    (void)netsim_sip_handle_packet(ctxp->sip, outevp->pkt_buf, outevp->pkt_len, peer_addrp,
      &ctxp->local_addr);
    return;
  }
  if (!decode_packet(outevp->pkt_buf, outevp->pkt_len, &pkt))
    return;
  if (!tsp->session_active || tsp->session_nonce == 0 ||
      pkt.nonce != tsp->session_nonce || pkt.type != NETSIM_PKT_FRAME)
    return;
  if (tsp->peer_stream_ssrc != 0 && pkt.rtp_ssrc != tsp->peer_stream_ssrc)
    return;
  if ((int)pkt.player != tsp->remote_player)
    return;
  if (!tsp->media_source_valid) {
    tsp->media_source = *peer_addrp;
    tsp->media_source_valid = true;
  } else if (!netsim_sockaddr_same(&tsp->media_source, peer_addrp)) {
    return;
  }
  if (pkt.tx_seq < tsp->last_peer_tx_seq)
    return;
  thread_note_peer_frame_seen(tsp, pkt.frame);
  thread_note_matching_peer_frame(tsp, pkt.frame);
  thread_mark_matching_peer_tx(tsp, pkt.tx_seq, pkt.frame);
  seq_rval = thread_accept_peer_seq(tsp, pkt.tx_seq);
  switch (seq_rval) {
    case PEER_SEQ_DUPLICATE:
      thread_resend_matching_frame(tsp, pkt.frame);
      return;

    case PEER_SEQ_GAP:
      netsim_log("peer seq gap peer_seq=%u expected=%u frame=%u last_delivered=%u",
        (unsigned int)pkt.tx_seq, (unsigned int)(tsp->last_peer_tx_seq + 1),
        (unsigned int)pkt.frame, (unsigned int)tsp->last_delivered);
      thread_fail_session(tsp, ctxp->inq, ctxp->outq, pkt.frame);
      return;

    case PEER_SEQ_ACCEPTED:
      break;
  }
  tsp->peer_frame_seen = true;
  if (pkt.frame <= tsp->last_delivered)
    return;
  if (pkt.frame > tsp->last_delivered + NETSIM_FRAME_WINDOW) {
    netsim_log("frame window overflow frame=%u last_delivered=%u",
      (unsigned int)pkt.frame, (unsigned int)tsp->last_delivered);
    thread_fail_session(tsp, ctxp->inq, ctxp->outq, pkt.frame);
    return;
  }
  if (!tsp->slots[pkt.frame % NETSIM_FRAME_WINDOW].valid) {
    struct remote_frame_slot rslot;

    rslot.valid = true;
    rslot.frame = pkt.frame;
    rslot.bits = (uint8_t)pkt.bits;
    rslot.first_recv_ns = netsim_monotonic_ns();
    tsp->slots[pkt.frame % NETSIM_FRAME_WINDOW] = rslot;
  }
}

static void *
netsim_rx_thread(void *arg)
{
  struct thread_ctx *ctxp;
  struct netsim_out_ev outev;
  netsim_sockaddr_t peer_addr;
  uint8_t *buf;
  int rlen, err;

  ctxp = (struct thread_ctx *)arg;
  while (!atomic_load_explicit(&ctxp->stop_requested, memory_order_relaxed)) {
    buf = malloc(NETSIM_RX_BUFSIZE);
    if (buf == NULL) {
      memset(&outev, '\0', sizeof(outev));
      outev.type = NETSIM_OUT_RX_ERROR;
      outev.err = ENOMEM;
      queue_out_put(ctxp->outq, &outev);
      return (NULL);
    }
    rlen = netsim_socket_recvfrom(ctxp->sock, buf, NETSIM_RX_BUFSIZE, &peer_addr);
    if (rlen < 0) {
      err = netsim_socket_last_error();
      free(buf);
      if (netsim_socket_err_wouldblock(err) || netsim_socket_err_transient(err))
        continue;
      memset(&outev, '\0', sizeof(outev));
      outev.type = NETSIM_OUT_RX_ERROR;
      outev.err = err;
      queue_out_put(ctxp->outq, &outev);
      return (NULL);
    }
    memset(&outev, '\0', sizeof(outev));
    outev.type = NETSIM_OUT_RX_PACKET;
    outev.pkt_len = (size_t)rlen;
    outev.peer_addr = peer_addr;
    outev.pkt_buf = buf;
    queue_out_put(ctxp->outq, &outev);
  }
  queue_out_wake(ctxp->outq);
  return (NULL);
}

static bool
thread_lookup_local_first_send(const struct netsim_thread_state *tsp,
  uint32_t frame, uint64_t *first_send_nsp)
{

  if (tsp->tx.active && tsp->tx.type == NETSIM_OUT_FRAME &&
      tsp->tx.frame == frame && tsp->tx.first_send_ns != 0) {
    *first_send_nsp = tsp->tx.first_send_ns;
    return (true);
  }
  if (tsp->prev_tx_valid && tsp->prev_tx.type == NETSIM_OUT_FRAME &&
      tsp->prev_tx.frame == frame && tsp->prev_tx.first_send_ns != 0) {
    *first_send_nsp = tsp->prev_tx.first_send_ns;
    return (true);
  }
  return (false);
}

static void
thread_deliver_ready_frames(struct netsim_thread_state *tsp,
  struct thread_ctx *ctxp)
{
  while (tsp->slots[(tsp->last_delivered + 1) % NETSIM_FRAME_WINDOW].valid &&
         tsp->slots[(tsp->last_delivered + 1) % NETSIM_FRAME_WINDOW].frame ==
         tsp->last_delivered + 1) {
    struct remote_frame_slot *fsp;
    uint64_t first_send_ns;
    int64_t delta_ns;
    uint32_t next_frame;

    next_frame = tsp->last_delivered + 1;
    if (!thread_lookup_local_first_send(tsp, next_frame, &first_send_ns))
      break;
    fsp = &tsp->slots[next_frame % NETSIM_FRAME_WINDOW];
    if (first_send_ns >= fsp->first_recv_ns)
      delta_ns = (int64_t)(first_send_ns - fsp->first_recv_ns);
    else
      delta_ns = -(int64_t)(fsp->first_recv_ns - first_send_ns);
#if defined(DIGGER_DEBUG)
    netsim_proto_log("push_frame: frame=%u first_send_ns=%llu first_recv_ns=%llu lead_ms=%d",
      fsp->frame, (unsigned long long)first_send_ns,
      (unsigned long long)fsp->first_recv_ns, ns_delta_to_ms(delta_ns));
#endif
    push_frame(ctxp->inq, fsp->frame, fsp->bits, ns_delta_to_ms(delta_ns));
    fsp->valid = false;
    tsp->last_delivered++;
  }
}

static enum thread_step_result
thread_handle_outgoing(struct netsim_thread_state *tsp, struct thread_ctx *ctxp)
{
  struct netsim_out_ev outev;
  char errbuf[128];
  enum thread_step_result rval;

  if (!queue_out_tryget(ctxp->outq, &outev))
    return (THREAD_STEP_NEXT);
  rval = THREAD_STEP_NEXT;
  switch (outev.type) {
    case NETSIM_OUT_RX_ERROR:
      netsim_log("recv failed: %s",
        netsim_socket_strerror(outev.err, errbuf, sizeof(errbuf)));
      push_error(ctxp->inq);
      thread_reset_session(tsp, ctxp->outq);
      rval = THREAD_STEP_CONTINUE;
      break;

    case NETSIM_OUT_RX_PACKET:
      thread_process_rx_packet(tsp, ctxp, &outev);
      break;

    case NETSIM_OUT_START: {
      char sip_errbuf[160];

      sip_errbuf[0] = '\0';
      thread_begin_session(tsp, outev.nonce, 0, 1, true);
      tsp->local_stream_ssrc = make_stream_ssrc(tsp->local_nonce, tsp->session_nonce);
      netsim_log("local start offer session=0x%016llx local_player=%d",
        (unsigned long long)tsp->session_nonce, tsp->local_player + 1);
      push_start(ctxp->inq, tsp->local_player, tsp->session_nonce);
      if (!netsim_sip_start_call(ctxp->sip, tsp->session_nonce,
            tsp->local_stream_ssrc, tsp->local_player, sip_errbuf,
            sizeof(sip_errbuf))) {
        netsim_log("SIP start failed: %s", sip_errbuf);
        push_error(ctxp->inq);
        thread_reset_session(tsp, ctxp->outq);
        rval = THREAD_STEP_CONTINUE;
      } else if (sip_errbuf[0] != '\0') {
        netsim_log("SIP start deferred: %s", sip_errbuf);
      }
      break;
    }

    case NETSIM_OUT_START_ACK:
      break;

    case NETSIM_OUT_EXIT:
      if (!tsp->session_active) {
        netsim_log("drop stale %s frame=%u while session inactive",
          pending_name(outev.type), (unsigned int)outev.frame);
        break;
      }
      netsim_log("local exit queued");
      tsp->exiting = true;
      tsp->session_active = false;
      tsp->tx.active = false;
      tsp->prev_tx_valid = false;
      clear_frame_slots(tsp->slots);
      clear_peer_seen_slots(tsp->peer_seen);
      netsim_sip_hangup(ctxp->sip);
      queue_out_clear_session(ctxp->outq);
      break;

    case NETSIM_OUT_FRAME:
      if (!tsp->session_active) {
        netsim_log("drop stale %s frame=%u while session inactive",
          pending_name(outev.type), (unsigned int)outev.frame);
        break;
      }
      if (thread_tx_blocks_frame(tsp))
        thread_queue_pending_frame(tsp, outev.frame, outev.bits);
      else
        thread_set_tx(tsp, outev.type, outev.frame, outev.bits);
      break;

    case NETSIM_OUT_STOP:
      netsim_log("stop requested");
      tsp->exiting = true;
      tsp->session_active = false;
      tsp->tx.active = false;
      tsp->prev_tx_valid = false;
      tsp->pending_frame_valid = false;
      clear_frame_slots(tsp->slots);
      clear_peer_seen_slots(tsp->peer_seen);
      netsim_sip_hangup(ctxp->sip);
      queue_out_clear_session(ctxp->outq);
      rval = THREAD_STEP_CONTINUE;
      break;

    case NETSIM_OUT_SHUTDOWN:
      netsim_log("shutdown signal received");
      tsp->tx.active = false;
      tsp->pending_frame_valid = false;
      rval = THREAD_STEP_STOP;
      break;

    default:
      assert(false && "unexpected out queue event");
      break;
  }
  netsim_out_ev_cleanup(&outev);
  return (rval);
}

static enum thread_step_result
thread_send_ready(struct netsim_thread_state *tsp, struct thread_ctx *ctxp)
{
  bool data_sent;

  if (!tsp->session_active || tsp->media_target.len == 0)
    return (THREAD_STEP_NEXT);
  if (!send_pending(tsp->sock, &tsp->media_target, tsp->local_ctrl_ssrc,
        tsp->local_stream_ssrc, &tsp->tx, tsp->local_player, &data_sent)) {
    netsim_log("session data send failed permanently, resetting session");
    thread_fail_session(tsp, ctxp->inq, ctxp->outq, tsp->tx.frame);
    return (THREAD_STEP_CONTINUE);
  }
  return (THREAD_STEP_NEXT);
}

static void *
netsim_thread(void *arg)
{
  struct thread_ctx *ctxp;
  struct netsim_thread_state ts;
  enum thread_step_result step;
  bool thread_stop;

  ctxp = (struct thread_ctx *)arg;
  memset(&ts, '\0', sizeof(ts));
  clear_frame_slots(ts.slots);
  thread_stop = false;
  ts.remote_player = 1;
  ts.local_nonce = make_nonce();
  ts.local_ctrl_ssrc = make_ssrc(ts.local_nonce);
  ts.sock = ctxp->sock;
  netsim_log("thread start local=%s:%s sip=%s:%s local_nonce=0x%016llx",
    ctxp->local_host, ctxp->local_port,
    ctxp->cfg.sip.server_host, ctxp->cfg.sip.server_port,
    (unsigned long long)ts.local_nonce);

  while (!thread_stop) {
    thread_drain_sip_events(&ts, ctxp);
    step = thread_handle_outgoing(&ts, ctxp);
    switch (step) {
      case THREAD_STEP_STOP:
        thread_stop = true;
        continue;

      case THREAD_STEP_CONTINUE:
        continue;

      case THREAD_STEP_NEXT:
        break;
    }
    (void)netsim_sip_run(ctxp->sip);
    thread_drain_sip_events(&ts, ctxp);
    thread_promote_pending_frame(&ts);
    if (thread_send_ready(&ts, ctxp) == THREAD_STEP_CONTINUE)
      continue;
    thread_deliver_ready_frames(&ts, ctxp);
    queue_out_wait_until(ctxp->outq,
      netsim_sip_next_wakeup(ctxp->sip, thread_next_wakeup(&ts)));
  }
  atomic_store_explicit(&ctxp->stop_requested, true, memory_order_relaxed);
  queue_out_wake(ctxp->outq);
  if (ctxp->rx_started)
    netsim_thread_join(ctxp->rx_thr);
  netsim_sip_destroy(ctxp->sip);
  if (ts.sock != NETSIM_SOCKET_INVALID)
    netsim_socket_close(ts.sock);
  netsim_log("thread stop send_stop=%d session_active=%d exiting=%d peer_frame_seen=%d",
    thread_stop, ts.session_active, ts.exiting, ts.peer_frame_seen);
  free(ctxp);
  return (NULL);
}

bool
netsim_configure(const char *spec)
{
  const char *dashp, *atp, *passp;
  size_t len;

  memset(&g_cfg, '\0', sizeof(g_cfg));
  dashp = strrchr(spec, '-');
  atp = strchr(spec, '@');
  if (dashp == NULL || dashp == spec || dashp[1] == '\0')
    return (false);
  if (atp == NULL || atp >= dashp) {
    len = (size_t)(dashp - spec);
    if (len == 0 || len >= sizeof(g_cfg.sip.username))
      return (false);
    memcpy(g_cfg.sip.username, spec, len);
    g_cfg.sip.username[len] = '\0';
    strcpy(g_cfg.sip.server_port, "5060");
  } else {
    passp = memchr(spec, ':', (size_t)(atp - spec));
    if (passp == NULL) {
      len = (size_t)(atp - spec);
      if (len == 0 || len >= sizeof(g_cfg.sip.username))
        return (false);
      memcpy(g_cfg.sip.username, spec, len);
      g_cfg.sip.username[len] = '\0';
    } else {
      len = (size_t)(passp - spec);
      if (len == 0 || len >= sizeof(g_cfg.sip.username))
        return (false);
      memcpy(g_cfg.sip.username, spec, len);
      g_cfg.sip.username[len] = '\0';
      len = (size_t)(atp - (passp + 1));
      if (len == 0 || len >= sizeof(g_cfg.sip.password))
        return (false);
      memcpy(g_cfg.sip.password, passp + 1, len);
      g_cfg.sip.password[len] = '\0';
    }
    len = (size_t)(dashp - (atp + 1));
    if (len == 0 || len >= sizeof(g_cfg.sip.server_host))
      return (false);
    {
      char hostbuf[272];

      memcpy(hostbuf, atp + 1, len);
      hostbuf[len] = '\0';
      if (!parse_sip_host_port(hostbuf, g_cfg.sip.server_host,
            sizeof(g_cfg.sip.server_host), g_cfg.sip.server_port,
            sizeof(g_cfg.sip.server_port)))
        return (false);
    }
  }
  if (strlen(dashp + 1) >= sizeof(g_cfg.sip.peer_user))
    return (false);
  strcpy(g_cfg.sip.peer_user, dashp + 1);
  g_cfg.configured = true;
  netsim_log("configured sip_user=%s server=%s%s%s peer=%s",
    g_cfg.sip.username,
    g_cfg.sip.server_host[0] != '\0' ? g_cfg.sip.server_host : "<peer>",
    g_cfg.sip.server_host[0] != '\0' ? ":" : "",
    g_cfg.sip.server_host[0] != '\0' ? g_cfg.sip.server_port : "",
    g_cfg.sip.peer_user);
  return (true);
}

bool
netsim_configured(void)
{

  return (g_cfg.configured);
}

bool
netsim_begin_wait(void)
{
  struct thread_ctx *ctxp;
  netsim_socket_t sock;
  char errbuf[160];

  if (!g_cfg.configured || g_session.running)
    return (g_session.running);
  if (!netsim_platform_init())
    return (false);
  netsim_log("begin_wait requested sip_user=%s server=%s%s%s peer=%s",
    g_cfg.sip.username,
    g_cfg.sip.server_host[0] != '\0' ? g_cfg.sip.server_host : "<peer>",
    g_cfg.sip.server_host[0] != '\0' ? ":" : "",
    g_cfg.sip.server_host[0] != '\0' ? g_cfg.sip.server_port : "",
    g_cfg.sip.peer_user);
  queue_out_init(&g_session.outq);
  queue_in_init(&g_session.inq);
  g_session.running = true;
  netsim_reset_state(NETSIM_SESSION_WAITING);
  sock = NETSIM_SOCKET_INVALID;
  ctxp = calloc(1, sizeof(*ctxp));
  if (ctxp == NULL)
    goto fail;
  if (!open_socket(&g_cfg, &sock, &ctxp->local_addr, ctxp->local_host,
        sizeof(ctxp->local_host), ctxp->local_port, sizeof(ctxp->local_port))) {
    free(ctxp);
    goto fail;
  }
  ctxp->sip = netsim_sip_create(&g_cfg.sip, sock, ctxp->local_host, ctxp->local_port,
    errbuf, sizeof(errbuf));
  if (ctxp->sip == NULL) {
    netsim_err("%s", errbuf);
    netsim_socket_close(sock);
    free(ctxp);
    goto fail;
  }
  ctxp->cfg = g_cfg;
  ctxp->sock = sock;
  ctxp->outq = &g_session.outq;
  ctxp->inq = &g_session.inq;
  atomic_init(&ctxp->stop_requested, false);
  if (!netsim_thread_create(&ctxp->rx_thr, netsim_rx_thread, ctxp)) {
    netsim_sip_destroy(ctxp->sip);
    netsim_socket_close(sock);
    free(ctxp);
    goto fail;
  }
  ctxp->rx_started = true;
  if (!netsim_thread_create(&g_session.thr, netsim_thread, ctxp)) {
    atomic_store_explicit(&ctxp->stop_requested, true, memory_order_relaxed);
    if (ctxp->rx_started)
      netsim_thread_join(ctxp->rx_thr);
    netsim_sip_destroy(ctxp->sip);
    netsim_socket_close(sock);
    free(ctxp);
    goto fail;
  }
  return (true);
fail:
  netsim_log("begin_wait failed");
  g_session.running = false;
  g_session.state = NETSIM_SESSION_WAITING;
  queue_out_destroy(&g_session.outq);
  queue_in_destroy(&g_session.inq);
  netsim_platform_cleanup();
  return (false);
}

static bool
queue_in_tryget(struct netsim_in_queue *qp, struct netsim_in_ev *evp)
{
  bool ok = false;

  netsim_mutex_lock(&qp->mutex);
  if (qp->len > 0) {
    *evp = qp->items[qp->head];
    qp->head = (qp->head + 1) % NETSIM_QUEUE_LEN;
    qp->len--;
    netsim_cond_broadcast(&qp->cond);
    ok = true;
  }
  netsim_mutex_unlock(&qp->mutex);
  return (ok);
}

static void
netsim_handle_prestart_event(const struct netsim_in_ev *inev)
{

  switch (inev->type) {
    case NETSIM_IN_START:
      g_session.local_player = inev->player;
      g_session.session_nonce = inev->nonce;
      if (g_session.state == NETSIM_SESSION_REMOTE_START)
        g_session.state = NETSIM_SESSION_READY_TO_ACK;
      else if (g_session.state != NETSIM_SESSION_START_ACK &&
               g_session.state != NETSIM_SESSION_STARTED)
        g_session.state = NETSIM_SESSION_ASSIGNED;
      break;
    case NETSIM_IN_GAMESTART:
      if (g_session.state == NETSIM_SESSION_ASSIGNED)
        g_session.state = NETSIM_SESSION_READY_TO_ACK;
      else if (g_session.state != NETSIM_SESSION_START_ACK &&
               g_session.state != NETSIM_SESSION_STARTED)
        g_session.state = NETSIM_SESSION_REMOTE_START;
      break;
    case NETSIM_IN_START_ACK:
      g_session.state = NETSIM_SESSION_START_ACK;
      break;
    case NETSIM_IN_EXIT:
      break;
    case NETSIM_IN_ERROR:
      g_session.state = NETSIM_SESSION_ERROR;
      break;
  }
}

static void
netsim_pump_prestart(void)
{
  struct netsim_in_ev inev;

  if (!g_session.running || netsim_is_started())
    return;
  while (queue_in_tryget(&g_session.inq, &inev))
    netsim_handle_prestart_event(&inev);
}

bool
netsim_start_session(bool initiated_locally)
{
  struct netsim_in_ev inev;
  struct netsim_out_ev outev;
  bool ack_queued = false;
  bool got_event;

  if (!g_cfg.configured)
    return (false);
  if (!g_session.running && !netsim_begin_wait())
    return (false);
  netsim_pump_prestart();
  if (netsim_is_error() || netsim_is_peer_exited())
    goto fail;
  if (netsim_is_started())
    return (true);
  if (initiated_locally) {
    g_session.state = NETSIM_SESSION_WAITING;
    g_session.session_nonce = make_nonce();
    memset(&outev, '\0', sizeof(outev));
    outev.type = NETSIM_OUT_START;
    outev.frame = 0;
    outev.nonce = g_session.session_nonce;
    queue_out_put(&g_session.outq, &outev);
    netsim_log("start_session waiting for start ack session=0x%016llx",
      (unsigned long long)g_session.session_nonce);
  } else {
    netsim_log("start_session waiting for remote start");
  }
  for (;;) {
    if (!ack_queued && netsim_is_ready_to_ack()) {
      memset(&outev, '\0', sizeof(outev));
      outev.type = NETSIM_OUT_START_ACK;
      outev.frame = 0;
      outev.nonce = g_session.session_nonce;
      queue_out_put(&g_session.outq, &outev);
      ack_queued = true;
      netsim_log("start_session ack queued session=0x%016llx local_player=%d",
        (unsigned long long)g_session.session_nonce, g_session.local_player + 1);
    }
    if (netsim_has_start_ack()) {
      g_session.state = NETSIM_SESSION_STARTED;
      netsim_log("session started local_player=%d session=0x%016llx",
        g_session.local_player + 1, (unsigned long long)g_session.session_nonce);
      return (true);
    }
    if (initiated_locally) {
      got_event = queue_in_timedget(&g_session.inq, &inev, 5000);
      if (!got_event) {
        netsim_log("start_session timed out waiting for start ack session=0x%016llx",
          (unsigned long long)g_session.session_nonce);
        break;
      }
    } else {
      queue_in_get(&g_session.inq, &inev);
    }
    netsim_handle_prestart_event(&inev);
    if (netsim_is_error() || netsim_is_peer_exited())
      break;
  }
fail:
  netsim_log("session start failed");
  queue_in_clear_session(&g_session.inq);
  netsim_reset_state(NETSIM_SESSION_WAITING);
  return (false);
}

void
netsim_stop_session(bool send_exit)
{
  struct netsim_out_ev outev;

  if (!g_session.running || !netsim_is_started())
    return;
  netsim_log("stop_session send_exit=%d started=%d peer_exited=%d",
    send_exit, netsim_is_started(),
    netsim_is_peer_exited());
  memset(&outev, '\0', sizeof(outev));
  outev.type = NETSIM_OUT_STOP;
  queue_out_put(&g_session.outq, &outev);
  queue_in_clear_session(&g_session.inq);
  netsim_reset_state(NETSIM_SESSION_WAITING);
  netsim_log("session stop queued");
}

void
netsim_shutdown(void)
{
  struct netsim_out_ev outev;

  if (!g_session.running)
    return;
  netsim_log("shutdown requested");
  memset(&outev, '\0', sizeof(outev));
  outev.type = NETSIM_OUT_SHUTDOWN;
  queue_out_put(&g_session.outq, &outev);
  netsim_thread_join(g_session.thr);
  queue_out_destroy(&g_session.outq);
  queue_in_destroy(&g_session.inq);
  g_session.running = false;
  netsim_reset_state(NETSIM_SESSION_WAITING);
  netsim_platform_cleanup();
  netsim_log("shutdown complete");
}

bool
netsim_sync_frame(uint32_t frame, uint8_t local_bits, bool local_freeze,
  uint8_t *remote_bits, bool *remote_freeze, int *remote_lead_ms)
{
  struct netsim_out_ev outev;
  struct netsim_in_ev inev;

  if (!netsim_is_started())
    return (false);
  memset(&outev, '\0', sizeof(outev));
  outev.type = NETSIM_OUT_FRAME;
  outev.frame = frame;
  outev.bits = local_bits;
  outev.nonce = g_session.session_nonce;
  if (local_freeze)
    outev.bits |= NETSIM_CTRL_FREEZE;
  netsim_log("submit local frame frame=%u bits=0x%02x",
    (unsigned int)frame, (unsigned int)outev.bits);
  queue_out_put(&g_session.outq, &outev);
  netsim_log("submitted local frame frame=%u", (unsigned int)frame);
  for (;;) {
    queue_in_get(&g_session.inq, &inev);
    if (inev.type == NETSIM_IN_FRAME && inev.frame == frame) {
      *remote_bits = inev.bits & ~NETSIM_CTRL_FREEZE;
      if (remote_freeze != NULL)
        *remote_freeze = (inev.bits & NETSIM_CTRL_FREEZE) != 0;
      if (remote_lead_ms != NULL)
        *remote_lead_ms = inev.remote_lead_ms;
      return (true);
    }
    if (inev.type == NETSIM_IN_EXIT) {
      netsim_log("sync frame=%u aborted: peer exit", (unsigned int)frame);
      g_session.state = NETSIM_SESSION_PEER_EXITED;
      return (false);
    }
    if (inev.type == NETSIM_IN_ERROR) {
      netsim_log("sync frame=%u aborted: internal error", (unsigned int)frame);
      g_session.state = NETSIM_SESSION_ERROR;
      return (false);
    }
  }
}

int
netsim_local_player(void)
{

  return (g_session.local_player);
}

bool
netsim_session_active(void)
{

  return (netsim_is_started());
}

bool
netsim_peer_exited(void)
{

  return (netsim_is_peer_exited());
}

bool
netsim_remote_start_requested(void)
{

  if (!g_session.running && g_cfg.configured)
    (void)netsim_begin_wait();
  netsim_pump_prestart();
  return (netsim_has_remote_start());
}

#else

bool
netsim_configure(const char *spec)
{

  (void)spec;
  return (false);
}

bool
netsim_configured(void)
{

  return (false);
}

bool
netsim_begin_wait(void)
{

  return (false);
}

bool
netsim_start_session(bool initiated_locally)
{

  (void)initiated_locally;
  return (false);
}

void
netsim_stop_session(bool send_exit)
{

  (void)send_exit;
}

void
netsim_shutdown(void)
{
}

bool
netsim_sync_frame(uint32_t frame, uint8_t local_bits, bool local_freeze,
  uint8_t *remote_bits, bool *remote_freeze, int *remote_lead_ms)
{

  (void)frame;
  (void)local_bits;
  (void)local_freeze;
  (void)remote_bits;
  if (remote_freeze != NULL)
    *remote_freeze = false;
  if (remote_lead_ms != NULL)
    *remote_lead_ms = 0;
  return (false);
}

int
netsim_local_player(void)
{

  return (0);
}

bool
netsim_session_active(void)
{

  return (false);
}

bool
netsim_peer_exited(void)
{

  return (false);
}

bool
netsim_remote_start_requested(void)
{

  return (false);
}

#endif
