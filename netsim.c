/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "def.h"
#include "netsim.h"
#include "netsim_platform.h"

#if NETSIM_PLATFORM_SUPPORTED

#include <stdbool.h>
#include <assert.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "digger_log.h"

#define NETSIM_MAGIC 0x4e53494dU
#define NETSIM_VERSION 1U
#define NETSIM_QUEUE_LEN 32
#define NETSIM_FRAME_WINDOW 32
#define NETSIM_RETRY_LIMIT 100
#define NETSIM_RETRY_MS 10
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
  NETSIM_OUT_SHUTDOWN = 106
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
  uint32_t magic;
  uint16_t version;
  uint8_t type;
  uint8_t player;
  uint32_t tx_seq;
  uint32_t frame;
  uint32_t bits;
  uint64_t nonce;
};

struct netsim_out_ev {
  int type;
  uint32_t frame;
  uint8_t bits;
  uint64_t nonce;
};

struct netsim_in_ev {
  int type;
  uint32_t frame;
  uint8_t bits;
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
  netsim_deadline_t next_tx;
};

struct netsim_config {
  bool configured;
  char local_host[256];
  char local_port[16];
  char remote_host[256];
  char remote_port[16];
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
  struct netsim_out_queue *outq;
  struct netsim_in_queue *inq;
};

static struct netsim_config g_cfg = {.configured = false};
static struct netsim_session g_session = {.running = false,
  .state = NETSIM_SESSION_WAITING, .local_player = 0};
static bool g_debug_ready = false, g_debug_enabled = false;
static atomic_uint_fast64_t g_nonce_seq = 1;

static void netsim_log(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
static void netsim_err(const char *fmt, ...)
  __attribute__((format(printf, 1, 2)));
static void netsim_reset_state(enum netsim_session_state state);
static bool netsim_is_started(void);
static bool netsim_is_error(void);
static bool netsim_is_peer_exited(void);
static bool netsim_is_ready_to_ack(void);
static bool netsim_has_start_ack(void);
static bool netsim_has_remote_start(void);
static void queue_out_clear_session(struct netsim_out_queue *qp);
static void queue_in_clear_session(struct netsim_in_queue *qp);

static void
netsim_debug_init(void)
{

  if (g_debug_ready)
    return;
  g_debug_enabled = getenv("DIGGER_NETSIM_DEBUG") != NULL;
  g_debug_ready = true;
}

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
parse_host_port(const char *spec, char *host, size_t host_len, char *port,
  size_t port_len)
{
  const char *portp;
  size_t len;

  portp = strrchr(spec, ':');
  if (portp == NULL || portp == spec || portp[1] == '\0')
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

static bool
queue_out_peek(struct netsim_out_queue *qp, struct netsim_out_ev *evp)
{
  bool ok = false;

  netsim_mutex_lock(&qp->mutex);
  if (qp->len > 0) {
    *evp = qp->items[qp->head];
    ok = true;
  }
  netsim_mutex_unlock(&qp->mutex);
  return (ok);
}

static bool
queue_out_drop_head(struct netsim_out_queue *qp)
{
  bool ok = false;

  netsim_mutex_lock(&qp->mutex);
  if (qp->len > 0) {
    qp->head = (qp->head + 1) % NETSIM_QUEUE_LEN;
    qp->len--;
    netsim_cond_broadcast(&qp->cond);
    ok = true;
  }
  netsim_mutex_unlock(&qp->mutex);
  return (ok);
}

static bool
queue_out_trytake_control(struct netsim_out_queue *qp,
  struct netsim_out_ev *evp)
{
  struct netsim_out_ev tmp[NETSIM_QUEUE_LEN];
  size_t i, ctrl_ix, out_len;
  bool ok = false;

  netsim_mutex_lock(&qp->mutex);
  ctrl_ix = 0;
  for (i = 0; i < qp->len; i++) {
    struct netsim_out_ev cur;

    cur = qp->items[(qp->head + i) % NETSIM_QUEUE_LEN];
    if (cur.type == NETSIM_OUT_STOP || cur.type == NETSIM_OUT_SHUTDOWN) {
      *evp = cur;
      ctrl_ix = i;
      ok = true;
      break;
    }
  }
  if (ok) {
    out_len = 0;
    for (i = ctrl_ix + 1; i < qp->len; i++)
      tmp[out_len++] = qp->items[(qp->head + i) % NETSIM_QUEUE_LEN];
    if (out_len > 0)
      memcpy(qp->items, tmp, out_len * sizeof(tmp[0]));
    qp->head = 0;
    qp->len = out_len;
    qp->tail = out_len % NETSIM_QUEUE_LEN;
    netsim_cond_broadcast(&qp->cond);
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
sleep_retry_interval(void)
{
  netsim_sleep_ms(NETSIM_RETRY_MS);
}

static bool
timespec_due(netsim_deadline_t deadline)
{
  return (netsim_deadline_due(deadline));
}

static void
pending_schedule(struct pending_tx *ptx)
{

  ptx->next_tx = netsim_deadline_after_ms(NETSIM_RETRY_MS);
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

static netsim_socket_t
open_socket(const struct netsim_config *cfgp)
{
  char local_desc[64], peer_desc[64], errbuf[160];
  netsim_socket_t sock;

  sock = NETSIM_SOCKET_INVALID;
  if (!netsim_socket_open_udp(cfgp->local_host, cfgp->local_port,
        cfgp->remote_host, cfgp->remote_port, &sock, local_desc,
        sizeof(local_desc), peer_desc, sizeof(peer_desc), errbuf,
        sizeof(errbuf))) {
    netsim_err("%s", errbuf);
    return (NETSIM_SOCKET_INVALID);
  }
  netsim_log("socket ready local=%s peer=%s", local_desc, peer_desc);
  return (sock);
}

static int
send_packet(netsim_socket_t sock, int type, int player, uint32_t tx_seq,
  uint32_t frame, uint8_t bits, uint64_t nonce)
{
  struct netsim_pkt pkt;

  memset(&pkt, '\0', sizeof(pkt));
  pkt.magic = htonl(NETSIM_MAGIC);
  pkt.version = htons(NETSIM_VERSION);
  pkt.type = (uint8_t)type;
  pkt.player = (uint8_t)player;
  pkt.tx_seq = htonl(tx_seq);
  pkt.frame = htonl(frame);
  pkt.bits = htonl(bits);
  pkt.nonce = htonll(nonce);
  return (netsim_socket_send(sock, &pkt, sizeof(pkt)));
}

static bool
recv_packet(netsim_socket_t sock, struct netsim_pkt *pktp)
{
  struct netsim_pkt pkt;
  int rlen;
  int err;
  char errbuf[128];

  rlen = netsim_socket_recv(sock, &pkt, sizeof(pkt));
  if (rlen < 0) {
    err = netsim_socket_last_error();
    if (netsim_socket_err_wouldblock(err) || netsim_socket_err_transient(err))
      return (false);
    netsim_log("recv failed: %s",
      netsim_socket_strerror(err, errbuf, sizeof(errbuf)));
    return (false);
  }
  if ((size_t)rlen != sizeof(pkt))
    return (false);
  if (ntohl(pkt.magic) != NETSIM_MAGIC)
    return (false);
  if (ntohs(pkt.version) != NETSIM_VERSION)
    return (false);
  pkt.magic = ntohl(pkt.magic);
  pkt.version = ntohs(pkt.version);
  pkt.tx_seq = ntohl(pkt.tx_seq);
  pkt.frame = ntohl(pkt.frame);
  pkt.bits = ntohl(pkt.bits);
  pkt.nonce = ntohll(pkt.nonce);
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
push_frame(struct netsim_in_queue *inqp, uint32_t frame, uint8_t bits)
{
  struct netsim_in_ev inev;

  memset(&inev, '\0', sizeof(inev));
  inev.type = NETSIM_IN_FRAME;
  inev.frame = frame;
  inev.bits = bits;
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
  ptx->next_tx = 0;
  netsim_log("queue %s seq=%u frame=%u bits=0x%02x", pending_name(type),
    (unsigned int)seq, (unsigned int)frame, (unsigned int)bits);
}

static bool
send_pending(netsim_socket_t sock, struct pending_tx *ptx, int player,
  bool *sentp)
{
  int type, err;
  char errbuf[128];

  *sentp = false;
  if (!ptx->active)
    return (true);
  if (!timespec_due(ptx->next_tx))
    return (true);
  if (ptx->type == NETSIM_PKT_HELLO)
    type = NETSIM_PKT_HELLO;
  else if (ptx->type == NETSIM_OUT_FRAME)
    type = NETSIM_PKT_FRAME;
  else if (ptx->type == NETSIM_OUT_EXIT)
    type = NETSIM_PKT_EXIT;
  else if (ptx->type == NETSIM_OUT_START)
    type = NETSIM_PKT_START;
  else if (ptx->type == NETSIM_OUT_START_ACK)
    type = NETSIM_PKT_START_ACK;
  else
    return (false);
  if (send_packet(sock, type, player, ptx->seq, ptx->frame, ptx->bits,
        ptx->nonce) < 0) {
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
  pending_schedule(ptx);
  return (true);
}

struct netsim_thread_state {
  struct remote_frame_slot slots[NETSIM_FRAME_WINDOW];
  struct pending_tx hello_tx;
  struct pending_tx prev_tx;
  struct pending_tx tx;
  uint64_t local_nonce;
  uint64_t peer_nonce;
  uint64_t session_nonce;
  uint32_t last_delivered;
  uint32_t last_peer_tx_seq;
  uint32_t next_tx_seq;
  netsim_socket_t sock;
  int local_player;
  int remote_player;
  bool peer_nonce_seen;
  bool peer_frame_seen;
  bool exiting;
  bool acking_peer_exit;
  bool session_active;
  bool session_offer_local;
  bool game_start_notified;
  bool prev_tx_valid;
};

static void
thread_clear_session(struct netsim_thread_state *tsp)
{

  tsp->tx.active = false;
  memset(&tsp->tx, '\0', sizeof(tsp->tx));
  memset(&tsp->prev_tx, '\0', sizeof(tsp->prev_tx));
  tsp->prev_tx_valid = false;
  tsp->session_nonce = 0;
  tsp->last_delivered = 0;
  tsp->last_peer_tx_seq = 0;
  tsp->next_tx_seq = 1;
  tsp->session_offer_local = false;
  tsp->session_active = false;
  tsp->exiting = false;
  tsp->acking_peer_exit = false;
  tsp->game_start_notified = false;
  clear_frame_slots(tsp->slots);
  tsp->local_player = 0;
  tsp->remote_player = 1;
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
  tsp->acking_peer_exit = false;
  queue_out_clear_session(outq);
  set_pending(&tsp->tx, NETSIM_OUT_EXIT, frame, 0, tsp->session_nonce,
    tsp->next_tx_seq++);
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
  if (tsp->tx.seq != 0) {
    tsp->prev_tx = tsp->tx;
    tsp->prev_tx_valid = true;
  }
  set_pending(&tsp->tx, type, frame, bits, tsp->session_nonce,
    tsp->next_tx_seq++);
}

static bool
thread_accept_peer_seq(struct netsim_thread_state *tsp, uint32_t peer_seq)
{

  if (peer_seq == 0)
    return (false);
  thread_ack_tx(tsp, peer_seq);
  if (peer_seq <= tsp->last_peer_tx_seq)
    return (false);
  assert(peer_seq == tsp->last_peer_tx_seq + 1);
  tsp->last_peer_tx_seq = peer_seq;
  return (true);
}

static void *
netsim_thread(void *arg)
{
  struct thread_ctx *ctxp;
  struct netsim_thread_state ts;
  struct netsim_out_ev outev;
  struct netsim_pkt pkt;
  bool data_sent;
  bool thread_stop;

  ctxp = (struct thread_ctx *)arg;
  memset(&ts, '\0', sizeof(ts));
  clear_frame_slots(ts.slots);
  thread_stop = false;
  ts.remote_player = 1;
  ts.local_nonce = make_nonce();
  ts.sock = ctxp->sock;
  netsim_log("thread start local=%s:%s remote=%s:%s local_nonce=0x%016llx",
    ctxp->cfg.local_host[0] != '\0' ? ctxp->cfg.local_host : "0.0.0.0",
    ctxp->cfg.local_port, ctxp->cfg.remote_host, ctxp->cfg.remote_port,
    (unsigned long long)ts.local_nonce);

  set_pending(&ts.hello_tx, NETSIM_PKT_HELLO, 0, 0, ts.local_nonce, 0);

  while (!thread_stop) {
    if (!send_pending(ts.sock, &ts.hello_tx, ts.local_player, &data_sent)) {
      push_error(ctxp->inq);
      continue;
    }
    if (!send_pending(ts.sock, &ts.tx, ts.local_player, &data_sent)) {
      if (ts.tx.type == NETSIM_OUT_EXIT) {
        netsim_log("exit send failed permanently, resetting session");
        thread_reset_session(&ts, ctxp->outq);
        continue;
      }
      netsim_log("session data send failed permanently, converting to exit");
      thread_fail_session(&ts, ctxp->inq, ctxp->outq, ts.tx.frame);
      continue;
    }
    if (data_sent && ts.acking_peer_exit && ts.tx.type == NETSIM_OUT_EXIT) {
      netsim_log("peer exit ack sent");
      thread_reset_session(&ts, ctxp->outq);
      continue;
    }
    if (data_sent && ts.tx.type == NETSIM_OUT_START_ACK &&
        !ts.session_active) {
      ts.session_active = true;
      netsim_log("session ready local_player=%d session=0x%016llx",
        ts.local_player + 1, (unsigned long long)ts.session_nonce);
      push_start_ack(ctxp->inq);
    }
    while (recv_packet(ts.sock, &pkt)) {
      if (pkt.type == NETSIM_PKT_HELLO) {
        if (!ts.peer_nonce_seen || pkt.nonce != ts.peer_nonce) {
          if (!ts.peer_nonce_seen) {
            netsim_log("peer present nonce=0x%016llx",
              (unsigned long long)pkt.nonce);
          } else {
            netsim_log("peer restarted nonce 0x%016llx -> 0x%016llx",
              (unsigned long long)ts.peer_nonce,
              (unsigned long long)pkt.nonce);
          }
          ts.peer_nonce = pkt.nonce;
          ts.peer_nonce_seen = true;
        }
        continue;
      }
      if (pkt.type == NETSIM_PKT_START) {
        bool adopt_remote = false;

        if (!ts.session_active) {
          if (ts.session_nonce == 0 || pkt.nonce == ts.session_nonce)
            adopt_remote = true;
          else if (ts.session_offer_local)
            adopt_remote = pkt.nonce < ts.session_nonce;
          else
            adopt_remote = true;
        }
        if (!adopt_remote)
          continue;
        if (pkt.nonce != ts.session_nonce || ts.session_offer_local) {
          thread_begin_session(&ts, pkt.nonce, 1, 0, false);
          netsim_log("recv start seq=%u session=0x%016llx local_player=%d",
            (unsigned int)pkt.tx_seq,
            (unsigned long long)ts.session_nonce, ts.local_player + 1);
          push_start(ctxp->inq, ts.local_player, ts.session_nonce);
        }
        if (ts.tx.active && ts.tx.type == NETSIM_OUT_START_ACK &&
            ts.tx.seq == pkt.tx_seq)
          ts.tx.active = false;
        if (!thread_accept_peer_seq(&ts, pkt.tx_seq))
          continue;
        if (!ts.game_start_notified) {
          ts.game_start_notified = true;
          netsim_log("peer start request");
          push_gamestart(ctxp->inq);
        }
        continue;
      }
      if (pkt.type == NETSIM_PKT_START_ACK) {
        if (!ts.session_offer_local || ts.session_nonce == 0 ||
            pkt.nonce != ts.session_nonce)
          continue;
        if (ts.tx.active && ts.tx.type == NETSIM_OUT_START &&
            ts.tx.seq == pkt.tx_seq)
          ts.tx.active = false;
        if (!thread_accept_peer_seq(&ts, pkt.tx_seq))
          continue;
        if (!ts.session_active) {
          ts.session_active = true;
          netsim_log("recv start-ack seq=%u session=0x%016llx local_player=%d",
            (unsigned int)pkt.tx_seq,
            (unsigned long long)ts.session_nonce, ts.local_player + 1);
          push_start_ack(ctxp->inq);
        }
        continue;
      }
      if (!ts.session_active || ts.session_nonce == 0 ||
          pkt.nonce != ts.session_nonce)
        continue;
      if ((int)pkt.player != ts.remote_player)
        continue;
      if (!thread_accept_peer_seq(&ts, pkt.tx_seq))
        continue;
      if (pkt.type == NETSIM_PKT_EXIT) {
        ts.peer_frame_seen = true;
        netsim_log("recv exit seq=%u", (unsigned int)pkt.tx_seq);
        push_exit(ctxp->inq);
        if (ts.tx.active && ts.tx.type == NETSIM_OUT_EXIT)
          ts.tx.active = false;
        if (ts.exiting) {
          thread_reset_session(&ts, ctxp->outq);
          continue;
        }
        queue_out_clear_session(ctxp->outq);
        ts.prev_tx_valid = false;
        ts.acking_peer_exit = true;
        ts.exiting = false;
        thread_set_tx(&ts, NETSIM_OUT_EXIT, pkt.frame, 0);
        continue;
      }
      if (pkt.type != NETSIM_PKT_FRAME)
        continue;
      ts.peer_frame_seen = true;
      if (ts.tx.active && ts.tx.type == NETSIM_OUT_FRAME &&
          ts.tx.seq == pkt.tx_seq)
        ts.tx.matched = true;
      if (pkt.frame <= ts.last_delivered)
        continue;
      if (pkt.frame > ts.last_delivered + NETSIM_FRAME_WINDOW) {
        netsim_log("frame window overflow frame=%u last_delivered=%u",
          (unsigned int)pkt.frame, (unsigned int)ts.last_delivered);
        thread_fail_session(&ts, ctxp->inq, ctxp->outq, pkt.frame);
        break;
      }
      if (!ts.slots[pkt.frame % NETSIM_FRAME_WINDOW].valid) {
        ts.slots[pkt.frame % NETSIM_FRAME_WINDOW].valid = true;
        ts.slots[pkt.frame % NETSIM_FRAME_WINDOW].frame = pkt.frame;
        ts.slots[pkt.frame % NETSIM_FRAME_WINDOW].bits = (uint8_t)pkt.bits;
      }
      while (ts.slots[(ts.last_delivered + 1) % NETSIM_FRAME_WINDOW].valid &&
             ts.slots[(ts.last_delivered + 1) % NETSIM_FRAME_WINDOW].frame ==
             ts.last_delivered + 1) {
        struct remote_frame_slot *fsp;

        fsp = &ts.slots[(ts.last_delivered + 1) % NETSIM_FRAME_WINDOW];
        push_frame(ctxp->inq, fsp->frame, fsp->bits);
        fsp->valid = false;
        ts.last_delivered++;
      }
    }
    if (thread_stop)
      break;
    if (queue_out_trytake_control(ctxp->outq, &outev)) {
      if (outev.type == NETSIM_OUT_SHUTDOWN) {
        netsim_log("shutdown signal received");
        ts.hello_tx.active = false;
        ts.tx.active = false;
        thread_stop = true;
        continue;
      }
      if (ts.exiting)
        continue;
      netsim_log("stop requested");
      thread_reset_session(&ts, ctxp->outq);
      continue;
    }
    if (queue_out_peek(ctxp->outq, &outev)) {
      if (outev.type == NETSIM_OUT_START) {
        if (ts.tx.active)
          goto sleep_only;
        (void)queue_out_drop_head(ctxp->outq);
        thread_begin_session(&ts, outev.nonce, 0, 1, true);
        netsim_log("local start offer session=0x%016llx local_player=%d",
          (unsigned long long)ts.session_nonce, ts.local_player + 1);
        push_start(ctxp->inq, ts.local_player, ts.session_nonce);
        thread_set_tx(&ts, outev.type, outev.frame, outev.bits);
        continue;
      }
      if (outev.type == NETSIM_OUT_START_ACK) {
        if (ts.session_nonce == 0 || outev.nonce != ts.session_nonce) {
          (void)queue_out_drop_head(ctxp->outq);
          continue;
        }
        if (ts.tx.active)
          goto sleep_only;
        (void)queue_out_drop_head(ctxp->outq);
        thread_set_tx(&ts, outev.type, outev.frame, outev.bits);
        continue;
      }
      if (!ts.session_active) {
        if (outev.type == NETSIM_OUT_FRAME || outev.type == NETSIM_OUT_EXIT) {
          netsim_log("drop stale %s frame=%u while session inactive",
            pending_name(outev.type), (unsigned int)outev.frame);
          (void)queue_out_drop_head(ctxp->outq);
          continue;
        }
        goto sleep_only;
      }
      if (outev.type == NETSIM_OUT_EXIT) {
        if (ts.acking_peer_exit) {
          (void)queue_out_drop_head(ctxp->outq);
          continue;
        }
        (void)queue_out_drop_head(ctxp->outq);
        netsim_log("local exit queued");
        ts.prev_tx_valid = false;
        ts.tx.active = false;
        thread_set_tx(&ts, outev.type, outev.frame, outev.bits);
        ts.exiting = true;
        continue;
      }
      if (outev.type == NETSIM_OUT_FRAME) {
        if (ts.tx.active && !(ts.tx.type == NETSIM_OUT_FRAME && ts.tx.matched))
          goto sleep_only;
        (void)queue_out_drop_head(ctxp->outq);
        thread_set_tx(&ts, outev.type, outev.frame, outev.bits);
        continue;
      }
      assert(false && "unexpected out queue event at worker head");
      continue;
    }
sleep_only:
    if (ts.exiting && !ts.tx.active) {
      thread_reset_session(&ts, ctxp->outq);
      continue;
    }
    sleep_retry_interval();
  }
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
  const char *dashp;
  const char *closep;
  char local_spec[272], remote_spec[272];
  size_t len;

  memset(&g_cfg, '\0', sizeof(g_cfg));
  if (spec[0] == '[') {
    closep = strchr(spec, ']');
    if (closep == NULL || closep[1] != '-')
      return (false);
    len = (size_t)(closep - (spec + 1));
    if (len == 0 || len >= sizeof(local_spec))
      return (false);
    memcpy(local_spec, spec + 1, len);
    local_spec[len] = '\0';
    if (!parse_host_port(local_spec, g_cfg.local_host, sizeof(g_cfg.local_host),
          g_cfg.local_port, sizeof(g_cfg.local_port)))
      return (false);
    if (!parse_host_port(closep + 2, g_cfg.remote_host, sizeof(g_cfg.remote_host),
          g_cfg.remote_port, sizeof(g_cfg.remote_port)))
      return (false);
  } else {
    dashp = strchr(spec, '-');
    if (dashp != NULL) {
      len = (size_t)(dashp - spec);
      if (len == 0 || len >= sizeof(local_spec) || strlen(dashp + 1) >= sizeof(remote_spec))
        return (false);
      memcpy(local_spec, spec, len);
      local_spec[len] = '\0';
      strcpy(remote_spec, dashp + 1);
      if (!parse_host_port(local_spec, g_cfg.local_host, sizeof(g_cfg.local_host),
            g_cfg.local_port, sizeof(g_cfg.local_port)))
        return (false);
      if (!parse_host_port(remote_spec, g_cfg.remote_host, sizeof(g_cfg.remote_host),
            g_cfg.remote_port, sizeof(g_cfg.remote_port)))
        return (false);
    } else {
      if (!parse_host_port(spec, g_cfg.remote_host, sizeof(g_cfg.remote_host),
            g_cfg.remote_port, sizeof(g_cfg.remote_port)))
        return (false);
      strcpy(g_cfg.local_port, g_cfg.remote_port);
    }
  }
  if (g_cfg.local_port[0] == '\0')
    strcpy(g_cfg.local_port, g_cfg.remote_port);
  g_cfg.configured = true;
  netsim_log("configured local=%s:%s remote=%s:%s",
    g_cfg.local_host[0] != '\0' ? g_cfg.local_host : "0.0.0.0",
    g_cfg.local_port, g_cfg.remote_host, g_cfg.remote_port);
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

  if (!g_cfg.configured || g_session.running)
    return (g_session.running);
  if (!netsim_platform_init())
    return (false);
  netsim_log("begin_wait requested local=%s:%s remote=%s:%s",
    g_cfg.local_host[0] != '\0' ? g_cfg.local_host : "0.0.0.0",
    g_cfg.local_port, g_cfg.remote_host, g_cfg.remote_port);
  queue_out_init(&g_session.outq);
  queue_in_init(&g_session.inq);
  g_session.running = true;
  netsim_reset_state(NETSIM_SESSION_WAITING);
  sock = open_socket(&g_cfg);
  if (sock == NETSIM_SOCKET_INVALID)
    goto fail;
  ctxp = calloc(1, sizeof(*ctxp));
  if (ctxp == NULL) {
    netsim_socket_close(sock);
    goto fail;
  }
  ctxp->cfg = g_cfg;
  ctxp->sock = sock;
  ctxp->outq = &g_session.outq;
  ctxp->inq = &g_session.inq;
  if (!netsim_thread_create(&g_session.thr, netsim_thread, ctxp)) {
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
  netsim_stop_session(false);
  return (false);
}

void
netsim_stop_session(bool send_exit)
{
  struct netsim_out_ev outev;

  if (!g_session.running)
    return;
  netsim_log("stop_session send_exit=%d started=%d peer_exited=%d",
    send_exit, netsim_is_started(),
    netsim_is_peer_exited());
  if (!send_exit && netsim_is_error()) {
    queue_in_clear_session(&g_session.inq);
    netsim_reset_state(NETSIM_SESSION_WAITING);
    netsim_log("session stop noted after internal error");
    return;
  }
  if (!send_exit && netsim_is_peer_exited()) {
    queue_in_clear_session(&g_session.inq);
    netsim_reset_state(NETSIM_SESSION_WAITING);
    netsim_log("session stop noted after peer exit");
    return;
  }
  memset(&outev, '\0', sizeof(outev));
  outev.type = send_exit && netsim_is_started() ? NETSIM_OUT_EXIT : NETSIM_OUT_STOP;
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
  uint8_t *remote_bits, bool *remote_freeze)
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
  queue_out_put(&g_session.outq, &outev);
  for (;;) {
    queue_in_get(&g_session.inq, &inev);
    if (inev.type == NETSIM_IN_FRAME && inev.frame == frame) {
      *remote_bits = inev.bits & ~NETSIM_CTRL_FREEZE;
      if (remote_freeze != NULL)
        *remote_freeze = (inev.bits & NETSIM_CTRL_FREEZE) != 0;
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
  uint8_t *remote_bits, bool *remote_freeze)
{

  (void)frame;
  (void)local_bits;
  (void)local_freeze;
  (void)remote_bits;
  if (remote_freeze != NULL)
    *remote_freeze = false;
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
