#pragma once

#include <stddef.h>

#include "usipy_sip_tm.h"

struct usipy_msg;
struct usipy_sip_status;

typedef int (*usipy_sip_raw_send_cb)(void *arg, const void *buf, size_t len);

int usipy_sip_tm_send_simple_response(struct usipy_sip_tm *tm,
  const struct usipy_sip_tm_handle_incoming_in *hin, const struct usipy_msg *msg,
  const struct usipy_sip_status *statusp);
int usipy_sip_send_stateless_response(const struct usipy_msg *msg,
  const struct usipy_sip_status *statusp, usipy_sip_raw_send_cb send_cb,
  void *send_arg);
