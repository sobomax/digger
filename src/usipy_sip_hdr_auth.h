#pragma once

struct usipy_msg_heap;
struct usipy_str;

#include "usipy_tvpair.h"

struct usipy_sip_hdr_auth {
    struct usipy_str scheme;
    struct usipy_str realm;
    struct usipy_str nonce;
    struct usipy_str qop;
    struct usipy_str algorithm;
    struct usipy_str opaque;
    int nparams;
    struct usipy_tvpair params[0];
};

union usipy_sip_hdr_parsed usipy_sip_hdr_auth_parse(struct usipy_msg_heap *,
  const struct usipy_str *);
int usipy_sip_hdr_auth_build(const union usipy_sip_hdr_parsed *, char *, size_t);
void usipy_sip_hdr_auth_dump(const union usipy_sip_hdr_parsed *, const char *,
  const char *, const char *);
