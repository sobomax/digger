#include <stddef.h>

#include "usipy_debug.h"
#include "usipy_sip_res.h"
#include "sip_ua/usipy_sip_ua_internal.h"

static int
usipy_sip_ua_connected_on_transaction(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(msg != NULL);

    if (uap->dialogp == NULL) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    rval = usipy_sip_dialog_handle_uas_transaction(uap->dialogp, tx_index, msg);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    uap->tx_index = tx_index;
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_DISCONNECTED);
    usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_DISCONNECT, tx_index, msg);
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_ua_connected_on_event(struct usipy_sip_ua *uap,
  const struct usipy_sip_ua_event *eventp, size_t *indexp)
{
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(eventp != NULL);
    USIPY_DASSERT(indexp != NULL);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    if (eventp->type != USIPY_SIP_UA_EVENT_DISCONNECT || uap->dialogp == NULL) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    rval = usipy_sip_dialog_end(uap->dialogp, NULL, indexp);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_DISCONNECTED);
    usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_DISCONNECT, *indexp, NULL);
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_ua_connected_on_tx_response(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

const struct usipy_sip_ua_state_ops usipy_sip_ua_connected_ops = {
    .on_transaction = usipy_sip_ua_connected_on_transaction,
    .on_event = usipy_sip_ua_connected_on_event,
    .on_tx_response = usipy_sip_ua_connected_on_tx_response,
};
