#pragma once

#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_ua.h"

#define USIPY_SIP_UA_HEAP_SIZE 1024u

struct usipy_sip_ua {
    struct usipy_sip_tm *tm;
    struct usipy_sip_dialog *dialogp;
    struct usipy_msg_heap heap;
    enum usipy_sip_ua_state state;
    enum usipy_sip_tm_role role;
    size_t tx_index;
    struct usipy_sip_ua_dialing_request *dialingp;
    usipy_sip_ua_emit_cb emit;
    void *emit_arg;
    struct usipy_sip_tm_addr default_target;
    int have_default_target;
};

struct usipy_sip_ua_dialing_request {
    struct usipy_sip_tm_request_id request_id;
    struct usipy_sip_tm_request_target request_target;
    struct usipy_sip_tm_addr request_target_addr;
    struct usipy_sip_tm_addr local;
    struct usipy_sip_tm_request_parties parties_by_username;
    uint32_t contact_expires;
    uint32_t invite_expires;
    struct usipy_sip_tm_uac_callbacks callbacks;
    struct usipy_sip_tm_request_payload payload;
    struct usipy_str request_call_id;
    struct usipy_str request_uri;
    struct usipy_str party_contact;
    struct usipy_str party_from;
    struct usipy_str party_to;
    struct usipy_str content_type;
    struct usipy_str body;
    struct usipy_str auth_username;
    struct usipy_str auth_password;
    struct usipy_str auth_qop;
    int auth_retry_started;
};

struct usipy_sip_ua_state_ops {
    int (*on_transaction)(struct usipy_sip_ua *, size_t, const struct usipy_msg *);
    int (*on_event)(struct usipy_sip_ua *, const struct usipy_sip_ua_event *, size_t *);
    int (*on_tx_response)(struct usipy_sip_ua *, size_t, const struct usipy_msg *);
};

extern const struct usipy_sip_ua_state_ops usipy_sip_ua_idle_ops;
extern const struct usipy_sip_ua_state_ops usipy_sip_ua_trying_ops;
extern const struct usipy_sip_ua_state_ops usipy_sip_ua_dialing_ops;
extern const struct usipy_sip_ua_state_ops usipy_sip_ua_connected_ops;
extern const struct usipy_sip_ua_state_ops usipy_sip_ua_disconnected_ops;

const struct usipy_sip_ua_state_ops *usipy_sip_ua_state_ops_get(enum usipy_sip_ua_state);
void usipy_sip_ua_transition(struct usipy_sip_ua *, enum usipy_sip_ua_state);
void usipy_sip_ua_emit_event(struct usipy_sip_ua *, enum usipy_sip_ua_emit_type, size_t,
  const struct usipy_msg *);
int usipy_sip_ua_expect_transaction(const struct usipy_sip_ua *, size_t,
  enum usipy_sip_tm_role, uint8_t, const struct usipy_sip_tm_tx **);
int usipy_sip_ua_store_dialing_request(struct usipy_sip_ua *,
  const struct usipy_sip_ua_dial_params *);
void usipy_sip_ua_fill_new_uac_tr_params(const struct usipy_sip_ua_dialing_request *,
  struct usipy_sip_tm_new_uac_tr_params *);
void usipy_sip_ua_clear_dialing_request(struct usipy_sip_ua *);
