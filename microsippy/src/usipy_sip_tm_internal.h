#pragma once

#include "public/usipy_sip_tm.h"

struct usipy_sip_tm_txi;
struct usipy_msg;
struct usipy_sip_tid;
struct usipy_sip_tm_dialog_request {
    struct usipy_sip_tm_request_id request_id;
    struct usipy_sip_tm_request_target request_target;
    struct usipy_sip_tm_addr request_target_addr;
    struct usipy_sip_tm_addr local;
    struct usipy_sip_tm_request_parties parties_by_uri;
    struct usipy_sip_tm_route_set route_set;
    struct usipy_sip_tm_dialog_tags dialog_tags;
    struct usipy_sip_tm_timer_policy timers;
    struct usipy_str call_id;
    struct usipy_str request_uri;
    struct usipy_str contact_uri;
    struct usipy_str from_uri;
    struct usipy_str to_uri;
    struct usipy_str local_tag;
    struct usipy_str remote_tag;
};

void usipy_sip_tm_dialog_request_get_params(
  const struct usipy_sip_tm_dialog_request *,
  struct usipy_sip_tm_new_in_dialog_transaction_params *);
int usipy_sip_tm_init_uac_dialog_request_params(const struct usipy_sip_tm *, size_t,
  const struct usipy_msg *, uint8_t, struct usipy_msg_heap *,
  struct usipy_sip_tm_dialog_request *);
int usipy_sip_tm_init_uas_dialog_request_params(const struct usipy_sip_tm *, size_t,
  uint8_t, struct usipy_msg_heap *,
  struct usipy_sip_tm_dialog_request *);
int usipy_sip_tm_apply_uac_2xx_ack_dialog(const struct usipy_sip_tm *, size_t,
  const struct usipy_msg *, struct usipy_sip_tm_txi *);
int usipy_sip_tm_tid_matches_tx(const struct usipy_sip_tid *,
  const struct usipy_sip_tm_tx *);
struct usipy_sip_tm_txi *usipy_sip_tm_alloc_slot(struct usipy_sip_tm *, size_t *);
void usipy_sip_tm_timer_policy_resolve(struct usipy_sip_tm_timer_policy *,
  const struct usipy_sip_tm_timer_policy *);
void usipy_sip_tm_tx_fini(struct usipy_sip_tm_txi *);
int usipy_sip_tm_uac_run(struct usipy_sip_tm_txi *, size_t,
  const struct usipy_sip_tm *, const struct usipy_sip_tm_run_in *,
  struct usipy_sip_tm_run_out *);
int usipy_sip_tm_uas_run(struct usipy_sip_tm_txi *, size_t,
  const struct usipy_sip_tm *, const struct usipy_sip_tm_run_in *,
  struct usipy_sip_tm_run_out *);
int usipy_sip_tm_handle_incoming_response(
  const struct usipy_sip_tm_handle_incoming_in *, struct usipy_msg *,
  const struct usipy_sip_tid *, struct usipy_sip_tm_handle_incoming_out *);
int usipy_sip_tm_handle_incoming_request(
  const struct usipy_sip_tm_handle_incoming_in *, struct usipy_msg *,
  const struct usipy_sip_tid *, struct usipy_sip_tm_handle_incoming_out *);

/* UAS core reliability for accepted INVITEs (RFC 3261, 13.3.1.4).
 * The transaction retains the 2xx until Timer L, but its owner (the dialog)
 * schedules the retransmissions, and is told of each send, of the ACK, and
 * of Timer L (timeout() returns 1 if it ended the session for want of ACK).
 * The owner is known by its arg; the calls taking one fail with
 * USIPY_SIP_TM_ERR_NOT_FOUND for an index that isn't an active transaction
 * of that owner (reaped, reused). */
struct usipy_sip_tm_uas_owner {
    void (*sent)(void *, uint64_t);
    void (*acked)(void *);
    int (*timeout)(void *);
};

int usipy_sip_tm_uas_get_callbacks(const struct usipy_sip_tm *, size_t,
  struct usipy_sip_tm_uas_callbacks *);
int usipy_sip_tm_uas_set_owner(struct usipy_sip_tm *, size_t,
  const struct usipy_sip_tm_uas_owner *, void *);
int usipy_sip_tm_uas_clear_owner(struct usipy_sip_tm *, size_t, const void *);
int usipy_sip_tm_uas_send_at(struct usipy_sip_tm *, size_t, const void *,
  uint64_t);
const struct usipy_sip_tm_tx *usipy_sip_tm_uas_owned_tx(
  const struct usipy_sip_tm *, size_t, const void *);
