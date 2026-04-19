#pragma once

#include <stddef.h>
#include <stdint.h>

#include "usipy_str.h"
#include "usipy_sip_hdr_types.h"
#include "usipy_sip_method_types.h"
#include "usipy_sip_msg.h"

struct usipy_sip_tm;
struct usipy_sip_hdr_auth;
struct usipy_sip_hdr_authz;
struct usipy_sip_tm_handle_incoming_in;

enum usipy_sip_tm_role {
    USIPY_SIP_TM_ROLE_UAC = 0,
    USIPY_SIP_TM_ROLE_UAS = 1
};

enum usipy_sip_tm_state {
    USIPY_SIP_TM_STATE_NULL = 0,
    USIPY_SIP_TM_STATE_CALLING,
    USIPY_SIP_TM_STATE_TRYING,
    USIPY_SIP_TM_STATE_PROCEEDING,
    USIPY_SIP_TM_STATE_COMPLETED,
    USIPY_SIP_TM_STATE_CONFIRMED,
    USIPY_SIP_TM_STATE_TERMINATED
};

enum usipy_sip_tm_transport {
    USIPY_SIP_TM_TRANSPORT_UNSPEC = 0,
    USIPY_SIP_TM_TRANSPORT_UDP,
    USIPY_SIP_TM_TRANSPORT_TCP,
    USIPY_SIP_TM_TRANSPORT_TLS,
    USIPY_SIP_TM_TRANSPORT_SCTP
};

enum usipy_sip_tm_match_kind {
    USIPY_SIP_TM_MATCH_NONE = 0,
    USIPY_SIP_TM_MATCH_EXISTING,
    USIPY_SIP_TM_MATCH_NEW
};

enum usipy_sip_tm_event {
    USIPY_SIP_TM_EVENT_NONE = 0,
    USIPY_SIP_TM_EVENT_REQUEST_RX,
    USIPY_SIP_TM_EVENT_REQUEST_RETRANSMIT,
    USIPY_SIP_TM_EVENT_ACK_RX,
    USIPY_SIP_TM_EVENT_RESPONSE_1XX,
    USIPY_SIP_TM_EVENT_RESPONSE_FINAL,
    USIPY_SIP_TM_EVENT_RETRANSMIT_DUE,
    USIPY_SIP_TM_EVENT_TIMEOUT,
    USIPY_SIP_TM_EVENT_TERMINATED
};

enum usipy_sip_tm_error {
    USIPY_SIP_TM_OK = 0,
    USIPY_SIP_TM_ERR_PARSE = -1,
    USIPY_SIP_TM_ERR_BADMSG = -2,
    USIPY_SIP_TM_ERR_NOT_FOUND = -3,
    USIPY_SIP_TM_ERR_UNSUPPORTED = -4,
    USIPY_SIP_TM_ERR_INVAL = -5,
    USIPY_SIP_TM_ERR_NOMEM = -6,
    USIPY_SIP_TM_ERR_NOSPC = -7
};

enum usipy_sip_tm_uac_timeout_id {
    USIPY_SIP_TM_TIMEOUT_NONE = 0,
    USIPY_SIP_TM_TIMEOUT_PR,
    USIPY_SIP_TM_TIMEOUT_FR
};

enum usipy_sip_tm_timer_kind {
    USIPY_SIP_TM_TIMER_NONE = 0,
    USIPY_SIP_TM_TIMER_A,
    USIPY_SIP_TM_TIMER_B,
    USIPY_SIP_TM_TIMER_D,
    USIPY_SIP_TM_TIMER_E,
    USIPY_SIP_TM_TIMER_F,
    USIPY_SIP_TM_TIMER_H,
    USIPY_SIP_TM_TIMER_I,
    USIPY_SIP_TM_TIMER_J,
    USIPY_SIP_TM_TIMER_K
};

struct usipy_sip_tm_addr {
    int af;
    uint16_t port;
    enum usipy_sip_tm_transport transport;
    struct usipy_str host;
};

/*
 * These string slices usually alias storage owned by request/response.
 * Callers that keep a transaction alive must keep the referenced messages alive.
 */
struct usipy_sip_tm_id {
    uint32_t hash;
    struct usipy_str branch;
    struct usipy_str call_id;
    struct usipy_str from_tag;
    uint32_t cseq;
    uint8_t method_type;
    uint8_t _pad[3];
};

struct usipy_sip_tm_timer_policy {
    uint32_t t1_ms;
    uint32_t t2_ms;
    uint32_t t4_ms;
    uint32_t timer_a_ms;
    uint32_t timer_b_ms;
    uint32_t timer_d_ms;
    uint32_t timer_e_ms;
    uint32_t timer_f_ms;
    uint32_t timer_j_ms;
    uint32_t timer_k_ms;
};

#define USIPY_SIP_TM_TIMER_POLICY_RFC3261 \
    ((struct usipy_sip_tm_timer_policy){ \
      .t1_ms = 500, \
      .t2_ms = 4000, \
      .t4_ms = 5000, \
      .timer_a_ms = 0, \
      .timer_b_ms = 0, \
      .timer_d_ms = 0, \
      .timer_e_ms = 0, \
      .timer_f_ms = 0, \
      .timer_j_ms = 0, \
      .timer_k_ms = 0 \
    })

#define USIPY_SIP_TM_TIMER_POLICY_DEFAULT USIPY_SIP_TM_TIMER_POLICY_RFC3261

#define USIPY_SIP_TM_F_RELIABLE_TRANSPORT 0x00000001u
#define USIPY_SIP_TM_F_TERMINATED         0x00000002u

#define USIPY_SIP_TM_TX_INDEX_NONE  ((size_t)-1)
#define USIPY_SIP_TM_TIME_NONE      UINT64_MAX

struct usipy_sip_tm_timer {
    enum usipy_sip_tm_timer_kind type;
    uint32_t value_ms;
    uint64_t due_at_ms;
};

struct usipy_sip_tm_outbound {
    struct usipy_sip_tm_addr target;
    struct usipy_str raw;
    uint64_t next_send_at_ms;
};

struct usipy_sip_tm_common {
    uint32_t flags;
    struct usipy_sip_tm_id id;
    struct usipy_sip_tm_addr peer;
    struct usipy_sip_tm_addr local;
    struct usipy_sip_tm_outbound outbound;
    uint8_t retransmit_count;
    uint64_t created_at_ms;
    uint64_t updated_at_ms;
    struct usipy_sip_tm_timer timer;
    struct usipy_sip_tm_timer_policy timers;
};

struct usipy_sip_tm_uac {
    uint16_t last_status_code;
    uint8_t response_class;
    uint8_t _pad0;
};

struct usipy_sip_tm_uas {
    uint16_t last_status_code;
    uint8_t request_retransmits;
    uint8_t _pad0;
};

struct usipy_sip_tm_tx {
    enum usipy_sip_tm_role role;
    enum usipy_sip_tm_state state;
    struct usipy_sip_tm_common common;
    union {
        struct usipy_sip_tm_uac uac;
        struct usipy_sip_tm_uas uas;
    } role_data;
};

typedef int (*usipy_sip_tm_send_to_cb)(void *, size_t,
  const struct usipy_sip_tm_tx *, const struct usipy_sip_tm_outbound *);
typedef void (*usipy_sip_tm_uac_response_cb)(void *, size_t,
  const struct usipy_sip_tm_tx *, const struct usipy_msg *);
typedef void (*usipy_sip_tm_uac_timeout_cb)(void *, size_t,
  const struct usipy_sip_tm_tx *, enum usipy_sip_tm_uac_timeout_id);
typedef void (*usipy_sip_tm_uas_cancel_cb)(void *, size_t,
  const struct usipy_sip_tm_tx *, const struct usipy_msg *);
typedef void (*usipy_sip_tm_uas_no_ack_cb)(void *, size_t,
  const struct usipy_sip_tm_tx *);
typedef void (*usipy_sip_tm_incoming_request_cb)(void *,
  const struct usipy_sip_tm_handle_incoming_in *, const struct usipy_msg *);

struct usipy_sip_tm_uac_callbacks {
    void *arg;
    usipy_sip_tm_uac_response_cb response;
    usipy_sip_tm_uac_timeout_cb timeout;
};

struct usipy_sip_tm_uas_callbacks {
    void *arg;
    usipy_sip_tm_uas_cancel_cb cancel;
    usipy_sip_tm_uas_no_ack_cb no_ack;
};

struct usipy_sip_tm_callbacks {
    void *arg;
    usipy_sip_tm_incoming_request_cb incoming_request;
};

struct usipy_sip_tm_id_policy_in {
    size_t transaction_index;
    uint32_t cseq;
    uint8_t method_type;
    uint8_t _pad0[3];
};

struct usipy_sip_tm_id_policy_out {
    struct usipy_str branch;
    struct usipy_str local_tag;
};

typedef int (*usipy_sip_tm_id_policy_cb)(void *, struct usipy_msg_heap *,
  const struct usipy_sip_tm_id_policy_in *,
  struct usipy_sip_tm_id_policy_out *);

struct usipy_sip_tm_id_policy {
    void *arg;
    usipy_sip_tm_id_policy_cb cb;
};

enum usipy_sip_tm_extra_header_kind {
    USIPY_SIP_TM_EH_RAW = 0,
    USIPY_SIP_TM_EH_PARSED = 1
};

struct usipy_sip_tm_extra_header {
    uint8_t hf_type;
    uint8_t value_kind;
    uint8_t _pad0[2];
    struct usipy_str value;
    const void *parsed;
};

struct usipy_sip_tm_request_id {
    struct usipy_str call_id;
    uint32_t cseq;
    uint8_t method_type;
    uint8_t _pad0[3];
};

struct usipy_sip_tm_request_target {
    struct usipy_str request_uri;
    struct usipy_sip_tm_addr target;
};

struct usipy_sip_tm_request_parties {
    struct usipy_str contact;
    struct usipy_str from;
    struct usipy_str to;
};

struct usipy_sip_tm_request_payload {
    struct usipy_str content_type;
    struct usipy_str body;
};

struct usipy_sip_tm_route_set {
    const struct usipy_str *routes;
    size_t nroutes;
};

struct usipy_sip_tm_dialog_tags {
    struct usipy_str local_tag;
    struct usipy_str remote_tag;
};

struct usipy_sip_tm_new_uac_tr_params {
    struct usipy_sip_tm_request_id request_id;
    struct usipy_sip_tm_request_target request_target;
    struct usipy_sip_tm_request_parties parties_by_username;
    uint32_t contact_expires;
    uint32_t invite_expires;
    struct usipy_str content_type;
    struct usipy_str body;
    struct usipy_sip_tm_uac_callbacks callbacks;
};

struct usipy_sip_tm_new_uas_tr_params {
    const struct usipy_msg *request;
    struct usipy_sip_tm_timer_policy timers;
    struct usipy_sip_tm_addr peer;
    struct usipy_sip_tm_addr local;
    struct usipy_sip_tm_uas_callbacks callbacks;
};

struct usipy_sip_tm_uas_response_params {
    struct usipy_sip_status status;
    struct usipy_str content_type;
    struct usipy_str body;
    struct usipy_sip_tm_uas_callbacks callbacks;
};

struct usipy_sip_tm_new_in_dialog_transaction_params {
    struct usipy_sip_tm_request_id request_id;
    struct usipy_sip_tm_request_target request_target;
    struct usipy_sip_tm_request_parties parties_by_uri;
    struct usipy_sip_tm_route_set route_set;
    struct usipy_sip_tm_dialog_tags dialog_tags;
    struct usipy_sip_tm_timer_policy timers;
    struct usipy_sip_tm_uac_callbacks callbacks;
};

struct usipy_sip_tm_ctor_params {
    int sock;
    enum usipy_sip_tm_transport transport;
    size_t max_transactions;
    struct usipy_sip_tm_callbacks callbacks;
    struct usipy_sip_tm_id_policy id_policy;
};

struct usipy_sip_tm_handle_incoming_in {
    uint64_t now_ms;
    struct usipy_sip_tm *tm;
    struct usipy_sip_tm_timer_policy timers;
    struct usipy_sip_tm_addr peer;
    struct usipy_sip_tm_addr local;
    const char *buf;
    size_t len;
};

struct usipy_sip_tm_handle_incoming_out {
    int error;
    int consumed;
    enum usipy_sip_tm_match_kind match_kind;
    enum usipy_sip_tm_event event;
    size_t transaction_index;
    struct usipy_msg *message;
};

struct usipy_sip_tm_timer_cb_out {
    int error;
    enum usipy_sip_tm_event event;
    size_t transaction_index;
    const struct usipy_msg *message;
};

struct usipy_sip_tm_run_in {
    uint64_t now_ms;
    struct usipy_sip_tm *tm;
    usipy_sip_tm_send_to_cb send_to;
    void *send_to_arg;
};

struct usipy_sip_tm_run_out {
    int error;
    uint64_t next_run_at_ms;
    size_t nsent;
    size_t ntimeouts;
};

struct usipy_sip_tm *usipy_sip_tm_ctor(
  const struct usipy_sip_tm_ctor_params *);
void usipy_sip_tm_dtor(struct usipy_sip_tm *);
size_t usipy_sip_tm_nactive(const struct usipy_sip_tm *);
const struct usipy_sip_tm_tx *usipy_sip_tm_get_transaction(
  const struct usipy_sip_tm *, size_t);
int usipy_sip_tm_drop_transaction(struct usipy_sip_tm *, size_t);
int usipy_sip_tm_set_timer_policy(struct usipy_sip_tm *, size_t,
  const struct usipy_sip_tm_timer_policy *);

int usipy_sip_tm_new_uac_tr(struct usipy_sip_tm *,
  const struct usipy_sip_tm_new_uac_tr_params *, size_t *);
int usipy_sip_tm_new_uas_tr(struct usipy_sip_tm *,
  const struct usipy_sip_tm_new_uas_tr_params *, size_t *);
int usipy_sip_tm_new_in_dialog_transaction(struct usipy_sip_tm *,
  const struct usipy_sip_tm_new_in_dialog_transaction_params *, size_t *);
int usipy_sip_tm_gen_authz_hf(const struct usipy_sip_tm *, size_t, uint8_t,
  struct usipy_msg_heap *, const struct usipy_sip_hdr_auth *,
  const struct usipy_str *, const struct usipy_str *, const struct usipy_str *,
  const struct usipy_str *, struct usipy_sip_tm_extra_header *);
int usipy_sip_tm_send_uas_response(struct usipy_sip_tm *, size_t,
  const struct usipy_sip_tm_uas_response_params *);
int usipy_sip_tm_uas_tr_cancelled(struct usipy_sip_tm *,
  const struct usipy_msg *, size_t,
  const struct usipy_sip_tm_uas_response_params *);
int usipy_sip_tm_next_transaction(struct usipy_sip_tm *, size_t,
  const struct usipy_sip_tm_request_payload *,
  const struct usipy_sip_tm_extra_header *, size_t);
int usipy_sip_tm_cancel(struct usipy_sip_tm *, size_t);
int usipy_sip_tm_run(struct usipy_sip_tm_run_in *,
  struct usipy_sip_tm_run_out *);

int usipy_sip_tm_handle_incoming(const struct usipy_sip_tm_handle_incoming_in *,
  struct usipy_sip_tm_handle_incoming_out *);
