#include <stddef.h>

#include "sip_ua/usipy_sip_ua_internal.h"

static int
usipy_sip_ua_disconnected_on_transaction(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

static int
usipy_sip_ua_disconnected_on_event(struct usipy_sip_ua *uap,
  const struct usipy_sip_ua_event *eventp, size_t *indexp)
{
    (void)uap;
    (void)eventp;
    if (indexp != NULL) {
        *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    }
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

static int
usipy_sip_ua_disconnected_on_tx_response(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

const struct usipy_sip_ua_state_ops usipy_sip_ua_disconnected_ops = {
    .on_transaction = usipy_sip_ua_disconnected_on_transaction,
    .on_event = usipy_sip_ua_disconnected_on_event,
    .on_tx_response = usipy_sip_ua_disconnected_on_tx_response,
};
