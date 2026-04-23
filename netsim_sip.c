/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim_sip_internal.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "netsim_sip_sdp.h"
#include "digger_log.h"

#if NETSIM_PLATFORM_SUPPORTED

#include "microsippy/src/public/usipy_sip_dialog.h"
#include "microsippy/src/public/usipy_sip_response_utils.h"
#include "microsippy/src/public/usipy_sip_tm_utils.h"
#include "microsippy/src/public/usipy_sip_ua_utils.h"
#include "microsippy/src/usipy_sip_hdr.h"
#include "microsippy/src/usipy_sip_hdr_nameaddr.h"
#include "microsippy/src/usipy_sip_method_db.h"
#include "microsippy/src/usipy_sip_res.h"
#include "microsippy/src/usipy_sip_uri.h"

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

#define NETSIM_SIP_REGISTER_RETRY_MS 5000ULL

static uint64_t
netsim_sip_now_ms(void)
{

  return (netsim_monotonic_ns() / 1000000ULL);
}

static bool
copy_str_to_buf(const struct usipy_str *srcp, char *dstbuf, size_t dstbuf_len,
  struct usipy_str *dstp)
{

  assert(srcp != NULL);
  assert(dstbuf != NULL);
  assert(dstp != NULL);
  assert(srcp->l == 0 || srcp->s.ro != NULL);
  if (srcp->l >= dstbuf_len)
    return (false);
  if (srcp->l != 0)
    memcpy(dstbuf, srcp->s.ro, srcp->l);
  dstbuf[srcp->l] = '\0';
  *dstp = (struct usipy_str){.s.ro = dstbuf, .l = srcp->l};
  return (true);
}

bool
local_registrar_mode(const struct netsim_sip *sp)
{

  return (sp->cfg.server_host.l == 0);
}

static struct usipy_sip_tm_addr
server_target(const struct netsim_sip *sp)
{

  return ((struct usipy_sip_tm_addr){
    .af = AF_INET,
    .port = (uint16_t)atoi(sp->cfg.server_port_buf),
    .transport = USIPY_SIP_TM_TRANSPORT_UDP,
    .host = sp->cfg.server_host,
  });
}

void
netsim_sip_config_rebind(struct netsim_sip_config *cfgp)
{

  cfgp->username.s.ro = cfgp->username_buf;
  cfgp->password.s.ro = cfgp->password_buf;
  cfgp->server_host.s.ro = cfgp->server_host_buf;
  cfgp->server_port.s.ro = cfgp->server_port_buf;
  cfgp->peer_user.s.ro = cfgp->peer_user_buf;
}

void
netsim_sip_config_clone(struct netsim_sip_config *dstp,
  const struct netsim_sip_config *srcp)
{

  *dstp = *srcp;
  netsim_sip_config_rebind(dstp);
}

static void outgoing_response(void *, size_t, const struct usipy_sip_tm_tx *,
  const struct usipy_msg *);
static void outgoing_timeout(void *, size_t, const struct usipy_sip_tm_tx *,
  enum usipy_sip_tm_uac_timeout_id);
static void incoming_request(void *, const struct usipy_sip_tm_handle_incoming_in *,
  const struct usipy_msg *);
static void ua_emit(void *, const struct usipy_sip_ua_emit *);
static int start_register(struct netsim_sip *);
static void schedule_register_retry(struct netsim_sip *sp, uint64_t now_ms);

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
event_rebind_peer_user(struct netsim_sip_event *evp)
{

  evp->peer_user = evp->peer_user.l != 0 ? (struct usipy_str){
    .s.ro = evp->peer_user_buf,
    .l = evp->peer_user.l,
  } : USIPY_STR_NULL;
}

static bool
event_set_peer_user(struct netsim_sip_event *evp,
  const struct usipy_str *peer_user)
{

  if (peer_user->l == 0) {
    evp->peer_user = USIPY_STR_NULL;
    evp->peer_user_buf[0] = '\0';
    return (true);
  }
  if (peer_user->l >= sizeof(evp->peer_user_buf))
    return (false);
  memcpy(evp->peer_user_buf, peer_user->s.ro, peer_user->l);
  evp->peer_user_buf[peer_user->l] = '\0';
  evp->peer_user = (struct usipy_str){
    .s.ro = evp->peer_user_buf,
    .l = peer_user->l,
  };
  return (true);
}

static void
event_push(struct netsim_sip *sp, enum netsim_sip_event_type type,
  const struct netsim_sip_session *sessionp)
{
  struct netsim_sip_event ev = {.type = type};

  if (sessionp != NULL)
    ev.session = *sessionp;
  if (sp->ev_len == NETSIM_SIP_EVENTS)
    return;
  sp->events[sp->ev_tail] = ev;
  sp->ev_tail = (sp->ev_tail + 1) % NETSIM_SIP_EVENTS;
  sp->ev_len++;
}

static void
event_push_peer(struct netsim_sip *sp, enum netsim_sip_event_type type,
  const struct netsim_sip_session *sessionp, const struct usipy_str *peer_user)
{
  struct netsim_sip_event ev = {.type = type};

  if (sessionp != NULL)
    ev.session = *sessionp;
  if (!event_set_peer_user(&ev, peer_user))
    return;
  if (sp->ev_len == NETSIM_SIP_EVENTS)
    return;
  sp->events[sp->ev_tail] = ev;
  sp->ev_tail = (sp->ev_tail + 1) % NETSIM_SIP_EVENTS;
  sp->ev_len++;
}

void
event_push_registered(struct netsim_sip *sp, const struct usipy_str *peer_user)
{
  struct netsim_sip_event ev = {.type = NETSIM_SIP_EVENT_REGISTERED};

  if (!event_set_peer_user(&ev, peer_user))
    return;
  if (sp->ev_len == NETSIM_SIP_EVENTS)
    return;
  sp->events[sp->ev_tail] = ev;
  sp->ev_tail = (sp->ev_tail + 1) % NETSIM_SIP_EVENTS;
  sp->ev_len++;
}

static bool
event_pop(struct netsim_sip *sp, struct netsim_sip_event *evp)
{

  if (sp->ev_len == 0)
    return (false);
  *evp = sp->events[sp->ev_head];
  event_rebind_peer_user(evp);
  sp->ev_head = (sp->ev_head + 1) % NETSIM_SIP_EVENTS;
  sp->ev_len--;
  return (true);
}

static void
session_clear(struct netsim_sip_session *sp)
{

  memset(sp, '\0', sizeof(*sp));
}

static void
pending_call_to_session(const struct netsim_sip_call_req *crp,
  struct netsim_sip_session *sessionp)
{

  *sessionp = (struct netsim_sip_session){
    .valid = crp->valid,
    .session_nonce = crp->session_nonce,
    .local_stream_ssrc = crp->local_stream_ssrc,
    .local_player = crp->local_player,
    .remote_player = 1 - crp->local_player,
  };
}

static bool
select_role_session(const struct netsim_sip *sp, enum usipy_sip_tm_role role,
  struct netsim_sip_session *sessionp)
{

  if (sp->disconnecting.valid && sp->disconnecting_role == role) {
    *sessionp = sp->disconnecting;
    return (true);
  }
  if (sp->current.valid && sp->current_role == role) {
    *sessionp = sp->current;
    return (true);
  }
  if (role == USIPY_SIP_TM_ROLE_UAC && sp->pending_call.valid) {
    pending_call_to_session(&sp->pending_call, sessionp);
    return (true);
  }
  if (role == USIPY_SIP_TM_ROLE_UAS && sp->pending_remote.valid) {
    *sessionp = sp->pending_remote;
    return (true);
  }
  return (false);
}

static bool
extract_peer_user(const struct usipy_msg *msg, char *peer_user, size_t peer_user_len)
{
  struct usipy_msg *cmsg;
  struct usipy_sip_hdr_match *from_hdrs;
  const struct usipy_sip_hdr_nameaddr *fromp;
  struct usipy_sip_uri *from_uri;

  if (msg == NULL || peer_user == NULL || peer_user_len == 0)
    return (false);
  cmsg = (struct usipy_msg *)msg;
  from_hdrs = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
  *from_hdrs = (struct usipy_sip_hdr_match){.hdrslen = 1};
  if (usipy_sip_msg_parse_hdrs_get(cmsg, USIPY_HFT_MASK(USIPY_HF_FROM), 1,
        from_hdrs) != 0 || from_hdrs->nhdrs == 0)
    return (false);
  fromp = from_hdrs->hdrsp[0]->parsed.from;
  if (fromp == NULL)
    return (false);
  from_uri = usipy_sip_uri_parse(&cmsg->heap, &fromp->addr_spec);
  if (from_uri == NULL || from_uri->user.l == 0 || from_uri->user.l >= peer_user_len)
    return (false);
  memcpy(peer_user, from_uri->user.s.ro, from_uri->user.l);
  peer_user[from_uri->user.l] = '\0';
  return (true);
}

static uint32_t
make_local_stream_ssrc(const struct netsim_sip *sp, uint64_t session_nonce)
{
  const unsigned char *cp;
  uint32_t ssrc;

  ssrc = (uint32_t)(session_nonce ^ (session_nonce >> 32));
  for (cp = (const unsigned char *)sp->username.s.ro;
       cp < (const unsigned char *)sp->username.s.ro + sp->username.l; cp++)
    ssrc = (ssrc * 16777619u) ^ *cp;
  ssrc ^= 0x5354524dU;
  if (ssrc == 0)
    ssrc = 0x4e53494dU;
  return (ssrc);
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
init_tm_ua(struct netsim_sip *sp, char *errbuf, size_t errbuf_len)
{
  const struct usipy_sip_tm_callbacks callbacks = {
    .arg = sp,
    .incoming_request = incoming_request,
  };
  const struct usipy_sip_tm_id_policy id_policy = {
    .arg = &sp->production_ids,
    .cb = usipy_tm_uac_production_id_policy,
  };
  struct usipy_sip_tm_ctor_params tm_ctorp = {
    .sock = (int)sp->sock,
    .transport = USIPY_SIP_TM_TRANSPORT_UDP,
    .max_transactions = NETSIM_SIP_MAX_TX,
    .callbacks = &callbacks,
    .id_policy = &id_policy,
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
        .default_target = !local_registrar_mode(sp) ?
          &(const struct usipy_sip_tm_addr){
            .af = AF_INET,
            .port = (uint16_t)atoi(sp->cfg.server_port_buf),
            .transport = USIPY_SIP_TM_TRANSPORT_UDP,
            .host = sp->cfg.server_host,
          } : NULL,
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

static bool
answer_incoming_invite(struct netsim_sip *sp, char *errbuf, size_t errbuf_len)
{
  struct usipy_sip_ua_event ev = {0};
  struct netsim_sip_call_req *crp;
  size_t tx_index;
  int rval;

  if (!sp->pending_remote.valid) {
    snprintf(errbuf, errbuf_len, "no pending remote session");
    return (false);
  }
  crp = &sp->answered_call;
  memset(crp, '\0', sizeof(*crp));
  crp->valid = true;
  crp->session_nonce = sp->pending_remote.session_nonce;
  crp->local_stream_ssrc = sp->pending_remote.local_stream_ssrc;
  crp->local_player = sp->pending_remote.local_player;
  if (build_call_sdp(crp,
        sp->incoming_local_host[0] != '\0' ? sp->incoming_local_host : sp->local_host,
        sp->local_port) != 0) {
    ev.type = USIPY_SIP_UA_EVENT_CONNECT;
    ev.data.response.status = &netsim_sip_res_bad_sdp;
    rval = usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
    sip_log("answer INVITE with 488 rval=%d tx=%zu", rval, tx_index);
    if (rval == USIPY_SIP_TM_OK)
      (void)run_tm_now(sp, netsim_sip_now_ms());
    snprintf(errbuf, errbuf_len, "cannot build answer SDP");
    return (false);
  }
  crp->sdp = (struct usipy_str){.s.ro = crp->sdp_buf, .l = strlen(crp->sdp_buf)};
  ev.type = USIPY_SIP_UA_EVENT_CONNECT;
  ev.data.response.status = &usipy_sip_res_ok;
  ev.data.response.content_type = &(const struct usipy_str)USIPY_2STR("application/sdp");
  ev.data.response.body = &crp->sdp;
  rval = usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
  sip_log("answer INVITE with 200 rval=%d tx=%zu body=%zu",
    rval, tx_index, crp->sdp.l);
  if (rval == USIPY_SIP_TM_OK) {
    rval = run_tm_now(sp, netsim_sip_now_ms());
    sip_log("post-answer run_tm rval=%d", rval);
  }
  if (rval != USIPY_SIP_TM_OK) {
    snprintf(errbuf, errbuf_len, "cannot send INVITE answer");
    return (false);
  }
  errbuf[0] = '\0';
  return (true);
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
        .default_target = !local_registrar_mode(sp) ?
          &(const struct usipy_sip_tm_addr){
            .af = AF_INET,
            .port = (uint16_t)atoi(sp->cfg.server_port_buf),
            .transport = USIPY_SIP_TM_TRANSPORT_UDP,
            .host = sp->cfg.server_host,
          } : NULL,
      }) != USIPY_SIP_TM_OK)
    return (USIPY_SIP_TM_ERR_NOMEM);
  sp->ua_reset_needed = false;
  return (USIPY_SIP_TM_OK);
}

static int
start_register(struct netsim_sip *sp)
{
  const struct usipy_sip_tm_addr target = server_target(sp);
  const struct usipy_sip_tm_uac_callbacks callbacks = {
    .arg = sp,
    .response = outgoing_response,
    .timeout = outgoing_timeout,
  };

  if (local_registrar_mode(sp))
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  return (usipy_sip_register_start(&sp->reg,
    &(const struct usipy_sip_register_start_params){
      .tm = sp->tm,
      .call_id = &sp->call_id,
      .target = &target,
      .username = &sp->username,
      .callbacks = &callbacks,
    }, NULL));
}

static void
schedule_register_retry(struct netsim_sip *sp, uint64_t now_ms)
{

  sp->reg.next_refresh_at_ms = now_ms + NETSIM_SIP_REGISTER_RETRY_MS;
}

static int
start_pending_call(struct netsim_sip *sp)
{
  struct usipy_sip_ua_event ev = {0};
  struct usipy_sip_ua_dial_params *dialp = &ev.data.dial;
  char local_host[NETSIM_SIP_HOST_BUFSIZE];
  const struct netsim_sip_peer_binding *bp = NULL;
  const struct usipy_str *to_user;
  struct usipy_sip_tm_addr target;
  size_t tx_index;

  if (!sp->pending_call.valid || sp->ua == NULL ||
      usipy_sip_ua_get_state(sp->ua) != USIPY_SIP_UA_STATE_IDLE)
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  if (local_registrar_mode(sp)) {
    bp = peer_binding_find_const(sp, &sp->pending_call.target_user);
    if (bp == NULL)
      return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    target = bp->target;
  } else {
    target = server_target(sp);
  }
  if (!resolve_local_host_for_target(sp, &target, local_host, sizeof(local_host)) ||
      build_call_sdp(&sp->pending_call, local_host, sp->local_port) != 0)
    return (USIPY_SIP_TM_ERR_INVAL);
  sp->pending_call.sdp = (struct usipy_str){.s.ro = sp->pending_call.sdp_buf,
    .l = strlen(sp->pending_call.sdp_buf)};
  ev.type = USIPY_SIP_UA_EVENT_DIAL;
  to_user = &sp->pending_call.target_user;
  *dialp = (struct usipy_sip_ua_dial_params){
    .request = &(struct usipy_sip_tm_new_uac_tr_params){
      .request_id = &(struct usipy_sip_tm_request_id){
        .cseq = sp->next_invite_cseq++,
        .method_type = USIPY_SIP_METHOD_INVITE,
      },
      .request_target = local_registrar_mode(sp) ?
        &(struct usipy_sip_tm_request_target){
          .request_uri = &bp->contact_uri,
          .target = &bp->target,
        } : &(struct usipy_sip_tm_request_target){0},
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = (uint16_t)atoi(sp->local_port),
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = {
          .s.ro = local_host,
          .l = strlen(local_host),
        },
      },
      .parties_by_username = &(struct usipy_sip_tm_request_parties){
        .from = &sp->username,
        .to = to_user,
        .contact = &sp->username,
      },
      .invite_expires = 60,
      .payload = &(struct usipy_sip_tm_request_payload){
        .content_type = &(struct usipy_str)USIPY_2STR("application/sdp"),
        .body = &sp->pending_call.sdp,
      },
      .callbacks = &(struct usipy_sip_tm_uac_callbacks){
        .arg = sp,
        .response = outgoing_response,
        .timeout = outgoing_timeout,
      },
    },
    .to_user = &sp->pending_call.target_user,
    .auth = &(struct usipy_sip_ua_credentials){
      .username = &sp->username,
      .password = &sp->password,
      .qop = &sp->qop,
    },
  };
  if (usipy_sip_ua_on_event(sp->ua, &ev, &tx_index) != USIPY_SIP_TM_OK)
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
  sip_log("INVITE queued peer=%.*s cseq=%u session=0x%016llx",
    USIPY_SFMT(&sp->pending_call.target_user), sp->next_invite_cseq - 1,
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
      sp->reg_active = true;
      sip_log("REGISTER established expires=%u next_refresh_at_ms=%llu",
        sp->reg.expires, (unsigned long long)sp->reg.next_refresh_at_ms);
      return;
    }
    if (reg_rval == USIPY_SIP_REGISTER_RESPONSE_FINAL) {
      sp->reg_active = false;
      schedule_register_retry(sp, netsim_sip_now_ms());
      sip_log("REGISTER failed status=%u retry_at_ms=%llu", scode,
        (unsigned long long)sp->reg.next_refresh_at_ms);
    }
    return;
  }
  sip_log("INVITE response %u cseq=%u", scode, txp->common.id.cseq);
  if (msg->kind == USIPY_SIP_MSG_RES && scode > 100 && scode < 200)
    return;
  rval = usipy_sip_ua_on_tx_response(sp->ua, tx_index, msg);
  if (rval != USIPY_SIP_TM_OK) {
    struct netsim_sip_session session;

    if (select_role_session(sp, USIPY_SIP_TM_ROLE_UAC, &session))
      event_push(sp, NETSIM_SIP_EVENT_ERROR, &session);
  }
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
    sp->reg_active = false;
    schedule_register_retry(sp, netsim_sip_now_ms());
    sip_log("REGISTER timeout id=%u", (unsigned int)timeout_id);
    return;
  }
  sip_log("INVITE timeout id=%u", (unsigned int)timeout_id);
  sp->ua_reset_needed = true;
  {
    struct netsim_sip_session session;

    if (select_role_session(sp, USIPY_SIP_TM_ROLE_UAC, &session))
      event_push(sp, NETSIM_SIP_EVENT_ERROR, &session);
  }
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
  if (hin->peer == NULL || hin->local == NULL || hin->timers == NULL ||
      tm_addr_to_sockaddr(hin->peer, &peer_addr) != true)
    memset(&peer_addr, '\0', sizeof(peer_addr));
  method_type = msg->sline.parsed.rl.method->cantype;
  if (local_registrar_mode(sp) && method_type == USIPY_SIP_METHOD_REGISTER) {
    struct netsim_sip_register_binding binding;
    const struct usipy_str *expected_peer_user;
    int rval;

    expected_peer_user = NULL;
    if (sp->cfg.peer_user.l != 0)
      expected_peer_user = &sp->cfg.peer_user;
    rval = extract_register_binding(msg, expected_peer_user, &binding);
    if (rval != USIPY_SIP_TM_OK ||
        send_register_ok(sp, hin, msg, &binding.contact_uri, binding.expires) !=
          USIPY_SIP_TM_OK ||
        store_peer_registration(sp, hin, &(const struct usipy_str){
          .s.ro = binding.peer_user,
          .l = strlen(binding.peer_user),
        }, &binding.contact_uri,
          binding.target_host, binding.target_port, binding.expires) !=
          USIPY_SIP_TM_OK) {
      (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
        &netsim_sip_res_forbidden);
      if (rval != USIPY_SIP_TM_OK)
        sip_log("REGISTER rejected rval=%d", rval);
    }
    return;
  }
  if (!local_registrar_mode(sp) && !usipy_sip_tm_addr_same(&sp->server_addr, hin->peer)) {
    (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
      &netsim_sip_res_forbidden);
    return;
  }
  if (sp->ua != NULL && usipy_sip_ua_matches_transaction(sp->ua, msg)) {
    const struct usipy_sip_tm_addr *localp = hin->local;
    struct usipy_sip_tm_new_uas_tr_params tp = {
      .request = msg,
      .timers = hin->timers,
      .peer = hin->peer,
      .local = localp,
    };
    size_t tx_index;

    if (resolve_local_tm_addr_for_peer(sp, hin->peer, hin->local->port, &local_addr,
          local_host, sizeof(local_host))) {
      localp = &local_addr;
      tp.local = localp;
      strcpy(sp->incoming_local_host, local_host);
    }
    if (usipy_sip_tm_new_uas_tr(sp->tm, &tp, &tx_index) != USIPY_SIP_TM_OK ||
        usipy_sip_ua_on_transaction(sp->ua, tx_index, msg) != USIPY_SIP_TM_OK) {
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    }
    return;
  }
  if (!usipy_sip_ua_request_targets_user(msg, &sp->username)) {
    (void)usipy_sip_tm_send_simple_response(sp->tm, hin, msg,
      &netsim_sip_res_not_found);
    return;
  }
  if (method_type == USIPY_SIP_METHOD_INVITE) {
    const struct usipy_sip_tm_addr *localp = hin->local;
    struct usipy_sip_tm_new_uas_tr_params tp = {
      .request = msg,
      .timers = hin->timers,
      .peer = hin->peer,
      .local = localp,
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
    if (resolve_local_tm_addr_for_peer(sp, hin->peer, hin->local->port, &local_addr,
          local_host, sizeof(local_host))) {
      localp = &local_addr;
      tp.local = localp;
      strcpy(sp->incoming_local_host, local_host);
    }
    if (usipy_sip_tm_new_uas_tr(sp->tm, &tp, &tx_index) != USIPY_SIP_TM_OK ||
        usipy_sip_ua_on_transaction(sp->ua, tx_index, msg) != USIPY_SIP_TM_OK) {
      event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    }
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
    {
      struct netsim_sip_session session;

      if (select_role_session(sp, emitp->role, &session))
        event_push(sp, NETSIM_SIP_EVENT_ERROR, &session);
      else
        event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    }
    return;
  }
  switch (emitp->type) {
    case USIPY_SIP_UA_EMIT_DIAL:
      sip_log("UA incoming INVITE role=%d body=%zu",
        (int)emitp->role, emitp->message != NULL ? emitp->message->body.l : 0);
      if (emitp->message != NULL && emitp->message->body.l > 0) {
        struct netsim_sdp_desc remote_desc;
        char peer_user[NETSIM_SIP_USER_BUFSIZE];

        if (!netsim_sip_sdp_parse(emitp->message->body.s.ro, emitp->message->body.l,
              &remote_desc) || remote_desc.player != 0) {
          struct usipy_sip_ua_event ev = {
            .type = USIPY_SIP_UA_EVENT_CONNECT,
          };
          size_t tx_index;

          ev.data.response.status = &netsim_sip_res_bad_sdp;
          (void)usipy_sip_ua_on_event(sp->ua, &ev, &tx_index);
          return;
        }
        peer_user[0] = '\0';
        (void)extract_peer_user(emitp->message, peer_user, sizeof(peer_user));
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
            const struct usipy_str peer_name = {
              .s.ro = peer_user,
              .l = strlen(peer_user),
            };

            event_push_peer(sp, NETSIM_SIP_EVENT_REMOTE_START, &sp->pending_remote,
              &peer_name);
            return;
          }
        }
      }
      {
        if (sp->pending_remote.valid)
          event_push(sp, NETSIM_SIP_EVENT_ERROR, &sp->pending_remote);
        else
          event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
      }
      return;

    case USIPY_SIP_UA_EMIT_CONNECT:
      sip_log("UA connected role=%d body=%zu", (int)emitp->role, emitp->body.l);
      if (emitp->role == USIPY_SIP_TM_ROLE_UAS) {
        sp->current = sp->pending_remote;
        sp->current_role = emitp->role;
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
          sp->current_role = emitp->role;
          sp->pending_call.valid = false;
          event_push(sp, NETSIM_SIP_EVENT_CONNECTED, &sp->current);
          return;
        }
      }
      {
        struct netsim_sip_session session;

        if (select_role_session(sp, emitp->role, &session))
          event_push(sp, NETSIM_SIP_EVENT_ERROR, &session);
      }
      return;

    case USIPY_SIP_UA_EMIT_DISCONNECT:
      sip_log("UA disconnected role=%d", (int)emitp->role);
      {
        struct netsim_sip_session session;

        if (select_role_session(sp, emitp->role, &session))
          event_push(sp, NETSIM_SIP_EVENT_DISCONNECTED, &session);
      }
      if (sp->current.valid && sp->current_role == emitp->role)
        session_clear(&sp->current);
      if (sp->pending_remote.valid && emitp->role == USIPY_SIP_TM_ROLE_UAS)
        session_clear(&sp->pending_remote);
      if (sp->disconnecting.valid && sp->disconnecting_role == emitp->role)
        session_clear(&sp->disconnecting);
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
  netsim_sip_config_clone(&sp->cfg, cfgp);
  sp->reg.requested_expires = 300;
  sp->reg.next_cseq = 1;
  sp->next_invite_cseq = 1;
  sp->invite_index = USIPY_SIP_TM_TX_INDEX_NONE;
  strcpy(sp->local_host, local_host);
  strcpy(sp->local_port, local_port);
  sp->username = sp->cfg.username;
  sp->password = sp->cfg.password;
  sp->qop = (struct usipy_str)USIPY_2STR("auth");
  if (!local_registrar_mode(sp)) {
    if (!netsim_sockaddr_resolve_udp(sp->cfg.server_host_buf, sp->cfg.server_port_buf,
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
    sp->server_addr.port = (uint16_t)atoi(sp->cfg.server_port_buf);
    sp->server_addr.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    sp->server_addr.host = (struct usipy_str){.s.ro = sp->server_uri_host,
      .l = strlen(sp->server_uri_host)};
    {
      const bool default_sip_port =
        usipy_str_eq(&sp->cfg.server_port, USIPY_PLATFORM.default_udp_port);

    blen = snprintf(sp->request_uri_buf, sizeof(sp->request_uri_buf), "sip:%.*s%.*s%.*s",
      USIPY_SFMT(&sp->cfg.server_host),
      default_sip_port ? 0 : 1,
      default_sip_port ? "" : ":",
      default_sip_port ? 0 : (int)sp->cfg.server_port.l,
      default_sip_port ? "" : sp->cfg.server_port.s.ro);
    }
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
  size_t i;

  now_ms = netsim_sip_now_ms();
  for (i = 0; i < NETSIM_SIP_MAX_REGISTERED; i++) {
      if (sp->peer_bindings[i].valid &&
        now_ms >= sp->peer_bindings[i].expires_at_ms) {
      sip_log("peer registration expired user=%.*s",
        USIPY_SFMT(&sp->peer_bindings[i].peer_user));
      peer_binding_clear(&sp->peer_bindings[i]);
    }
  }
  if (apply_ua_reset(sp) != USIPY_SIP_TM_OK) {
    sip_log("UA reset failed");
    if (sp->current.valid)
      event_push(sp, NETSIM_SIP_EVENT_ERROR, &sp->current);
    return (false);
  }
  if (!local_registrar_mode(sp) && !sp->reg.registering && sp->reg.next_refresh_at_ms != 0 &&
      now_ms >= sp->reg.next_refresh_at_ms && start_register(sp) != USIPY_SIP_TM_OK) {
    sp->reg_active = false;
    schedule_register_retry(sp, now_ms);
    sip_log("refresh REGISTER start failed retry_at_ms=%llu",
      (unsigned long long)sp->reg.next_refresh_at_ms);
  }
  if (sp->pending_call.valid && sp->ua != NULL &&
      usipy_sip_ua_get_state(sp->ua) == USIPY_SIP_UA_STATE_IDLE &&
      (!local_registrar_mode(sp) ||
       peer_binding_find_const(sp, &sp->pending_call.target_user) != NULL)) {
    int rval;

    sip_log("starting pending INVITE session=0x%016llx", 
      (unsigned long long)sp->pending_call.session_nonce);
    rval = start_pending_call(sp);
    if (rval != USIPY_SIP_TM_OK) {
      sip_log("start_pending_call failed rval=%d", rval);
      {
        struct netsim_sip_session session;

        if (select_role_session(sp, USIPY_SIP_TM_ROLE_UAC, &session))
          event_push(sp, NETSIM_SIP_EVENT_ERROR, &session);
      }
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
  struct usipy_sip_tm_timer_policy timers = {0};
  struct usipy_sip_tm_addr peer;
  struct usipy_sip_tm_addr local;
  struct usipy_sip_tm_handle_incoming_in hin = {0};
  struct usipy_sip_tm_handle_incoming_out hout;

  if (!sockaddr_to_tm_addr(peerp, &peer) || !sockaddr_to_tm_addr(localp, &local))
    return (false);
  hin.now_ms = netsim_sip_now_ms();
  hin.tm = sp->tm;
  hin.timers = &timers;
  hin.peer = &peer;
  hin.local = &local;
  hin.buf = buf;
  hin.len = len;
  if (usipy_sip_tm_handle_incoming(&hin, &hout) != USIPY_SIP_TM_OK &&
      hout.error != USIPY_SIP_TM_ERR_NOT_FOUND) {
    tm_addr_cleanup(&peer);
    tm_addr_cleanup(&local);
    event_push(sp, NETSIM_SIP_EVENT_ERROR, NULL);
    return (false);
  }
  tm_addr_cleanup(&peer);
  tm_addr_cleanup(&local);
  usipy_sip_tm_reap_terminated(sp->tm);
  return (true);
}

bool
netsim_sip_pop_event(struct netsim_sip *sp, struct netsim_sip_event *evp)
{

  return (event_pop(sp, evp));
}

bool
netsim_sip_start_call(struct netsim_sip *sp,
  const struct netsim_sip_start_call_in *inp, char *errbuf, size_t errbuf_len)
{
  const struct usipy_str *target_user;

  if (inp == NULL) {
    snprintf(errbuf, errbuf_len, "missing call params");
    return (false);
  }
  target_user = inp->peer_user.l != 0 ? &inp->peer_user : &sp->cfg.peer_user;
  memset(&sp->pending_call, '\0', sizeof(sp->pending_call));
  sp->pending_call.valid = true;
  sp->pending_call.session_nonce = inp->session_nonce;
  sp->pending_call.local_stream_ssrc = inp->local_stream_ssrc;
  sp->pending_call.local_player = inp->local_player;
  if (!copy_str_to_buf(target_user, sp->pending_call.target_user_buf,
        sizeof(sp->pending_call.target_user_buf), &sp->pending_call.target_user)) {
    snprintf(errbuf, errbuf_len, "peer name too long");
    return (false);
  }
  if (local_registrar_mode(sp) &&
      peer_binding_find_const(sp, &sp->pending_call.target_user) == NULL) {
    snprintf(errbuf, errbuf_len, "waiting for peer REGISTER");
    sip_log("call deferred until %s session=0x%016llx",
      "peer REGISTER arrives",
      (unsigned long long)inp->session_nonce);
    return (true);
  }
  if (sp->ua != NULL && usipy_sip_ua_get_state(sp->ua) == USIPY_SIP_UA_STATE_IDLE &&
      start_pending_call(sp) != USIPY_SIP_TM_OK) {
    snprintf(errbuf, errbuf_len, "cannot start INVITE");
    sip_log("start_call immediate INVITE start failed");
    return (false);
  }
  return (true);
}

bool
netsim_sip_answer_pending_remote(struct netsim_sip *sp, char *errbuf,
  size_t errbuf_len)
{

  if (sp->pending_remote.valid == false) {
    snprintf(errbuf, errbuf_len, "no pending remote session");
    return (false);
  }
  if (sp->ua == NULL || usipy_sip_ua_get_state(sp->ua) == USIPY_SIP_UA_STATE_IDLE) {
    snprintf(errbuf, errbuf_len, "SIP UA not waiting on incoming INVITE");
    return (false);
  }
  return (answer_incoming_invite(sp, errbuf, errbuf_len));
}

void
netsim_sip_hangup(struct netsim_sip *sp)
{
  struct usipy_sip_ua_event ev = {
    .type = USIPY_SIP_UA_EVENT_DISCONNECT,
  };
  enum usipy_sip_ua_state uastate;
  bool need_ua_disconnect;
  size_t tx_index;

  uastate = (sp->ua != NULL) ? usipy_sip_ua_get_state(sp->ua) :
    USIPY_SIP_UA_STATE_IDLE;
  need_ua_disconnect = (uastate != USIPY_SIP_UA_STATE_IDLE &&
    uastate != USIPY_SIP_UA_STATE_DISCONNECTED);
  session_clear(&sp->disconnecting);
  if (sp->current.valid) {
    if (need_ua_disconnect) {
      sp->disconnecting = sp->current;
      sp->disconnecting_role = sp->current_role;
    }
    session_clear(&sp->current);
  } else if (sp->pending_call.valid) {
    if (need_ua_disconnect) {
      pending_call_to_session(&sp->pending_call, &sp->disconnecting);
      sp->disconnecting_role = USIPY_SIP_TM_ROLE_UAC;
    }
  } else if (sp->pending_remote.valid) {
    if (need_ua_disconnect) {
      sp->disconnecting = sp->pending_remote;
      sp->disconnecting_role = USIPY_SIP_TM_ROLE_UAS;
    }
    session_clear(&sp->pending_remote);
  }
  sp->pending_call.valid = false;
  sp->pending_remote.valid = false;
  if (!need_ua_disconnect)
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

bool
netsim_sip_registration_ready(const struct netsim_sip *sp)
{

  return ((!local_registrar_mode(sp) && sp->reg_active) ||
    (local_registrar_mode(sp) && peer_bindings_valid(sp)));
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

bool
netsim_sip_registration_ready(const struct netsim_sip *sp)
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
netsim_sip_start_call(struct netsim_sip *sp,
  const struct netsim_sip_start_call_in *inp, char *errbuf, size_t errbuf_len)
{

  (void)sp;
  (void)inp;
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
