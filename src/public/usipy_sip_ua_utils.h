#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "usipy_sip_tm.h"
#include "usipy_sip_ua.h"

struct usipy_msg;
struct usipy_str;

struct usipy_sip_register_state {
    int registering;
    int registered_once;
    int auth_retry_started;
    uint16_t status;
    uint32_t requested_expires;
    uint32_t next_cseq;
    unsigned int expires;
    uint64_t next_refresh_at_ms;
};

struct usipy_sip_register_start_params {
    struct usipy_sip_tm *tm;
    const struct usipy_str *call_id;
    const struct usipy_sip_tm_addr *target;
    const struct usipy_str *username;
    const struct usipy_sip_tm_uac_callbacks *callbacks;
};

enum usipy_sip_register_response_result {
    USIPY_SIP_REGISTER_RESPONSE_PENDING = 0,
    USIPY_SIP_REGISTER_RESPONSE_AUTH_RETRY,
    USIPY_SIP_REGISTER_RESPONSE_ESTABLISHED,
    USIPY_SIP_REGISTER_RESPONSE_FINAL,
    USIPY_SIP_REGISTER_RESPONSE_ERROR
};

bool usipy_sip_tm_addr_same(const struct usipy_sip_tm_addr *ap,
  const struct usipy_sip_tm_addr *bp);
bool usipy_sip_ua_request_targets_user(const struct usipy_msg *msg,
  const struct usipy_str *usernamep);
int usipy_sip_register_start(struct usipy_sip_register_state *statep,
  const struct usipy_sip_register_start_params *paramsp, size_t *indexp);
int usipy_sip_register_handle_response(struct usipy_sip_register_state *statep,
  struct usipy_sip_tm *tm, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg, const struct usipy_str *usernamep,
  const struct usipy_str *passwordp, const struct usipy_str *qopp,
  uint64_t now_ms, enum usipy_sip_register_response_result *resultp);
void usipy_sip_register_handle_timeout(struct usipy_sip_register_state *statep);
int usipy_sip_ua_schedule_refresh(unsigned int expires, uint64_t now_ms,
  uint64_t *next_refresh_at_msp);
int usipy_sip_ua_reset(struct usipy_sip_ua **uapp,
  const struct usipy_sip_ua_ctor_params *ctorp);
