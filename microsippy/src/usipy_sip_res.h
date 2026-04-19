#pragma once

struct usipy_msg_heap;
struct usipy_msg;
struct usipy_sip_status;
struct usipy_str;

extern const struct usipy_sip_status usipy_sip_res_trying;
extern const struct usipy_sip_status usipy_sip_res_ringing;
extern const struct usipy_sip_status usipy_sip_res_ok;
extern const struct usipy_sip_status usipy_sip_res_not_impl;
extern const struct usipy_sip_status usipy_sip_res_unauth;
extern const struct usipy_sip_status usipy_sip_res_busy_here;
extern const struct usipy_sip_status usipy_sip_res_req_term;

struct usipy_msg *usipy_sip_res_ctor_fromreq(const struct usipy_msg *,
  const struct usipy_sip_status *);
struct usipy_msg *usipy_sip_res_build_fromreq_tagged(struct usipy_msg_heap *,
  const struct usipy_msg *, const struct usipy_sip_status *,
  const struct usipy_str *);
