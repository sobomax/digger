#pragma once

struct usipy_msg_heap;
struct usipy_str;
struct usipy_tvpair;

#include "usipy_tvpair.h"

struct usipy_sip_hdr_nameaddr {
    struct usipy_str display_name;
    struct usipy_str addr_spec;
    int nparams;
    struct usipy_tvpair params[0];
};

union usipy_sip_hdr_parsed usipy_sip_hdr_nameaddr_parse(struct usipy_msg_heap *,
  const struct usipy_str *);
int usipy_sip_hdr_nameaddr_build(const union usipy_sip_hdr_parsed *, char *, size_t);
void usipy_sip_hdr_nameaddr_dump(const union usipy_sip_hdr_parsed *, const char *,
  const char *, const char *);
const struct usipy_str *usipy_sip_hdr_nameaddr_get_param(
  const struct usipy_sip_hdr_nameaddr *, const char *);
