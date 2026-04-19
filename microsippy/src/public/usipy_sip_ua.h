#pragma once

#include <stddef.h>
#include <stdint.h>

#include "usipy_sip_dialog.h"
#include "usipy_sip_tm.h"

struct usipy_sip_ua;

enum usipy_sip_ua_state {
    USIPY_SIP_UA_STATE_IDLE = 0,
    USIPY_SIP_UA_STATE_TRYING,
    USIPY_SIP_UA_STATE_DIALING,
    USIPY_SIP_UA_STATE_CONNECTED,
    USIPY_SIP_UA_STATE_DISCONNECTED
};

enum usipy_sip_ua_event_type {
    USIPY_SIP_UA_EVENT_DIAL = 0,
    USIPY_SIP_UA_EVENT_CONNECT,
    USIPY_SIP_UA_EVENT_DISCONNECT
};

enum usipy_sip_ua_emit_type {
    USIPY_SIP_UA_EMIT_DIAL = 0,
    USIPY_SIP_UA_EMIT_CONNECT,
    USIPY_SIP_UA_EMIT_DISCONNECT
};

struct usipy_sip_ua_credentials {
    struct usipy_str username;
    struct usipy_str password;
    struct usipy_str qop;
};

struct usipy_sip_ua_dial_params {
    struct usipy_sip_tm_new_uac_tr_params request;
    struct usipy_sip_ua_credentials auth;
};

struct usipy_sip_ua_event {
    enum usipy_sip_ua_event_type type;
    union {
        struct usipy_sip_ua_dial_params dial;
        struct usipy_sip_tm_uas_response_params response;
    } data;
};

struct usipy_sip_ua_emit {
    enum usipy_sip_ua_emit_type type;
    enum usipy_sip_ua_state state;
    enum usipy_sip_tm_role role;
    size_t transaction_index;
    const struct usipy_msg *message;
    struct usipy_str body;
};

typedef void (*usipy_sip_ua_emit_cb)(void *, const struct usipy_sip_ua_emit *);

struct usipy_sip_ua_ctor_params {
    struct usipy_sip_tm *tm;
    usipy_sip_ua_emit_cb emit;
    void *emit_arg;
};

struct usipy_sip_ua *usipy_sip_ua_ctor(const struct usipy_sip_ua_ctor_params *);
void usipy_sip_ua_dtor(struct usipy_sip_ua *);
enum usipy_sip_ua_state usipy_sip_ua_get_state(const struct usipy_sip_ua *);
int usipy_sip_ua_matches_transaction(const struct usipy_sip_ua *, const struct usipy_msg *);
int usipy_sip_ua_on_event(struct usipy_sip_ua *, const struct usipy_sip_ua_event *,
  size_t *);
int usipy_sip_ua_on_transaction(struct usipy_sip_ua *, size_t, const struct usipy_msg *);
int usipy_sip_ua_on_tx_response(struct usipy_sip_ua *, size_t, const struct usipy_msg *);
