/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_SIP_INTERNAL_H
#define NETSIM_SIP_INTERNAL_H

#include "netsim_sip.h"

#if NETSIM_PLATFORM_SUPPORTED

#include "microsippy/platforms/posix/usipy_tm_uac.h"
#include "microsippy/src/public/usipy_sip_msg.h"
#include "microsippy/src/public/usipy_sip_tm.h"
#include "microsippy/src/public/usipy_sip_ua.h"
#include "microsippy/src/public/usipy_sip_ua_utils.h"
#include "microsippy/src/public/usipy_str.h"

#define NETSIM_SIP_EVENTS 16
#define NETSIM_SIP_MAX_TX 32

struct netsim_sip_call_req {
  bool valid;
  uint64_t session_nonce;
  uint32_t local_stream_ssrc;
  int local_player;
  char target_user_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str target_user;
  char sdp_buf[NETSIM_SIP_SDP_BUFSIZE];
  struct usipy_str sdp;
};

struct netsim_sip_peer_binding {
  bool valid;
  uint64_t expires_at_ms;
  char peer_user_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str peer_user;
  char contact_uri_buf[NETSIM_SIP_CONTACT_BUFSIZE];
  struct usipy_str contact_uri;
  char target_host[NETSIM_SIP_HOST_BUFSIZE];
  struct usipy_sip_tm_addr target;
};

struct netsim_sip_register_binding {
  char peer_user[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str contact_uri;
  char target_host[NETSIM_SIP_HOST_BUFSIZE];
  uint16_t target_port;
  unsigned int expires;
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
  bool reg_active;
  bool ua_reset_needed;
  bool stop;
  bool error;
  struct netsim_sip_call_req pending_call;
  struct netsim_sip_call_req answered_call;
  struct netsim_sip_peer_binding peer_bindings[NETSIM_SIP_MAX_REGISTERED];
  struct netsim_sip_session pending_remote;
  struct netsim_sip_session current;
  enum usipy_sip_tm_role current_role;
  struct netsim_sip_session disconnecting;
  enum usipy_sip_tm_role disconnecting_role;
  struct netsim_sip_event events[NETSIM_SIP_EVENTS];
  size_t ev_head;
  size_t ev_tail;
  size_t ev_len;
};

bool local_registrar_mode(const struct netsim_sip *sp);
void event_push_registered(struct netsim_sip *sp,
  const struct usipy_str *peer_user);
void peer_binding_clear(struct netsim_sip_peer_binding *bp);
bool peer_bindings_valid(const struct netsim_sip *sp);
struct netsim_sip_peer_binding *peer_binding_find(struct netsim_sip *sp,
  const struct usipy_str *peer_user);
const struct netsim_sip_peer_binding *peer_binding_find_const(
  const struct netsim_sip *sp, const struct usipy_str *peer_user);
struct netsim_sip_peer_binding *peer_binding_store_slot(struct netsim_sip *sp,
  const struct usipy_str *peer_user);
int extract_register_binding(const struct usipy_msg *msg,
  const struct usipy_str *peer_user, struct netsim_sip_register_binding *outp);
int send_register_ok(struct netsim_sip *sp,
  const struct usipy_sip_tm_handle_incoming_in *hin, const struct usipy_msg *msg,
  const struct usipy_str *contact_urip, unsigned int expires);
int store_peer_registration(struct netsim_sip *sp,
  const struct usipy_sip_tm_handle_incoming_in *hin,
  const struct usipy_str *peer_user,
  const struct usipy_str *contact_urip, const char *target_host,
  uint16_t target_port, unsigned int expires);

#endif

#endif
