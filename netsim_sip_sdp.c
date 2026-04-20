/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim_sip_sdp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NETSIM_SDP_PROTO "RTP/AVP"
#define NETSIM_SDP_FMT "96"
#define NETSIM_SDP_APP "digger"

static bool
parse_u64_line(const char *line, const char *prefix, uint64_t *vp)
{
  char *endp;
  unsigned long long v;

  if (strncmp(line, prefix, strlen(prefix)) != 0)
    return (false);
  v = strtoull(line + strlen(prefix), &endp, 10);
  if (endp == line + strlen(prefix) || (*endp != '\0' && *endp != '\r'))
    return (false);
  *vp = (uint64_t)v;
  return (true);
}

static bool
parse_ssrc_line(const char *line, uint32_t *vp)
{
  char *endp;
  unsigned long v;

  if (strncmp(line, "a=ssrc:", 7) != 0)
    return (false);
  v = strtoul(line + 7, &endp, 10);
  if (endp == line + 7 || *endp != ' ')
    return (false);
  *vp = (uint32_t)v;
  return (true);
}

static bool
parse_int_line(const char *line, const char *prefix, int *vp)
{
  char *endp;
  long v;

  if (strncmp(line, prefix, strlen(prefix)) != 0)
    return (false);
  v = strtol(line + strlen(prefix), &endp, 10);
  if (endp == line + strlen(prefix) || (*endp != '\0' && *endp != '\r'))
    return (false);
  *vp = (int)v;
  return (true);
}

bool
netsim_sip_sdp_build(char *buf, size_t buflen, const char *host,
  const char *port, const struct netsim_sdp_desc *descp)
{
  int blen;

  blen = snprintf(buf, buflen,
    "v=0\r\n"
    "o=- %llu 1 IN IP4 %s\r\n"
    "s=digger-netsim\r\n"
    "c=IN IP4 %s\r\n"
    "t=0 0\r\n"
    "m=application %s " NETSIM_SDP_PROTO " " NETSIM_SDP_FMT "\r\n"
    "a=rtpmap:" NETSIM_SDP_FMT " " NETSIM_SDP_APP "/11932\r\n"
    "a=x-digger-session:%llu\r\n"
    "a=ssrc:%u cname:digger-netsim\r\n"
    "a=x-digger-player:%d\r\n",
    (unsigned long long)descp->session_nonce, host, host, port,
    (unsigned long long)descp->session_nonce, descp->stream_ssrc,
    descp->player);
  return (blen > 0 && (size_t)blen < buflen);
}

bool
netsim_sip_sdp_parse(const char *buf, size_t len, struct netsim_sdp_desc *descp)
{
  char line[512];
  size_t off, line_len;
  bool have_host = false, have_port = false;
  bool have_session = false, have_ssrc = false, have_player = false;

  memset(descp, '\0', sizeof(*descp));
  for (off = 0; off < len;) {
    size_t start = off;

    while (off < len && buf[off] != '\n')
      off++;
    line_len = off - start;
    if (line_len > 0 && buf[start + line_len - 1] == '\r')
      line_len--;
    if (line_len >= sizeof(line))
      return (false);
    memcpy(line, buf + start, line_len);
    line[line_len] = '\0';
    if (off < len && buf[off] == '\n')
      off++;

    if (strncmp(line, "c=IN IP4 ", 9) == 0) {
      if (strlen(line + 9) >= sizeof(descp->conn_host))
        return (false);
      strcpy(descp->conn_host, line + 9);
      have_host = true;
      continue;
    }
    if (strncmp(line, "m=application ", 14) == 0) {
      const char *sp1, *sp2;
      size_t plen;

      sp1 = strchr(line + 14, ' ');
      if (sp1 == NULL)
        return (false);
      plen = (size_t)(sp1 - (line + 14));
      if (plen == 0 || plen >= sizeof(descp->media_port))
        return (false);
      memcpy(descp->media_port, line + 14, plen);
      descp->media_port[plen] = '\0';
      sp2 = strchr(sp1 + 1, ' ');
      if (sp2 == NULL || strncmp(sp1 + 1, NETSIM_SDP_PROTO,
            (size_t)(sp2 - (sp1 + 1))) != 0)
        return (false);
      have_port = true;
      continue;
    }
    if (parse_u64_line(line, "a=x-digger-session:", &descp->session_nonce)) {
      have_session = true;
      continue;
    }
    if (parse_ssrc_line(line, &descp->stream_ssrc)) {
      have_ssrc = true;
      continue;
    }
    if (parse_int_line(line, "a=x-digger-player:", &descp->player)) {
      have_player = true;
      continue;
    }
  }
  return (have_host && have_port && have_session && have_ssrc && have_player);
}
