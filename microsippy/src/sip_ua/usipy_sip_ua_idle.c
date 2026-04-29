#include <stddef.h>

#include "usipy_debug.h"
#include "usipy_sip_res.h"
#include "sip_ua/usipy_sip_ua_internal.h"

static int
usipy_sip_ua_idle_on_transaction(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    const struct usipy_sip_tm_tx *txp;
    const struct usipy_sip_tm_uas_response_params trying = {
      .status = &usipy_sip_res_trying,
    };
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(msg != NULL);

    rval = usipy_sip_ua_expect_transaction(uap, tx_index, USIPY_SIP_TM_ROLE_UAS,
      USIPY_SIP_METHOD_INVITE, &txp);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    rval = usipy_sip_tm_send_uas_response(uap->tm, tx_index, &trying);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    uap->role = USIPY_SIP_TM_ROLE_UAS;
    uap->tx_index = tx_index;
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_TRYING);
    usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_DIAL, tx_index, msg);
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_ua_idle_on_event(struct usipy_sip_ua *uap,
  const struct usipy_sip_ua_event *eventp, size_t *indexp)
{
    struct usipy_sip_tm_new_uac_tr_params tpp;
    size_t tx_index;
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(eventp != NULL);
    USIPY_DASSERT(indexp != NULL);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    if (eventp->type != USIPY_SIP_UA_EVENT_DIAL) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    rval = usipy_sip_ua_store_dialing_request(uap, &eventp->data.dial);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    USIPY_DASSERT(uap->dialingp != NULL);
    usipy_sip_ua_fill_new_uac_tr_params(uap->dialingp, &tpp);
    rval = usipy_sip_tm_new_uac_tr(uap->tm, &tpp, &tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        usipy_sip_ua_clear_dialing_request(uap);
        return (rval);
    }
    uap->role = USIPY_SIP_TM_ROLE_UAC;
    uap->tx_index = tx_index;
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_DIALING);
    *indexp = tx_index;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_ua_idle_on_tx_response(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

const struct usipy_sip_ua_state_ops usipy_sip_ua_idle_ops = {
    .on_transaction = usipy_sip_ua_idle_on_transaction,
    .on_event = usipy_sip_ua_idle_on_event,
    .on_tx_response = usipy_sip_ua_idle_on_tx_response,
};
