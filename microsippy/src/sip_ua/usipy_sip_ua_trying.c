#include <stddef.h>

#include "usipy_debug.h"
#include "usipy_sip_res.h"
#include "sip_ua/usipy_sip_ua_internal.h"

static int
usipy_sip_ua_trying_on_transaction(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

static int
usipy_sip_ua_trying_on_event(struct usipy_sip_ua *uap,
  const struct usipy_sip_ua_event *eventp, size_t *indexp)
{
    const struct usipy_sip_tm_uas_response_params *rpp;
    struct usipy_sip_tm_uas_response_params def_resp;
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(eventp != NULL);
    USIPY_DASSERT(indexp != NULL);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    if (eventp->type == USIPY_SIP_UA_EVENT_CONNECT) {
        if (eventp->data.response.status.code == 0) {
            def_resp = eventp->data.response;
            def_resp.status = usipy_sip_res_ok;
            rpp = &def_resp;
        } else {
            rpp = &eventp->data.response;
        }
        if (rpp->status.code < 200 || rpp->status.code > 299) {
            return (USIPY_SIP_TM_ERR_INVAL);
        }
        uap->dialogp = usipy_sip_dialog_uas_ctor(uap->tm, uap->tx_index, rpp);
        if (uap->dialogp == NULL) {
            return (USIPY_SIP_TM_ERR_UNSUPPORTED);
        }
        usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_CONNECTED);
        usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_CONNECT, uap->tx_index, NULL);
        *indexp = uap->tx_index;
        return (USIPY_SIP_TM_OK);
    }
    if (eventp->type != USIPY_SIP_UA_EVENT_DISCONNECT) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (eventp->data.response.status.code == 0) {
        def_resp = eventp->data.response;
        def_resp.status = usipy_sip_res_req_term;
        rpp = &def_resp;
    } else {
        rpp = &eventp->data.response;
    }
    if (rpp->status.code < 300) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    rval = usipy_sip_tm_send_uas_response(uap->tm, uap->tx_index, rpp);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_DISCONNECTED);
    usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_DISCONNECT, uap->tx_index, NULL);
    *indexp = uap->tx_index;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_ua_trying_on_tx_response(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

const struct usipy_sip_ua_state_ops usipy_sip_ua_trying_ops = {
    .on_transaction = usipy_sip_ua_trying_on_transaction,
    .on_event = usipy_sip_ua_trying_on_event,
    .on_tx_response = usipy_sip_ua_trying_on_tx_response,
};
