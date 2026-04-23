/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_SIP_H
#define NETSIM_SIP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "def.h"
#include "microsippy/src/public/usipy_str.h"
#include "netsim_platform.h"

struct netsim_sip;

#define NETSIM_SIP_USER_BUFSIZE      64
#define NETSIM_SIP_PASSWORD_BUFSIZE  32
#define NETSIM_SIP_HOST_BUFSIZE      64
#define NETSIM_SIP_PORT_BUFSIZE     16
#define NETSIM_SIP_URI_BUFSIZE      256
#define NETSIM_SIP_CONTACT_BUFSIZE  256
#define NETSIM_SIP_SDP_BUFSIZE      768
#define NETSIM_SIP_LOG_BUFSIZE      1024
#define NETSIM_SIP_ERR_BUFSIZE      160
#define NETSIM_SIP_MAX_REGISTERED   50

struct netsim_sip_config {
  char username_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str username;
  char password_buf[NETSIM_SIP_PASSWORD_BUFSIZE];
  struct usipy_str password;
  char server_host_buf[NETSIM_SIP_HOST_BUFSIZE];
  struct usipy_str server_host;
  char server_port_buf[NETSIM_SIP_PORT_BUFSIZE];
  struct usipy_str server_port;
  char peer_user_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str peer_user;
};

struct netsim_sip_session {
  bool valid;
  uint64_t session_nonce;
  uint32_t local_stream_ssrc;
  uint32_t peer_stream_ssrc;
  int local_player;
  int remote_player;
  netsim_sockaddr_t media_addr;
};

enum netsim_sip_event_type {
  NETSIM_SIP_EVENT_NONE = 0,
  NETSIM_SIP_EVENT_REGISTERED,
  NETSIM_SIP_EVENT_REMOTE_START,
  NETSIM_SIP_EVENT_CONNECTED,
  NETSIM_SIP_EVENT_DISCONNECTED,
  NETSIM_SIP_EVENT_ERROR
};

struct netsim_sip_event {
  enum netsim_sip_event_type type;
  struct netsim_sip_session session;
  char peer_user_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str peer_user;
};

struct netsim_sip_start_call_in {
  uint64_t session_nonce;
  uint32_t local_stream_ssrc;
  int local_player;
  char peer_user_buf[NETSIM_SIP_USER_BUFSIZE];
  struct usipy_str peer_user;
};

struct netsim_sip *netsim_sip_create(const struct netsim_sip_config *cfgp,
  netsim_socket_t sock, const char *local_host, const char *local_port,
  char *errbuf, size_t errbuf_len);
void netsim_sip_config_rebind(struct netsim_sip_config *cfgp);
void netsim_sip_config_clone(struct netsim_sip_config *dstp,
  const struct netsim_sip_config *srcp);
void netsim_sip_destroy(struct netsim_sip *sp);
bool netsim_sip_run(struct netsim_sip *sp);
netsim_deadline_t netsim_sip_next_wakeup(const struct netsim_sip *sp,
  netsim_deadline_t fallback);
bool netsim_sip_handle_packet(struct netsim_sip *sp, const void *buf, size_t len,
  const netsim_sockaddr_t *peerp, const netsim_sockaddr_t *localp);
bool netsim_sip_pop_event(struct netsim_sip *sp, struct netsim_sip_event *evp);
bool netsim_sip_start_call(struct netsim_sip *sp,
  const struct netsim_sip_start_call_in *inp, char *errbuf, size_t errbuf_len);
bool netsim_sip_answer_pending_remote(struct netsim_sip *sp, char *errbuf,
  size_t errbuf_len);
void netsim_sip_hangup(struct netsim_sip *sp);
bool netsim_sip_packet_looks_like(const void *buf, size_t len);
bool netsim_sip_registration_ready(const struct netsim_sip *sp);

#endif
