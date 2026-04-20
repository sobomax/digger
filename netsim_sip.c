/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim_sip.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netsim_sip_sdp.h"
#include "digger_log.h"

#if NETSIM_PLATFORM_SUPPORTED

#include "microsippy/platforms/posix/usipy_tm_uac.h"
#include "microsippy/src/public/usipy_sip_dialog.h"
#include "microsippy/src/public/usipy_sip_msg.h"
#include "microsippy/src/public/usipy_sip_tm.h"
#include "microsippy/src/public/usipy_sip_ua.h"
#include "microsippy/src/public/usipy_sip_response_utils.h"
#include "microsippy/src/public/usipy_sip_tm_utils.h"
#include "microsippy/src/public/usipy_sip_ua_utils.h"
#include "microsippy/src/public/usipy_str.h"
#include "microsippy/src/usipy_sip_hdr.h"
#include "microsippy/src/usipy_sip_hdr_nameaddr.h"
#include "microsippy/src/usipy_sip_method_db.h"
#include "microsippy/src/usipy_tvpair.h"
#include "microsippy/src/usipy_sip_uri.h"
#include "microsippy/src/usipy_sip_res.h"

#define NETSIM_SIP_EVENTS 16
#define NETSIM_SIP_MAX_TX 32

struct netsim_sip_call_req {
  bool valid;
  uint64_t session_nonce;
  uint32_t local_stream_ssrc;
  int local_player;
  char sdp_buf[NETSIM_SIP_SDP_BUFSIZE];
  struct usipy_str sdp;
};

struct netsim_sip_peer_binding {
  bool valid;
  uint64_t expires_at_ms;
  char contact_uri_buf[NETSIM_SIP_CONTACT_BUFSIZE];
  struct usipy_str contact_uri;
  char target_host[NETSIM_SIP_HOST_BUFSIZE];
  struct usipy_sip_tm_addr target;
};

struct netsim_sip {
  netsim_socket_t sock;
  struct netsim_sip_config cfg;
  struct usipy_tm_uac_production_ids production_ids;
  struct usipy_sip_tm *tm;
  struct usipy_sip_ua *ua;
  struct usipy_str username;
  struct usipy_str password;
  struct usipy_str qop;
  struct usipy_str request_uri;
  struct usipy_str call_id;
  struct usipy_sip_tm_addr server_addr;
  char local_host[NETSIM_SIP_HOST_BUFSIZE];
  char local_port[NETSIM_SIP_PORT_BUFSIZE];
  char incoming_local_host[NETSIM_SIP_HOST_BUFSIZE];
  char server_uri_host[NETSIM_SIP_HOST_BUFSIZE];
  char request_uri_buf[NETSIM_SIP_URI_BUFSIZE];
  uint32_t next_invite_cseq;
  size_t invite_index;
  struct usipy_sip_register_state reg;
  bool ua_reset_needed;
  bool stop;
  bool error;
  struct netsim_sip_call_req pending_call;
  struct netsim_sip_call_req answered_call;
  struct netsim_sip_peer_binding peer_binding;
  struct netsim_sip_session pending_remote;
  struct netsim_sip_session current;
  struct netsim_sip_event events[NETSIM_SIP_EVENTS];
  size_t ev_head;
  size_t ev_tail;
  size_t ev_len;
};

static const struct usipy_sip_status netsim_sip_res_bad_sdp = {
  .code = 488,
  .reason_phrase = USIPY_2STR("Not Acceptable Here"),
};

static const struct usipy_sip_status netsim_sip_res_not_found = {
  .code = 404,
  .reason_phrase = USIPY_2STR("Not Found"),
};

static const struct usipy_sip_status netsim_sip_res_forbidden = {
  .code = 403,
  .reason_phrase = USIPY_2STR("Forbidden"),
};

static uint64_t
netsim_sip_now_ms(void)
{

  return (netsim_monotonic_ns() / 1000000ULL);
}

static bool
local_registrar_mode(const struct netsim_sip *sp)
{

  return (sp->cfg.server_host[0] == '\0');
}

static void outgoing_response(void *, size_t, const struct usipy_sip_tm_tx *,
  const struct usipy_msg *);
static void outgoing_timeout(void *, size_t, const struct usipy_sip_tm_tx *,
  enum usipy_sip_tm_uac_timeout_id);
static void incoming_request(void *, const struct usipy_sip_tm_handle_incoming_in *,
  const struct usipy_msg *);
static void ua_emit(void *, const struct usipy_sip_ua_emit *);
static int start_register(struct netsim_sip *);

static void sip_log(const char *fmt, ...)
  __attribute__ ((format (printf, 1, 2)));

static void
sip_log(const char *fmt, ...)
{
  va_list ap;
  char buf[NETSIM_SIP_LOG_BUFSIZE];

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  digger_log_printf("netsim-sip: %s\n", buf);
}

static void
event_push(struct netsim_sip *sp, enum netsim_sip_event_type type,
  const struct netsim_sip_session *sessionp)
{
  struct netsim_sip_event *evp;

  if (sp->ev_len == NETSIM_SIP_EVENTS)
    return;
  evp = &sp->events[sp->ev_tail];
  memset(evp, '\0', sizeof(*evp));
  evp->type = type;
  if (sessionp != NULL)
    evp->session = *sessionp;
  sp->ev_tail = (sp->ev_tail + 1) % NETSIM_SIP_EVENTS;
  sp->ev_len++;
}

static bool
event_pop(struct netsim_sip *sp, struct netsim_sip_event *evp)
{

  if (sp->ev_len == 0)
    return (false);
  *evp = sp->events[sp->ev_head];
  sp->ev_head = (sp->ev_head + 1) % NETSIM_SIP_EVENTS;
  sp->ev_len--;
  return (true);
}

static void
session_clear(struct netsim_sip_session *sp)
{

  memset(sp, '\0', sizeof(*sp));
}

static uint32_t
make_local_stream_ssrc(const struct netsim_sip *sp, uint64_t session_nonce)
{
  const unsigned char *cp;
  uint32_t ssrc;

  ssrc = (uint32_t)(session_nonce ^ (session_nonce >> 32));
  for (cp = (const unsigned char *)sp->cfg.username; *cp != '\0'; cp++)
    ssrc = (ssrc * 16777619u) ^ *cp;
  ssrc ^= 0x5354524dU;
  if (ssrc == 0)
    ssrc = 0x4e53494dU;
  return (ssrc);
}

static void
peer_binding_clear(struct netsim_sip_peer_binding *bp)
{

  memset(bp, '\0', sizeof(*bp));
}

static bool
tm_addr_to_sockaddr(const struct usipy_sip_tm_addr *taddrp, netsim_sockaddr_t *addrp)
{
  char portbuf[NETSIM_SIP_PORT_BUFSIZE];
  char errbuf[NETSIM_SIP_ERR_BUFSIZE];

  snprintf(portbuf, sizeof(portbuf), "%u", (unsigned int)taddrp->port);
  return (netsim_sockaddr_resolve_udp(taddrp->host.s.ro, portbuf, addrp, errbuf,
    sizeof(errbuf)));
}

static bool
sockaddr_to_tm_addr(const netsim_sockaddr_t *addrp, struct usipy_sip_tm_addr *outp)
{
  const struct sockaddr_in *sin;
  char hostbuf[NETSIM_SIP_HOST_BUFSIZE];

  if (addrp == NULL || addrp->len < sizeof(*sin))
    return (false);
  sin = (const struct sockaddr_in *)&addrp->ss;
  if (sin->sin_family != AF_INET)
    return (false);
  if (inet_ntop(AF_INET, &sin->sin_addr, hostbuf, sizeof(hostbuf)) == NULL)
    return (false);
  outp->af = AF_INET;
  outp->port = ntohs(sin->sin_port);
  outp->transport = USIPY_SIP_TM_TRANSPORT_UDP;
  outp->host = (struct usipy_str){.s.ro = strdup(hostbuf), .l = strlen(hostbuf)};
  return (outp->host.s.ro != NULL);
}

static void
tm_addr_cleanup(struct usipy_sip_tm_addr *addrp)
{

  if (addrp->host.s.ro != NULL)
    free((void *)addrp->host.s.ro);
  memset(addrp, '\0', sizeof(*addrp));
}

static int
socket_send_to(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_sip_tm_outbound *outp)
{
  struct netsim_sip *sp = arg;
  netsim_sockaddr_t target;
  char hostbuf[NETSIM_SIP_HOST_BUFSIZE];
  char portbuf[NETSIM_SIP_PORT_BUFSIZE];
  char errbuf[NETSIM_SIP_ERR_BUFSIZE];
  int sent;

  (void)tx_index;
  (void)txp;
  if (outp->target.host.l == 0 || outp->target.host.l >= sizeof(hostbuf)) {
    sip_log("send target host invalid len=%zu", outp->target.host.l);
    return (-1);
  }
  memcpy(hostbuf, outp->target.host.s.ro, outp->target.host.l);
  hostbuf[outp->target.host.l] = '\0';
  snprintf(portbuf, sizeof(portbuf), "%u", (unsigned int)outp->target.port);
  if (!netsim_sockaddr_resolve_udp(hostbuf, portbuf, &target, errbuf,
        sizeof(errbuf))) {
    sip_log("send resolve failed host='%.*s' port=%u",
      (int)outp->target.host.l, outp->target.host.s.ro,
      (unsigned int)outp->target.port);
    return (-1);
  }
  sent = netsim_socket_sendto(sp->sock, outp->raw.s.ro, outp->raw.l, &target);
  if (sent == (int)outp->raw.l) {
    sip_log("sent %zu bytes to %.*s:%u", outp->raw.l,
      (int)outp->target.host.l, outp->target.host.s.ro,
      (unsigned int)outp->target.port);
  } else {
    sip_log("send failed to %.*s:%u sent=%d expected=%zu",
      (int)outp->target.host.l, outp->target.host.s.ro,
      (unsigned int)outp->target.port, sent, outp->raw.l);
  }
  return (sent == (int)outp->raw.l ? 0 : -1);
}

static int
run_tm_now(struct netsim_sip *sp, uint64_t now_ms)
{
  struct usipy_sip_tm_run_in rin = {
    .now_ms = now_ms,
    .tm = sp->tm,
    .send_to = socket_send_to,
    .send_to_arg = sp,
  };
  struct usipy_sip_tm_run_out rout;

  return (usipy_sip_tm_run(&rin, &rout));
}

static int
send_stateless_response(void *arg, const void *buf, size_t len)
{
  struct {
    struct netsim_sip *sp;
    const netsim_sockaddr_t *peerp;
  } *sarg = arg;
  int sent;

  sent = netsim_socket_sendto(sarg->sp->sock, buf, len, sarg->peerp);
  return (sent == (int)len ? 0 : -1);
}

static int
build_call_sdp(struct netsim_sip_call_req *crp, const char *host, const char *port)
{
  struct netsim_sdp_desc desc;

  desc.session_nonce = crp->session_nonce;
  desc.stream_ssrc = crp->local_stream_ssrc;
  desc.player = crp->local_player;
  return (netsim_sip_sdp_build(crp->sdp_buf, sizeof(crp->sdp_buf), host, port,
    &desc) ? 0 : -1);
}

static bool
format_tm_port(uint16_t port, char *buf, size_t len)
{
  int blen;

  blen = snprintf(buf, len, "%u", (unsigned int)port);
  return (blen > 0 && (size_t)blen < len);
}

static bool
resolve_local_host_for_target(const struct netsim_sip *sp,
  const struct usipy_sip_tm_addr *targetp, char *hostbuf, size_t hostbuf_len)
{
  char peer_host[NETSIM_SIP_HOST_BUFSIZE];
  char portbuf[NETSIM_SIP_PORT_BUFSIZE];
  char errbuf[NETSIM_SIP_ERR_BUFSIZE];

  if (sp->local_host[0] != '\0' && strcmp(sp->local_host, "0.0.0.0") != 0) {
    if (strlen(sp->local_host) >= hostbuf_len)
      return (false);
    strcpy(hostbuf, sp->local_host);
    return (true);
  }
  if (targetp == NULL || targetp->host.l == 0 || targetp->host.l >= sizeof(peer_host) ||
      !format_tm_port(targetp->port, portbuf, sizeof(portbuf)))
    return (false);
  memcpy(peer_host, targetp->host.s.ro, targetp->host.l);
  peer_host[targetp->host.l] = '\0';
  if (!netsim_sockaddr_local_for_peer(peer_host, portbuf, hostbuf, hostbuf_len,
        errbuf, sizeof(errbuf))) {
    sip_log("local host resolve failed for %s:%s: %s", peer_host, portbuf, errbuf);
    return (false);
  }
  return (true);
}

static bool
resolve_local_tm_addr_for_peer(const struct netsim_sip *sp,
  const struct usipy_sip_tm_addr *peerp, uint16_t local_port,
  struct usipy_sip_tm_addr *localp, char *hostbuf, size_t hostbuf_len)
{
  if (!resolve_local_host_for_target(sp, peerp, hostbuf, hostbuf_len))
    return (false);
  *localp = (struct usipy_sip_tm_addr){
    .af = AF_INET,
    .port = local_port,
    .transport = USIPY_SIP_TM_TRANSPORT_UDP,
    .host = {
      .s.ro = hostbuf,
      .l = strlen(hostbuf),
    },
  };
  return (true);
}

static int
extract_register_binding(const struct usipy_msg *msg, const struct usipy_str *peer_userp,
  struct usipy_str *contact_urip, char *target_host, size_t target_host_len,
  uint16_t *target_portp, unsigned int *expiresp)
{
  struct usipy_msg *cmsg = (struct usipy_msg *)msg;
  struct usipy_sip_hdr_match *contact_hdrs;
  struct usipy_sip_hdr_match *from_hdrs;
  unsigned int expires;

  if (msg == NULL || peer_userp == NULL || contact_urip == NULL ||
      target_host == NULL || target_portp == NULL || expiresp == NULL)
    return (USIPY_SIP_TM_ERR_INVAL);
  contact_hdrs = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(cmsg->nhdrs));
  from_hdrs = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
  *contact_hdrs = (struct usipy_sip_hdr_match){.hdrslen = cmsg->nhdrs};
  *from_hdrs = (struct usipy_sip_hdr_match){.hdrslen = 1};
  if (usipy_sip_msg_parse_hdrs_get(cmsg, USIPY_HFT_MASK(USIPY_HF_CONTACT), 0,
        contact_hdrs) != 0)
    return (USIPY_SIP_TM_ERR_PARSE);
  if (usipy_sip_msg_parse_hdrs_get(cmsg, USIPY_HFT_MASK(USIPY_HF_FROM), 1,
        from_hdrs) != 0)
    return (USIPY_SIP_TM_ERR_PARSE);
  if (from_hdrs->nhdrs == 0 || contact_hdrs->nhdrs == 0)
    return (USIPY_SIP_TM_ERR_BADMSG);
  {
    const struct usipy_sip_hdr_nameaddr *fromp = from_hdrs->hdrsp[0]->parsed.from;
    struct usipy_sip_uri *from_uri;

    if (fromp == NULL)
      return (USIPY_SIP_TM_ERR_BADMSG);
    from_uri = usipy_sip_uri_parse(&cmsg->heap, &fromp->addr_spec);
    if (from_uri == NULL || !usipy_str_eq(&from_uri->user, peer_userp))
      return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  }
  if (usipy_tm_uac_extract_register_expires(msg, peer_userp, &expires) != 0)
    return (USIPY_SIP_TM_ERR_BADMSG);
  for (size_t i = 0; i < contact_hdrs->nhdrs; i++) {
    const struct usipy_sip_hdr_nameaddr *nap = contact_hdrs->hdrsp[i]->parsed.contact;
    struct usipy_sip_uri *urip;

    if (nap == NULL || nap->addr_spec.l == 0)
      continue;
    urip = usipy_sip_uri_parse(&cmsg->heap, &nap->addr_spec);
    if (urip == NULL || !usipy_str_eq(&urip->user, peer_userp))
      continue;
    if (urip->host.l == 0 || urip->host.l >= target_host_len)
      return (USIPY_SIP_TM_ERR_BADMSG);
    memcpy(target_host, urip->host.s.ro, urip->host.l);
    target_host[urip->host.l] = '\0';
    *target_portp = (uint16_t)(urip->port != 0 ? urip->port : 5060);
    *contact_urip = nap->addr_spec;
    *expiresp = expires;
    return (USIPY_SIP_TM_OK);
  }
  return (USIPY_SIP_TM_ERR_NOT_FOUND);
}

static int
send_register_ok(struct netsim_sip *sp, const struct usipy_sip_tm_handle_incoming_in *hin,
  const struct usipy_msg *msg, const struct usipy_str *contact_urip,
  unsigned int expires)
{
  struct usipy_sip_tm_new_uas_tr_params tp = {
    .request = msg,
    .timers = hin->timers,
    .peer = hin->peer,
    .local = hin->local,
  };
  struct usipy_sip_tm_uas_response_params rp = {
    .status = usipy_sip_res_ok,
  };
  struct usipy_sip_tm_extra_header eh = {
    .hf_type = USIPY_HF_CONTACT,
    .value_kind = USIPY_SIP_TM_EH_RAW,
  };
  char contact_raw[NETSIM_SIP_CONTACT_BUFSIZE];
  size_t tx_index;
  int blen;
  int rval;

  blen = snprintf(contact_raw, sizeof(contact_raw), "<%.*s>;expires=%u",
    (int)contact_urip->l, contact_urip->s.ro, expires);
  if (blen < 0 || (size_t)blen >= sizeof(contact_raw))
    return (USIPY_SIP_TM_ERR_INVAL);
  eh.value = (struct usipy_str){.s.ro = contact_raw, .l = (size_t)blen};
  rp.extra_headers = &eh;
  rp.nextra_headers = 1;
  rval = usipy_sip_tm_new_uas_tr(sp->tm, &tp, &tx_index);
  if (rval != USIPY_SIP_TM_OK)
    return (rval);
  return (usipy_sip_tm_send_uas_response(sp->tm, tx_index, &rp));
}

static int
store_peer_registration(struct netsim_sip *sp,
  const struct usipy_sip_tm_handle_incoming_in *hin, const struct usipy_str *contact_urip,
  const char *target_host, uint16_t target_port, unsigned int expires)
{
  peer_binding_clear(&sp->peer_binding);
  if (expires == 0)
    return (USIPY_SIP_TM_OK);
  if (contact_urip->l >= sizeof(sp->peer_binding.contact_uri_buf))
    return (USIPY_SIP_TM_ERR_NOSPC);
  memcpy(sp->peer_binding.contact_uri_buf, contact_urip->s.ro, contact_urip->l);
  sp->peer_binding.contact_uri_buf[contact_urip->l] = '\0';
  strcpy(sp->peer_binding.target_host, target_host);
  sp->peer_binding.contact_uri = (struct usipy_str){
    .s.ro = sp->peer_binding.contact_uri_buf,
    .l = contact_urip->l,
  };
  sp->peer_binding.target.af = AF_INET;
  sp->peer_binding.target.port = target_port;
  sp->peer_binding.target.transport = USIPY_SIP_TM_TRANSPORT_UDP;
  sp->peer_binding.target.host = (struct usipy_str){
    .s.ro = sp->peer_binding.target_host,
    .l = strlen(sp->peer_binding.target_host),
  };
  sp->peer_binding.expires_at_ms = hin->now_ms + ((uint64_t)expires * 1000u);
  sp->peer_binding.valid = true;
  sip_log("stored peer registration user=%s contact=%s:%u expires=%u",
    sp->cfg.peer_user, target_host, (unsigned int)target_port, expires);
  return (USIPY_SIP_TM_OK);
}

static int
init_tm_ua(struct netsim_sip *sp, char *errbuf, size_t errbuf_len)
{
  struct usipy_sip_tm_ctor_params tm_ctorp = {
    .sock = (int)sp->sock,
    .transport = USIPY_SIP_TM_TRANSPORT_UDP,
    .max_transactions = NETSIM_SIP_MAX_TX,
    .callbacks = {
      .arg = sp,
      .incoming_request = incoming_request,
    },
    .id_policy = {
      .arg = &sp->production_ids,
      .cb = usipy_tm_uac_production_id_policy,
    },
  };

  sp->tm = usipy_sip_tm_ctor(&tm_ctorp);
  if (sp->tm == NULL) {
    snprintf(errbuf, errbuf_len, "cannot initialize SIP TM");
    return (USIPY_SIP_TM_ERR_NOMEM);
  }
  if (usipy_sip_ua_reset(&sp->ua, &(const struct usipy_sip_ua_ctor_params){
        .tm = sp->tm,
        .emit = ua_emit,
        .emit_arg = sp,
      }) != USIPY_SIP_TM_OK) {
    usipy_sip_tm_dtor(sp->tm);
    sp->tm = NULL;
    snprintf(errbuf, errbuf_len, "cannot initialize SIP UA");
    return (USIPY_SIP_TM_ERR_NOMEM);
  }
  if (!local_registrar_mode(sp) && start_register(sp) != USIPY_SIP_TM_OK) {
    usipy_sip_ua_dtor(sp->ua);
    usipy_sip_tm_dtor(sp->tm);
    sp->ua = NULL;
    sp->tm = NULL;
    snprintf(errbuf, errbuf_len, "cannot start SIP registration");
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  }
  return (USIPY_SIP_TM_OK);
}

static int
make_media_session(struct netsim_sip_session *sessionp,
  const struct netsim_sip_call_req *localp, const struct netsim_sdp_desc *remotep)
{
  char errbuf[NETSIM_SIP_ERR_BUFSIZE];

  memset(sessionp, '\0', sizeof(*sessionp));
  sessionp->valid = true;
  sessionp->session_nonce = localp->session_nonce;
  sessionp->local_stream_ssrc = localp->local_stream_ssrc;
  sessionp->peer_stream_ssrc = remotep->stream_ssrc;
  sessionp->local_player = localp->local_player;
  sessionp->remote_player = 1 - localp->local_player;
  return (netsim_sockaddr_resolve_udp(remotep->conn_host, remotep->media_port,
    &sessionp->media_addr, errbuf, sizeof(errbuf)) ? 0 : -1);
}

static void
answer_incoming_invite(struct netsim_sip *sp, const struct usipy_msg *msg,
  const struct netsim_sdp_desc *remote_descp)
{
  struct usipy_sip_ua_event ev = {0};
  struct netsim_sip_call_req *crp;
  size_t tx_index;
  int rval;

  crp = &sp->answered_call;
  memset(crp, '\0', sizeof(*crp));
  crp->valid = true;
  crp->session_nonce = remote_descp->session_nonce;
  crp->local_stream_ssrc = sp->pending_remote.local_stream_ssrc;
  crp->local_player = sp->pending_remote.local_player;
  if (build_call_sdp(crp,
        sp->incoming_local_host[0] != '\0' ? sp->incoming_local_host : sp->local_host,
        sp->local_port) != 0) {
    ev.type = USIPY_SIP_UA_EVENT_CONNECT;
    ev.data.response.status = netsim_sip_res_bad_sdp;
    rval = usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
    sip_log("answer INVITE with 488 rval=%d tx=%zu", rval, tx_index);
    if (rval == USIPY_SIP_TM_OK)
      (void)run_tm_now(sp, netsim_sip_now_ms());
    return;
  }
  crp->sdp = (struct usipy_str){.s.ro = crp->sdp_buf, .l = strlen(crp->sdp_buf)};
  ev.type = USIPY_SIP_UA_EVENT_CONNECT;
  ev.data.response.status = usipy_sip_res_ok;
  ev.data.response.content_type = (struct usipy_str)USIPY_2STR("application/sdp");
  ev.data.response.body = crp->sdp;
  rval = usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
  sip_log("answer INVITE with 200 rval=%d tx=%zu body=%zu",
    rval, tx_index, crp->sdp.l);
  if (rval == USIPY_SIP_TM_OK) {
    rval = run_tm_now(sp, netsim_sip_now_ms());
    sip_log("post-answer run_tm rval=%d", rval);
  }
  (void)msg;
}

static int
apply_ua_reset(struct netsim_sip *sp)
{

  if (!sp->ua_reset_needed)
    return (USIPY_SIP_TM_OK);
  if (usipy_sip_ua_reset(&sp->ua, &(const struct usipy_sip_ua_ctor_params){
        .tm = sp->tm,
        .emit = ua_emit,
        .emit_arg = sp,
      }) != USIPY_SIP_TM_OK)
    return (USIPY_SIP_TM_ERR_NOMEM);
  sp->ua_reset_needed = false;
  return (USIPY_SIP_TM_OK);
}

static int
start_register(struct netsim_sip *sp)
{
  if (local_registrar_mode(sp))
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  return (usipy_sip_register_start(&sp->reg,
    &(const struct usipy_sip_register_start_params){
      .tm = sp->tm,
      .call_id = sp->call_id,
      .request_uri = sp->request_uri,
      .target = sp->server_addr,
      .username = sp->username,
      .callbacks = {
        .arg = sp,
        .response = outgoing_response,
        .timeout = outgoing_timeout,
      },
    }, NULL));
}

static int
start_pending_call(struct netsim_sip *sp)
{
  struct usipy_sip_ua_event ev = {0};
  struct usipy_sip_ua_dial_params *dialp = &ev.data.dial;
  char req_uri_buf[NETSIM_SIP_URI_BUFSIZE];
  char local_host[NETSIM_SIP_HOST_BUFSIZE];
  struct usipy_str to_user;
  struct usipy_str request_uri;
  struct usipy_sip_tm_addr target;
  size_t tx_index;
  int blen;

  if (!sp->pending_call.valid || sp->ua == NULL ||
      usipy_sip_ua_get_state(sp->ua) != USIPY_SIP_UA_STATE_IDLE)
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  if (local_registrar_mode(sp)) {
    if (!sp->peer_binding.valid)
      return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    request_uri = sp->peer_binding.contact_uri;
    target = sp->peer_binding.target;
  } else {
    blen = snprintf(req_uri_buf, sizeof(req_uri_buf), "sip:%s@%s%s%s",
      sp->cfg.peer_user, sp->cfg.server_host,
      strcmp(sp->cfg.server_port, "5060") == 0 ? "" : ":",
      strcmp(sp->cfg.server_port, "5060") == 0 ? "" : sp->cfg.server_port);
    if (blen < 0 || (size_t)blen >= sizeof(req_uri_buf))
      return (USIPY_SIP_TM_ERR_INVAL);
    request_uri = (struct usipy_str){.s.ro = req_uri_buf, .l = strlen(req_uri_buf)};
    target = sp->server_addr;
  }
  if (!resolve_local_host_for_target(sp, &target, local_host, sizeof(local_host)) ||
      build_call_sdp(&sp->pending_call, local_host, sp->local_port) != 0)
    return (USIPY_SIP_TM_ERR_INVAL);
  sp->pending_call.sdp = (struct usipy_str){.s.ro = sp->pending_call.sdp_buf,
    .l = strlen(sp->pending_call.sdp_buf)};
  ev.type = USIPY_SIP_UA_EVENT_DIAL;
  to_user = (struct usipy_str){.s.ro = sp->cfg.peer_user, .l = strlen(sp->cfg.peer_user)};
  *dialp = (struct usipy_sip_ua_dial_params){
    .request = {
      .request_id = {
        .cseq = sp->next_invite_cseq++,
        .method_type = USIPY_SIP_METHOD_INVITE,
      },
      .request_target = {
        .request_uri = request_uri,
        .target = target,
      },
      .parties_by_username = {
        .from = sp->username,
        .to = to_user,
        .contact = sp->username,
      },
      .invite_expires = 60,
      .content_type = (struct usipy_str)USIPY_2STR("application/sdp"),
      .body = sp->pending_call.sdp,
      .callbacks = {
        .arg = sp,
        .response = outgoing_response,
        .timeout = outgoing_timeout,
      },
    },
    .auth = {
      .username = sp->username,
      .password = sp->password,
      .qop = sp->qop,
    },
  };
  if (usipy_sip_ua_on_event(sp->ua, &ev, &tx_index) != USIPY_SIP_TM_OK)
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  sip_log("INVITE queued peer=%s cseq=%u session=0x%016llx",
    sp->cfg.peer_user, sp->next_invite_cseq - 1,
    (unsigned long long)sp->pending_call.session_nonce);
  sp->invite_index = tx_index;
  return (USIPY_SIP_TM_OK);
}

static void
outgoing_response(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
  struct netsim_sip *sp = arg;
  const unsigned int scode = msg->sline.parsed.sl.status.code;
  enum usipy_sip_register_response_result reg_rval;
  int rval;

  if (txp->common.id.method_type == USIPY_SIP_METHOD_REGISTER) {
    sip_log("REGISTER response %u cseq=%u", scode, txp->common.id.cseq);
    rval = usipy_sip_register_handle_response(&sp->reg, sp->tm, tx_index, txp, msg,
      &sp->username, &sp->password, &sp->qop, netsim_sip_now_ms(), &reg_rval);
    if (rval != USIPY_SIP_TM_OK) {
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
      return;
    }
    if (reg_rval == USIPY_SIP_REGISTER_RESPONSE_ESTABLISHED) {
      sip_log("REGISTER established expires=%u next_refresh_at_ms=%llu",
        sp->reg.expires, (unsigned long long)sp->reg.next_refresh_at_ms);
      return;
    }
    if (reg_rval == USIPY_SIP_REGISTER_RESPONSE_FINAL)
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return;
  }
  sip_log("INVITE response %u cseq=%u", scode, txp->common.id.cseq);
  if (msg->kind == USIPY_SIP_MSG_RES && scode > 100 && scode < 200)
    return;
  rval = usipy_sip_ua_on_tx_response(sp->ua, tx_index, msg);
  if (rval != USIPY_SIP_TM_OK)
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
}

static void
outgoing_timeout(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  enum usipy_sip_tm_uac_timeout_id timeout_id)
{
  struct netsim_sip *sp = arg;

  (void)tx_index;
  (void)timeout_id;
  if (txp->common.id.method_type == USIPY_SIP_METHOD_REGISTER) {
    usipy_sip_register_handle_timeout(&sp->reg);
    sip_log("REGISTER timeout id=%u", (unsigned int)timeout_id);
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return;
  }
  sip_log("INVITE timeout id=%u", (unsigned int)timeout_id);
  sp->ua_reset_needed = true;
  event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
}

static void
incoming_request(void *arg, const struct usipy_sip_tm_handle_incoming_in *hin,
  const struct usipy_msg *msg)
{
  struct netsim_sip *sp = arg;
  uint8_t method_type;
  netsim_sockaddr_t peer_addr;
  char local_host[NETSIM_SIP_HOST_BUFSIZE];
  struct usipy_sip_tm_addr local_addr;

  if (apply_ua_reset(sp) != USIPY_SIP_TM_OK)
    return;
  sp->incoming_local_host[0] = '\0';
  if (tm_addr_to_sockaddr(&hin->peer, &peer_addr) != true)
    memset(&peer_addr, '\0', sizeof(peer_addr));
  method_type = msg->sline.parsed.rl.method->cantype;
  if (local_registrar_mode(sp) && method_type == USIPY_SIP_METHOD_REGISTER) {
    struct usipy_str contact_uri;
    char target_host[NETSIM_SIP_HOST_BUFSIZE];
    uint16_t target_port;
    unsigned int expires;
    int rval;

    rval = extract_register_binding(msg, &(struct usipy_str){
        .s.ro = sp->cfg.peer_user, .l = strlen(sp->cfg.peer_user),
      }, &contact_uri, target_host, sizeof(target_host), &target_port, &expires);
    if (rval != USIPY_SIP_TM_OK ||
        send_register_ok(sp, hin, msg, &contact_uri, expires) != USIPY_SIP_TM_OK ||
        store_peer_registration(sp, hin, &contact_uri, target_host, target_port,
          expires) != USIPY_SIP_TM_OK) {
      (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
        &netsim_sip_res_forbidden);
      if (rval != USIPY_SIP_TM_OK)
        sip_log("REGISTER rejected rval=%d", rval);
    }
    return;
  }
  if (!local_registrar_mode(sp) && !usipy_sip_tm_addr_same(&sp->server_addr, &hin->peer)) {
    (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
      &netsim_sip_res_forbidden);
    return;
  }
  if (sp->ua != NULL && usipy_sip_ua_matches_transaction(sp->ua, msg)) {
    struct usipy_sip_tm_new_uas_tr_params tp = {
      .request = msg,
      .timers = hin->timers,
      .peer = hin->peer,
      .local = hin->local,
    };
    size_t tx_index;

    if (resolve_local_tm_addr_for_peer(sp, &hin->peer, hin->local.port, &local_addr,
          local_host, sizeof(local_host))) {
      tp.local = local_addr;
      strcpy(sp->incoming_local_host, local_host);
    }
    if (usipy_sip_tm_new_uas_tr(sp->tm, &tp, &tx_index) != USIPY_SIP_TM_OK ||
        usipy_sip_ua_on_transaction(sp->ua, tx_index, msg) != USIPY_SIP_TM_OK)
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return;
  }
  if (!usipy_sip_ua_request_targets_user(msg, &sp->username)) {
    (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
      &netsim_sip_res_not_found);
    return;
  }
  if (method_type == USIPY_SIP_METHOD_INVITE) {
    struct usipy_sip_tm_new_uas_tr_params tp = {
      .request = msg,
      .timers = hin->timers,
      .peer = hin->peer,
      .local = hin->local,
    };
    size_t tx_index;

    if (sp->ua == NULL || usipy_sip_ua_get_state(sp->ua) != USIPY_SIP_UA_STATE_IDLE) {
      (void)usipy_sip_send_stateless_response(msg, &usipy_sip_res_busy_here,
        send_stateless_response, &(struct {
          struct netsim_sip *sp;
          const netsim_sockaddr_t *peerp;
        }){sp, &peer_addr});
      return;
    }
    if (resolve_local_tm_addr_for_peer(sp, &hin->peer, hin->local.port, &local_addr,
          local_host, sizeof(local_host))) {
      tp.local = local_addr;
      strcpy(sp->incoming_local_host, local_host);
    }
    if (usipy_sip_tm_new_uas_tr(sp->tm, &tp, &tx_index) != USIPY_SIP_TM_OK ||
        usipy_sip_ua_on_transaction(sp->ua, tx_index, msg) != USIPY_SIP_TM_OK)
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return;
  }
  if (method_type == USIPY_SIP_METHOD_BYE) {
    (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
      &netsim_sip_res_not_found);
    return;
  }
  (void)usipy_sip_send_stateless_response(msg, &usipy_sip_res_not_impl,
    send_stateless_response, &(struct {
      struct netsim_sip *sp;
      const netsim_sockaddr_t *peerp;
    }){sp, &peer_addr});
}

static void
ua_emit(void *arg, const struct usipy_sip_ua_emit *emitp)
{
  struct netsim_sip *sp = arg;

  if (run_tm_now(sp, netsim_sip_now_ms()) != USIPY_SIP_TM_OK) {
    sip_log("run_tm failed during ua_emit");
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return;
  }
  switch (emitp->type) {
    case USIPY_SIP_UA_EMIT_DIAL:
      sip_log("UA incoming INVITE role=%d body=%zu",
        (int)emitp->role, emitp->message != NULL ? emitp->message->body.l : 0);
      if (emitp->message != NULL && emitp->message->body.l > 0) {
        struct netsim_sdp_desc remote_desc;

        if (!netsim_sip_sdp_parse(emitp->message->body.s.ro, emitp->message->body.l,
              &remote_desc) || remote_desc.player != 0) {
          struct usipy_sip_ua_event ev = {
            .type = USIPY_SIP_UA_EVENT_CONNECT,
          };
          size_t tx_index;

          ev.data.response.status = netsim_sip_res_bad_sdp;
          (void)usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
          return;
        }
        session_clear(&sp->pending_remote);
        sp->pending_remote.valid = true;
        sp->pending_remote.session_nonce = remote_desc.session_nonce;
        sp->pending_remote.local_player = 1;
        sp->pending_remote.remote_player = 0;
        sp->pending_remote.local_stream_ssrc = make_local_stream_ssrc(sp,
          remote_desc.session_nonce);
        sp->pending_remote.peer_stream_ssrc = remote_desc.stream_ssrc;
        {
          char errbuf[NETSIM_SIP_ERR_BUFSIZE];

          if (netsim_sockaddr_resolve_udp(remote_desc.conn_host,
                remote_desc.media_port, &sp->pending_remote.media_addr, errbuf,
                sizeof(errbuf))) {
            event_push(sp, NETSIM_SIP_EVENT_REMOTE_START, &sp->pending_remote);
            answer_incoming_invite(sp, emitp->message, &remote_desc);
            return;
          }
        }
      }
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
      return;

    case USIPY_SIP_UA_EMIT_CONNECT:
      sip_log("UA connected role=%d body=%zu", (int)emitp->role, emitp->body.l);
      if (emitp->role == USIPY_SIP_TM_ROLE_UAS) {
        sp->current = sp->pending_remote;
        sp->current.local_stream_ssrc = sp->answered_call.local_stream_ssrc;
        sp->current.valid = true;
        event_push(sp, NETSIM_SIP_EVENT_CONNECTED, &sp->current);
        sp->pending_remote.valid = false;
        return;
      }
      if (emitp->body.l > 0) {
        struct netsim_sdp_desc remote_desc;
        struct netsim_sip_session session;

        if (netsim_sip_sdp_parse(emitp->body.s.ro, emitp->body.l, &remote_desc) &&
            remote_desc.session_nonce == sp->pending_call.session_nonce &&
            remote_desc.player == 1 - sp->pending_call.local_player &&
            make_media_session(&session, &sp->pending_call, &remote_desc) == 0) {
          sp->current = session;
          event_push(sp, NETSIM_SIP_EVENT_CONNECTED, &sp->current);
          return;
        }
      }
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
      return;

    case USIPY_SIP_UA_EMIT_DISCONNECT:
      sip_log("UA disconnected role=%d", (int)emitp->role);
      if (sp->current.valid) {
        event_push(sp, NETSIM_SIP_EVENT_DISCONNECTED, &sp->current);
        session_clear(&sp->current);
      } else if (sp->pending_remote.valid) {
        event_push(sp, NETSIM_SIP_EVENT_DISCONNECTED, &sp->pending_remote);
        session_clear(&sp->pending_remote);
      } else {
        event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
      }
      sp->pending_call.valid = false;
      sp->answered_call.valid = false;
      sp->ua_reset_needed = true;
      return;
  }
}

struct netsim_sip *
netsim_sip_create(const struct netsim_sip_config *cfgp, netsim_socket_t sock,
  const char *local_host, const char *local_port, char *errbuf,
  size_t errbuf_len)
{
  struct netsim_sip *sp;
  netsim_sockaddr_t server_addr;
  int blen;

  sp = calloc(1, sizeof(*sp));
  if (sp == NULL) {
    snprintf(errbuf, errbuf_len, "out of memory");
    return (NULL);
  }
  sp->sock = sock;
  sp->cfg = *cfgp;
  sp->reg.requested_expires = 300;
  sp->reg.next_cseq = 1;
  sp->next_invite_cseq = 1;
  sp->invite_index = USIPY_SIP_TM_TX_INDEX_NONE;
  strcpy(sp->local_host, local_host);
  strcpy(sp->local_port, local_port);
  sp->username = (struct usipy_str){.s.ro = sp->cfg.username,
    .l = strlen(sp->cfg.username)};
  sp->password = (struct usipy_str){.s.ro = sp->cfg.password,
    .l = strlen(sp->cfg.password)};
  sp->qop = (struct usipy_str)USIPY_2STR("auth");
  if (!local_registrar_mode(sp)) {
    if (!netsim_sockaddr_resolve_udp(sp->cfg.server_host, sp->cfg.server_port,
          &server_addr, errbuf, errbuf_len)) {
      free(sp);
      return (NULL);
    }
    if (inet_ntop(AF_INET, &((struct sockaddr_in *)&server_addr.ss)->sin_addr,
          sp->server_uri_host, sizeof(sp->server_uri_host)) == NULL) {
      snprintf(errbuf, errbuf_len, "cannot format server host");
      free(sp);
      return (NULL);
    }
    sp->server_addr.af = AF_INET;
    sp->server_addr.port = (uint16_t)atoi(sp->cfg.server_port);
    sp->server_addr.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    sp->server_addr.host = (struct usipy_str){.s.ro = sp->server_uri_host,
      .l = strlen(sp->server_uri_host)};
    blen = snprintf(sp->request_uri_buf, sizeof(sp->request_uri_buf), "sip:%s%s%s",
      sp->cfg.server_host, strcmp(sp->cfg.server_port, "5060") == 0 ? "" : ":",
      strcmp(sp->cfg.server_port, "5060") == 0 ? "" : sp->cfg.server_port);
    if (blen < 0 || (size_t)blen >= sizeof(sp->request_uri_buf)) {
      snprintf(errbuf, errbuf_len, "request URI too long");
      free(sp);
      return (NULL);
    }
    sp->request_uri = (struct usipy_str){.s.ro = sp->request_uri_buf,
      .l = strlen(sp->request_uri_buf)};
  }
  if (usipy_tm_uac_production_ids_init(&sp->production_ids) != 0) {
    snprintf(errbuf, errbuf_len, "cannot initialize SIP ids");
    free(sp);
    return (NULL);
  }
  sp->call_id = sp->production_ids.call_id_s;
  if (init_tm_ua(sp, errbuf, errbuf_len) != USIPY_SIP_TM_OK) {
    free(sp);
    return (NULL);
  }
  return (sp);
}

void
netsim_sip_destroy(struct netsim_sip *sp)
{

  if (sp == NULL)
    return;
  if (sp->ua != NULL)
    usipy_sip_ua_dtor(sp->ua);
  if (sp->tm != NULL)
    usipy_sip_tm_dtor(sp->tm);
  free(sp);
}

bool
netsim_sip_run(struct netsim_sip *sp)
{
  uint64_t now_ms;

  now_ms = netsim_sip_now_ms();
  if (sp->peer_binding.valid && now_ms >= sp->peer_binding.expires_at_ms) {
    sip_log("peer registration expired");
    peer_binding_clear(&sp->peer_binding);
  }
  if (apply_ua_reset(sp) != USIPY_SIP_TM_OK) {
    sip_log("UA reset failed");
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return (false);
  }
  if (!local_registrar_mode(sp) && !sp->reg.registering && sp->reg.next_refresh_at_ms != 0 &&
      now_ms >= sp->reg.next_refresh_at_ms && start_register(sp) != USIPY_SIP_TM_OK) {
    sip_log("refresh REGISTER start failed");
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return (false);
  }
  if (((!local_registrar_mode(sp) && sp->reg.registered_once) ||
       (local_registrar_mode(sp) && sp->peer_binding.valid)) &&
      sp->pending_call.valid &&
      sp->ua != NULL && usipy_sip_ua_get_state(sp->ua) == USIPY_SIP_UA_STATE_IDLE) {
    int rval;

    sip_log("starting pending INVITE session=0x%016llx", 
      (unsigned long long)sp->pending_call.session_nonce);
    rval = start_pending_call(sp);
    if (rval != USIPY_SIP_TM_OK) {
      sip_log("start_pending_call failed rval=%d", rval);
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
      return (false);
    }
  }
  if (run_tm_now(sp, now_ms) != USIPY_SIP_TM_OK) {
    sip_log("run_tm failed");
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return (false);
  }
  usipy_sip_tm_reap_terminated(sp->tm);
  return (true);
}

netsim_deadline_t
netsim_sip_next_wakeup(const struct netsim_sip *sp, netsim_deadline_t fallback)
{
  netsim_deadline_t deadline;

  deadline = fallback;
  if (sp->reg.next_refresh_at_ms != 0 && sp->reg.next_refresh_at_ms < deadline)
    deadline = sp->reg.next_refresh_at_ms;
  return (deadline);
}

bool
netsim_sip_handle_packet(struct netsim_sip *sp, const void *buf, size_t len,
  const netsim_sockaddr_t *peerp, const netsim_sockaddr_t *localp)
{
  struct usipy_sip_tm_handle_incoming_in hin = {0};
  struct usipy_sip_tm_handle_incoming_out hout;

  if (!sockaddr_to_tm_addr(peerp, &hin.peer) || !sockaddr_to_tm_addr(localp, &hin.local))
    return (false);
  hin.now_ms = netsim_sip_now_ms();
  hin.tm = sp->tm;
  hin.buf = buf;
  hin.len = len;
  if (usipy_sip_tm_handle_incoming(&hin, &hout) != USIPY_SIP_TM_OK &&
      hout.error != USIPY_SIP_TM_ERR_NOT_FOUND) {
    tm_addr_cleanup(&hin.peer);
    tm_addr_cleanup(&hin.local);
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return (false);
  }
  tm_addr_cleanup(&hin.peer);
  tm_addr_cleanup(&hin.local);
  usipy_sip_tm_reap_terminated(sp->tm);
  return (true);
}

bool
netsim_sip_pop_event(struct netsim_sip *sp, struct netsim_sip_event *evp)
{

  return (event_pop(sp, evp));
}

bool
netsim_sip_start_call(struct netsim_sip *sp, uint64_t session_nonce,
  uint32_t local_stream_ssrc, int local_player, char *errbuf,
  size_t errbuf_len)
{

  memset(&sp->pending_call, '\0', sizeof(sp->pending_call));
  sp->pending_call.valid = true;
  sp->pending_call.session_nonce = session_nonce;
  sp->pending_call.local_stream_ssrc = local_stream_ssrc;
  sp->pending_call.local_player = local_player;
  if ((!local_registrar_mode(sp) && !sp->reg.registered_once) ||
      (local_registrar_mode(sp) && !sp->peer_binding.valid)) {
    snprintf(errbuf, errbuf_len, local_registrar_mode(sp) ?
      "waiting for peer REGISTER" : "waiting for SIP registration");
    sip_log("call deferred until %s session=0x%016llx",
      local_registrar_mode(sp) ? "peer REGISTER arrives" : "REGISTER completes",
      (unsigned long long)session_nonce);
    return (true);
  }
  if (start_pending_call(sp) != USIPY_SIP_TM_OK) {
    snprintf(errbuf, errbuf_len, "cannot start INVITE");
    sip_log("start_call immediate INVITE start failed");
    return (false);
  }
  return (true);
}

void
netsim_sip_hangup(struct netsim_sip *sp)
{
  struct usipy_sip_ua_event ev = {
    .type = USIPY_SIP_UA_EVENT_DISCONNECT,
  };
  size_t tx_index;

  sp->pending_call.valid = false;
  if (sp->ua == NULL)
    return;
  if (usipy_sip_ua_get_state(sp->ua) == USIPY_SIP_UA_STATE_IDLE)
    return;
  (void)usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
}

bool
netsim_sip_packet_looks_like(const void *buf, size_t len)
{
  const unsigned char *cp = buf;

  if (len < 4)
    return (false);
  if (len >= 7 && memcmp(cp, "SIP/2.0", 7) == 0)
    return (true);
  return (isalpha(cp[0]) && isalpha(cp[1]) && isalpha(cp[2]));
}

#else

struct netsim_sip {
  int dummy;
};

struct netsim_sip *
netsim_sip_create(const struct netsim_sip_config *cfgp, netsim_socket_t sock,
  const char *local_host, const char *local_port, char *errbuf,
  size_t errbuf_len)
{

  (void)cfgp;
  (void)sock;
  (void)local_host;
  (void)local_port;
  snprintf(errbuf, errbuf_len, "SIP netsim is only implemented on POSIX builds");
  return (NULL);
}

void
netsim_sip_destroy(struct netsim_sip *sp)
{

  (void)sp;
}

bool
netsim_sip_run(struct netsim_sip *sp)
{

  (void)sp;
  return (false);
}

netsim_deadline_t
netsim_sip_next_wakeup(const struct netsim_sip *sp, netsim_deadline_t fallback)
{

  (void)sp;
  return (fallback);
}

bool
netsim_sip_handle_packet(struct netsim_sip *sp, const void *buf, size_t len,
  const netsim_sockaddr_t *peerp, const netsim_sockaddr_t *localp)
{

  (void)sp;
  (void)buf;
  (void)len;
  (void)peerp;
  (void)localp;
  return (false);
}

bool
netsim_sip_pop_event(struct netsim_sip *sp, struct netsim_sip_event *evp)
{

  (void)sp;
  (void)evp;
  return (false);
}

bool
netsim_sip_start_call(struct netsim_sip *sp, uint64_t session_nonce,
  uint32_t local_stream_ssrc, int local_player, char *errbuf,
  size_t errbuf_len)
{

  (void)sp;
  (void)session_nonce;
  (void)local_stream_ssrc;
  (void)local_player;
  snprintf(errbuf, errbuf_len, "SIP netsim is only implemented on POSIX builds");
  return (false);
}

void
netsim_sip_hangup(struct netsim_sip *sp)
{

  (void)sp;
}

bool
netsim_sip_packet_looks_like(const void *buf, size_t len)
{

  (void)buf;
  (void)len;
  return (false);
}

#endif
