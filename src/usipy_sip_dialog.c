#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_dialog.h"
#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_hdr_types.h"
#include "public/usipy_str.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_method_db.h"
#include "usipy_sip_res.h"
#include "usipy_sip_tid.h"
#include "usipy_sip_tm_internal.h"
#include "usipy_sip_uri.h"

#define USIPY_SIP_DIALOG_HEAP_SIZE 1024u

struct usipy_sip_dialog_state {
    struct usipy_str call_id;
    struct usipy_sip_tm_request_target request_target;
    struct usipy_sip_tm_request_parties parties_by_uri;
    struct usipy_sip_tm_route_set route_set;
    struct usipy_sip_tm_dialog_tags dialog_tags;
    struct usipy_sip_tm_timer_policy timers;
    uint32_t match_hash;
    uint32_t cseq;
};

struct usipy_sip_dialog {
    struct usipy_sip_tm *tm;
    struct usipy_msg_heap heap;
    struct usipy_sip_dialog_state state;
    int ended;
    unsigned char _storage[USIPY_SIP_DIALOG_HEAP_SIZE];
};

static void
usipy_sip_dialog_store_state(struct usipy_sip_dialog_state *dstp,
  const struct usipy_sip_tm_new_in_dialog_transaction_params *srcp)
{
    USIPY_DASSERT(dstp != NULL);
    USIPY_DASSERT(srcp != NULL);

    memset(dstp, '\0', sizeof(*dstp));
    dstp->call_id = srcp->request_id.call_id;
    dstp->request_target = srcp->request_target;
    dstp->parties_by_uri = srcp->parties_by_uri;
    dstp->route_set = srcp->route_set;
    dstp->dialog_tags = srcp->dialog_tags;
    dstp->timers = srcp->timers;
    dstp->match_hash = usipy_sip_dialog_hash(&srcp->request_id.call_id,
      &srcp->dialog_tags.remote_tag, &srcp->dialog_tags.local_tag);
    dstp->cseq = srcp->request_id.cseq;
}

static int
usipy_sip_dialog_match_uas_request(const struct usipy_sip_dialog *dp,
  const struct usipy_msg *msg, const struct usipy_str **call_idpp,
  const struct usipy_str **from_tagpp, const struct usipy_str **to_tagpp)
{
    struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *fromp = NULL, *top = NULL;
    const struct usipy_str *call_idp = NULL;
    const struct usipy_str *from_tagp, *to_tagp;
    uint32_t hash;

    USIPY_DASSERT(dp != NULL);
    USIPY_DASSERT(msg != NULL);

    if (msg->kind != USIPY_SIP_MSG_REQ) {
        return (0);
    }
    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(msg->nhdrs));
    *matchp = (struct usipy_sip_hdr_match){.hdrslen = msg->nhdrs};
    if (usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)msg,
      USIPY_HFT_MASK(USIPY_HF_CALLID) | USIPY_HFT_MASK(USIPY_HF_FROM) |
      USIPY_HFT_MASK(USIPY_HF_TO), 1, matchp) != 0 || matchp->nhdrs != 3) {
        return (0);
    }
    for (size_t i = 0; i < matchp->nhdrs; i++) {
        switch (matchp->hdrsp[i]->hf_type->cantype) {
        case USIPY_HF_CALLID:
            call_idp = matchp->hdrsp[i]->parsed.generic;
            break;

        case USIPY_HF_FROM:
            fromp = matchp->hdrsp[i]->parsed.from;
            break;

        case USIPY_HF_TO:
            top = matchp->hdrsp[i]->parsed.to;
            break;
        }
    }
    if (call_idp == NULL || fromp == NULL || top == NULL) {
        return (0);
    }
    from_tagp = usipy_sip_hdr_nameaddr_get_param(fromp, "tag");
    to_tagp = usipy_sip_hdr_nameaddr_get_param(top, "tag");
    if (from_tagp == NULL || from_tagp->l == 0 || to_tagp == NULL || to_tagp->l == 0) {
        return (0);
    }
    hash = usipy_sip_dialog_hash(call_idp, from_tagp, to_tagp);
    if (hash != dp->state.match_hash ||
      !usipy_str_eq(call_idp, &dp->state.call_id) ||
      !usipy_str_eq(from_tagp, &dp->state.dialog_tags.remote_tag) ||
      !usipy_str_eq(to_tagp, &dp->state.dialog_tags.local_tag)) {
        return (0);
    }
    if (call_idpp != NULL) {
        *call_idpp = call_idp;
    }
    if (from_tagpp != NULL) {
        *from_tagpp = from_tagp;
    }
    if (to_tagpp != NULL) {
        *to_tagpp = to_tagp;
    }
    return (1);
}

struct usipy_sip_dialog *
usipy_sip_dialog_uac_ctor(struct usipy_sip_tm *tm, size_t invite_index,
  const struct usipy_msg *msg)
{
    struct usipy_sip_dialog *dp;
    struct usipy_sip_tm_new_in_dialog_transaction_params tpp = {0};

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(msg != NULL);
    dp = calloc(1, sizeof(*dp));
    if (dp == NULL) {
        return (NULL);
    }
    usipy_msg_heap_init(&dp->heap, dp->_storage, sizeof(dp->_storage), NULL, 0);
    dp->tm = tm;
    if (usipy_sip_tm_init_uac_dialog_request_params(tm, invite_index, msg,
      USIPY_SIP_METHOD_BYE, &dp->heap, &tpp) != USIPY_SIP_TM_OK) {
        free(dp);
        return (NULL);
    }
    usipy_sip_dialog_store_state(&dp->state, &tpp);
    return (dp);
}

struct usipy_sip_dialog *
usipy_sip_dialog_uas_ctor(struct usipy_sip_tm *tm, size_t invite_index,
  const struct usipy_sip_tm_uas_response_params *rpp)
{
    struct usipy_sip_dialog *dp;
    struct usipy_sip_tm_new_in_dialog_transaction_params tpp = {0};

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(rpp != NULL);
    if (rpp->status.code < 200 || rpp->status.code > 299) {
        return (NULL);
    }
    dp = calloc(1, sizeof(*dp));
    if (dp == NULL) {
        return (NULL);
    }
    usipy_msg_heap_init(&dp->heap, dp->_storage, sizeof(dp->_storage), NULL, 0);
    dp->tm = tm;
    if (usipy_sip_tm_init_uas_dialog_request_params(tm, invite_index,
      USIPY_SIP_METHOD_BYE, &dp->heap, &tpp) != USIPY_SIP_TM_OK ||
      usipy_sip_tm_send_uas_response(tm, invite_index, rpp) != USIPY_SIP_TM_OK) {
        free(dp);
        return (NULL);
    }
    usipy_sip_dialog_store_state(&dp->state, &tpp);
    return (dp);
}

void
usipy_sip_dialog_dtor(struct usipy_sip_dialog *dp)
{
    USIPY_DASSERT(dp != NULL);
    free(dp);
}

int
usipy_sip_dialog_matches_uas_transaction(const struct usipy_sip_dialog *dp,
  const struct usipy_msg *msg)
{
    USIPY_DASSERT(dp != NULL);
    USIPY_DASSERT(msg != NULL);
    return (usipy_sip_dialog_match_uas_request(dp, msg, NULL, NULL, NULL));
}

int
usipy_sip_dialog_handle_uas_transaction(struct usipy_sip_dialog *dp, size_t tx_index,
  const struct usipy_msg *msg)
{
    const struct usipy_sip_tm_tx *txp;
    const struct usipy_sip_tm_uas_response_params ok = {
      .status = usipy_sip_res_ok,
    };
    int rval;

    USIPY_DASSERT(dp != NULL);
    USIPY_DASSERT(msg != NULL);
    if (!usipy_sip_dialog_match_uas_request(dp, msg, NULL, NULL, NULL)) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    txp = usipy_sip_tm_get_transaction(dp->tm, tx_index);
    if (txp == NULL) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    if (txp->role != USIPY_SIP_TM_ROLE_UAS ||
      txp->common.id.method_type != USIPY_SIP_METHOD_BYE ||
      msg->sline.parsed.rl.method->cantype != USIPY_SIP_METHOD_BYE) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    rval = usipy_sip_tm_send_uas_response(dp->tm, tx_index, &ok);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    dp->ended = 1;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_dialog_end(struct usipy_sip_dialog *dp,
  const struct usipy_sip_tm_uac_callbacks *cbp, size_t *indexp)
{
    struct usipy_sip_tm_new_in_dialog_transaction_params tp = {0};
    struct usipy_sip_tm_uac_callbacks callbacks = {0};
    int rval;

    USIPY_DASSERT(dp != NULL);
    USIPY_DASSERT(indexp != NULL);
    if (dp->ended) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (cbp != NULL) {
        callbacks = *cbp;
    }
    tp.request_id.call_id = dp->state.call_id;
    tp.request_id.cseq = dp->state.cseq + 1;
    tp.request_id.method_type = USIPY_SIP_METHOD_BYE;
    tp.request_target = dp->state.request_target;
    tp.parties_by_uri = dp->state.parties_by_uri;
    tp.route_set = dp->state.route_set;
    tp.dialog_tags = dp->state.dialog_tags;
    tp.timers = dp->state.timers;
    tp.callbacks = callbacks;
    rval = usipy_sip_tm_new_in_dialog_transaction(dp->tm, &tp, indexp);

    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    dp->state.cseq = tp.request_id.cseq;
    dp->ended = 1;
    return (USIPY_SIP_TM_OK);
}
