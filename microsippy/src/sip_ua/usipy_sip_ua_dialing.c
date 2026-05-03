#include <stddef.h>

#include "public/usipy_str.h"
#include "public/usipy_msg_heap.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_debug.h"
#include "sip_ua/usipy_sip_ua_internal.h"

static int
usipy_sip_ua_dialing_auth_retry(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    const struct usipy_sip_ua_dialing_request *dp;
    const struct usipy_sip_hdr_auth *challengep = NULL;
    struct usipy_sip_tm_extra_header auth_hdr;
    struct usipy_msg_heap auth_heap;
    char auth_storage[512];
    size_t auth_cpts[4];
    struct usipy_msg *cmsg = (struct usipy_msg *)msg;
    struct usipy_sip_hdr_match *hdr_matchp;
    const struct usipy_sip_tm_request_payload *payloadp;
    const struct usipy_str *effective_qopp;
    uint64_t parse_mask;
    uint8_t auth_hf_type;
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(uap->dialingp != NULL);

    dp = uap->dialingp;
    payloadp = (dp->body.l != 0) ? &dp->payload : NULL;
    auth_hdr = (struct usipy_sip_tm_extra_header){0};
    USIPY_DASSERT(dp->payload.content_type == &dp->content_type);
    USIPY_DASSERT(dp->payload.body == &dp->body);
    if (dp->auth_retry_started ||
      dp->auth_username.l == 0 || dp->auth_password.l == 0) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    switch (msg->sline.parsed.sl.status.code) {
    case 401:
        parse_mask = USIPY_HFT_MASK(USIPY_HF_WWWAUTHENTICATE);
        auth_hf_type = USIPY_HF_AUTHORIZATION;
        break;

    case 407:
        parse_mask = USIPY_HFT_MASK(USIPY_HF_PROXYAUTHENTICATE);
        auth_hf_type = USIPY_HF_PROXYAUTHORIZATION;
        break;

    default:
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    hdr_matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
    *hdr_matchp = (struct usipy_sip_hdr_match){.hdrslen = 1};
    if (usipy_sip_msg_parse_hdrs_get(cmsg, parse_mask, 0, hdr_matchp) != 0 ||
      hdr_matchp->nhdrs == 0) {
        return (USIPY_SIP_TM_ERR_PARSE);
    }
    challengep = hdr_matchp->hdrsp[0]->parsed.auth;
    if (challengep == NULL) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    effective_qopp = (challengep->qop.l != 0 ? &dp->auth_qop : NULL);
    usipy_msg_heap_init(&auth_heap, auth_storage, sizeof(auth_storage),
      auth_cpts, sizeof(auth_cpts) / sizeof(auth_cpts[0]));
    rval = usipy_sip_tm_gen_authz_hf(uap->tm, tx_index, auth_hf_type, &auth_heap,
      challengep, &dp->auth_username, &dp->auth_password,
      (dp->body.l != 0) ? &dp->body : NULL, effective_qopp,
      &auth_hdr);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    rval = usipy_sip_tm_next_transaction(uap->tm, tx_index,
      payloadp, &auth_hdr, 1);
    if (rval == USIPY_SIP_TM_OK) {
        uap->dialingp->auth_retry_started = 1;
    }
    return (rval);
}

static int
usipy_sip_ua_dialing_on_transaction(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    (void)uap;
    (void)tx_index;
    (void)msg;
    return (USIPY_SIP_TM_ERR_UNSUPPORTED);
}

static int
usipy_sip_ua_dialing_on_event(struct usipy_sip_ua *uap,
  const struct usipy_sip_ua_event *eventp, size_t *indexp)
{
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(eventp != NULL);
    USIPY_DASSERT(indexp != NULL);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    if (eventp->type != USIPY_SIP_UA_EVENT_DISCONNECT) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    rval = usipy_sip_tm_cancel(uap->tm, uap->tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    usipy_sip_ua_clear_dialing_request(uap);
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_DISCONNECTED);
    usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_DISCONNECT, uap->tx_index, NULL);
    *indexp = uap->tx_index;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_ua_dialing_on_tx_response(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    const struct usipy_sip_tm_tx *txp;
    unsigned int scode;
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(msg != NULL);

    rval = usipy_sip_ua_expect_transaction(uap, tx_index, USIPY_SIP_TM_ROLE_UAC,
      USIPY_SIP_METHOD_INVITE, &txp);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    (void)txp;
    if (tx_index != uap->tx_index || msg->kind != USIPY_SIP_MSG_RES) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    scode = msg->sline.parsed.sl.status.code;
    if (scode < 200) {
        return (USIPY_SIP_TM_OK);
    }
    if (scode == 401 || scode == 407) {
        rval = usipy_sip_ua_dialing_auth_retry(uap, tx_index, msg);
        if (rval == USIPY_SIP_TM_OK) {
            return (USIPY_SIP_TM_OK);
        }
    }
    if (scode < 300) {
        if (uap->dialogp == NULL) {
            uap->dialogp = usipy_sip_dialog_uac_ctor(uap->tm, tx_index, msg);
            if (uap->dialogp == NULL) {
                return (USIPY_SIP_TM_ERR_BADMSG);
            }
        }
        usipy_sip_ua_clear_dialing_request(uap);
        usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_CONNECTED);
        usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_CONNECT, tx_index, msg);
        return (USIPY_SIP_TM_OK);
    }
    usipy_sip_ua_clear_dialing_request(uap);
    usipy_sip_ua_transition(uap, USIPY_SIP_UA_STATE_DISCONNECTED);
    usipy_sip_ua_emit_event(uap, USIPY_SIP_UA_EMIT_DISCONNECT, tx_index, msg);
    return (USIPY_SIP_TM_OK);
}

const struct usipy_sip_ua_state_ops usipy_sip_ua_dialing_ops = {
    .on_transaction = usipy_sip_ua_dialing_on_transaction,
    .on_event = usipy_sip_ua_dialing_on_event,
    .on_tx_response = usipy_sip_ua_dialing_on_tx_response,
};
