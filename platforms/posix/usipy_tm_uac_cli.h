#pragma once

#include <stdint.h>
#include <sys/socket.h>

#include "public/microsippy.h"

int usipy_tm_uac_cli_parse_u32(const char *, uint32_t, uint32_t, uint32_t *);
int usipy_tm_uac_cli_parse_target(const char *, uint16_t, struct sockaddr_storage *,
  socklen_t *, struct usipy_sip_tm_addr *);
int usipy_tm_uac_cli_format_uri_host(char *, size_t, const struct usipy_sip_tm_addr *);
