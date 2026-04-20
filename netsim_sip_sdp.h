/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef NETSIM_SIP_SDP_H
#define NETSIM_SIP_SDP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct netsim_sdp_desc {
  uint64_t session_nonce;
  uint32_t stream_ssrc;
  int player;
  char conn_host[256];
  char media_port[16];
};

bool netsim_sip_sdp_build(char *buf, size_t buflen, const char *host,
  const char *port, const struct netsim_sdp_desc *descp);
bool netsim_sip_sdp_parse(const char *buf, size_t len,
  struct netsim_sdp_desc *descp);

#endif
