#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "usipy_debug.h"
#include "usipy_types.h"
#include "sip_ua/usipy_sip_ua_internal.h"

static void *
usipy_sip_ua_heap_buf(struct usipy_sip_ua *uap)
{
    USIPY_DASSERT(uap != NULL);

    return ((void *)(uap + 1));
}

static void
usipy_sip_ua_heap_str_dup(struct usipy_sip_ua *uap, struct usipy_str *dstp,
  const struct usipy_str *srcp)
{
    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(dstp != NULL);
    USIPY_DASSERT(srcp != NULL);

    *dstp = USIPY_STR_NULL;
    if (srcp->l == 0) {
        return;
    }
    if (usipy_msg_heap_append(&uap->heap, dstp, srcp) != 0) {
        dstp->l = 0;
    }
}

static int
usipy_sip_ua_heap_str_dup_nullable(struct usipy_sip_ua *uap, struct usipy_str *dstp,
  const struct usipy_str *srcp)
{
    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(dstp != NULL);

    if (srcp == NULL) {
        *dstp = USIPY_STR_NULL;
        return (USIPY_SIP_TM_OK);
    }
    usipy_sip_ua_heap_str_dup(uap, dstp, srcp);
    if (srcp->l != 0 && dstp->l == 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    return (USIPY_SIP_TM_OK);
}

void
usipy_sip_ua_clear_dialing_request(struct usipy_sip_ua *uap)
{
    USIPY_DASSERT(uap != NULL);

    usipy_msg_heap_init(&uap->heap, usipy_sip_ua_heap_buf(uap), USIPY_SIP_UA_HEAP_SIZE,
      NULL, 0);
    uap->dialingp = NULL;
}

static int
usipy_sip_ua_build_request_target(struct usipy_sip_ua *uap,
  struct usipy_sip_tm_request_target *dstp, struct usipy_str *request_urip,
  struct usipy_sip_tm_addr *target_dstp, const struct usipy_sip_tm_addr *targetp,
  const struct usipy_str *to_userp)
{
    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(dstp != NULL);
    USIPY_DASSERT(request_urip != NULL);
    USIPY_DASSERT(target_dstp != NULL);
    USIPY_DASSERT(targetp != NULL);
    USIPY_DASSERT(to_userp != NULL);

    *target_dstp = *targetp;
    *dstp = (struct usipy_sip_tm_request_target){
      .request_uri = request_urip,
      .target = target_dstp,
    };
    usipy_sip_ua_heap_str_dup(uap, &target_dstp->host, &targetp->host);
    if (targetp->host.l != 0 && target_dstp->host.l == 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (targetp->port == 0 || targetp->port == 5060) {
        if (usipy_msg_heap_sprintf(&uap->heap, request_urip, "sip:%.*s@%.*s",
              USIPY_SFMT(to_userp), USIPY_SFMT(&targetp->host)) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    } else {
        if (usipy_msg_heap_sprintf(&uap->heap, request_urip,
              "sip:%.*s@%.*s:%d", USIPY_SFMT(to_userp),
              USIPY_SFMT(&targetp->host), targetp->port) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    }
    return (USIPY_SIP_TM_OK);
}

static void
usipy_sip_ua_dialing_request_init(struct usipy_sip_ua_dialing_request *dp)
{
    USIPY_DASSERT(dp != NULL);

    *dp = (struct usipy_sip_ua_dialing_request){0};
    dp->request_id.call_id = &dp->request_call_id;
    dp->request_target.request_uri = &dp->request_uri;
    dp->request_target.target = &dp->request_target_addr;
    dp->parties_by_username.contact = &dp->party_contact;
    dp->parties_by_username.from = &dp->party_from;
    dp->parties_by_username.to = &dp->party_to;
    dp->payload.content_type = &dp->content_type;
    dp->payload.body = &dp->body;
}

static int
usipy_sip_ua_dialing_request_fill(struct usipy_sip_ua *uap,
  struct usipy_sip_ua_dialing_request *dp,
  const struct usipy_sip_tm_new_uac_tr_params *requestp,
  const struct usipy_sip_ua_credentials *authp, const struct usipy_str *to_userp)
{
#define CHECK_HEAP_STR_DUP(dstp, srcp) do { \
    rval = usipy_sip_ua_heap_str_dup_nullable(uap, (dstp), (srcp)); \
    if (rval != USIPY_SIP_TM_OK) { \
        return (rval); \
    } \
} while (0)
    const struct usipy_sip_tm_addr *targetp;
    const struct usipy_str *call_idp, *request_urip;
    const struct usipy_sip_tm_addr *request_target_addrp;
    const struct usipy_str *request_target_hostp;
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(dp != NULL);
    USIPY_DASSERT(requestp != NULL);
    USIPY_DASSERT(requestp->request_id != NULL);
    USIPY_DASSERT(requestp->request_target != NULL);
    USIPY_DASSERT(requestp->parties_by_username != NULL);
    USIPY_DASSERT(authp != NULL);

    call_idp = requestp->request_id->call_id;
    request_urip = requestp->request_target->request_uri;
    request_target_addrp = requestp->request_target->target;
    request_target_hostp = GET_HOST_OR_NULL(request_target_addrp);
    dp->request_id.cseq = requestp->request_id->cseq;
    dp->request_id.method_type = requestp->request_id->method_type;
    if (request_target_addrp != NULL) {
        dp->request_target_addr = *request_target_addrp;
    }
    if (requestp->local != NULL) {
        dp->local = *requestp->local;
    }
    dp->contact_expires = requestp->contact_expires;
    dp->invite_expires = requestp->invite_expires;
    if (requestp->callbacks != NULL) {
        dp->callbacks = *requestp->callbacks;
    }
    CHECK_HEAP_STR_DUP(&dp->request_call_id, call_idp);
    CHECK_HEAP_STR_DUP(&dp->local.host, GET_HOST_OR_NULL(requestp->local));
    if ((request_urip != NULL && request_urip->l != 0) ||
      request_target_hostp != NULL) {
        CHECK_HEAP_STR_DUP(&dp->request_uri, request_urip);
        CHECK_HEAP_STR_DUP(&dp->request_target_addr.host, request_target_hostp);
    } else {
        targetp = uap->have_default_target ? &uap->default_target : NULL;
        if (targetp == NULL || to_userp == NULL || to_userp->l == 0) {
            return (USIPY_SIP_TM_ERR_INVAL);
        }
        rval = usipy_sip_ua_build_request_target(uap, &dp->request_target,
          &dp->request_uri, &dp->request_target_addr, targetp, to_userp);
        if (rval != USIPY_SIP_TM_OK) {
            return (rval);
        }
    }
    const struct usipy_str *contactp = requestp->parties_by_username->contact;
    const struct usipy_str *fromp = requestp->parties_by_username->from;
    const struct usipy_str *party_top =
      (requestp->parties_by_username->to != NULL &&
      requestp->parties_by_username->to->l != 0) ?
      requestp->parties_by_username->to : to_userp;
    const struct usipy_str *content_typep =
      GET_MEMBER_OR_NULL(requestp->payload, content_type);
    const struct usipy_str *bodyp = GET_MEMBER_OR_NULL(requestp->payload, body);
    const struct usipy_str *auth_usernamep = authp->username;
    const struct usipy_str *auth_passwordp = authp->password;
    const struct usipy_str *auth_qopp = authp->qop;
    CHECK_HEAP_STR_DUP(&dp->party_contact, contactp);
    CHECK_HEAP_STR_DUP(&dp->party_from, fromp);
    CHECK_HEAP_STR_DUP(&dp->party_to, party_top);
    CHECK_HEAP_STR_DUP(&dp->content_type, content_typep);
    CHECK_HEAP_STR_DUP(&dp->body, bodyp);
    CHECK_HEAP_STR_DUP(&dp->auth_username, auth_usernamep);
    CHECK_HEAP_STR_DUP(&dp->auth_password, auth_passwordp);
    CHECK_HEAP_STR_DUP(&dp->auth_qop, auth_qopp);
    dp->auth_retry_started = 0;
#undef CHECK_HEAP_STR_DUP
    return (USIPY_SIP_TM_OK);
}

void
usipy_sip_ua_fill_new_uac_tr_params(const struct usipy_sip_ua_dialing_request *dp,
  struct usipy_sip_tm_new_uac_tr_params *outp)
{
    USIPY_DASSERT(dp != NULL);
    USIPY_DASSERT(outp != NULL);

    *outp = (struct usipy_sip_tm_new_uac_tr_params){
      .request_id = &dp->request_id,
      .request_target = &dp->request_target,
      .local = &dp->local,
      .parties_by_username = &dp->parties_by_username,
      .contact_expires = dp->contact_expires,
      .invite_expires = dp->invite_expires,
      .payload = &dp->payload,
      .callbacks = &dp->callbacks,
    };
}

int
usipy_sip_ua_store_dialing_request(struct usipy_sip_ua *uap,
  const struct usipy_sip_ua_dial_params *dialp)
{
    struct usipy_sip_ua_dialing_request *dp;
    static const struct usipy_sip_ua_credentials empty_auth;
    const struct usipy_sip_ua_credentials *authp;
    const struct usipy_sip_tm_new_uac_tr_params *requestp;
    const struct usipy_str *to_userp;
    int rval;

    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(dialp != NULL);

    usipy_sip_ua_clear_dialing_request(uap);
    authp = (dialp->auth != NULL) ? dialp->auth : &empty_auth;
    requestp = dialp->request;
    USIPY_DASSERT(requestp != NULL);
    USIPY_DASSERT(requestp->request_id != NULL);
    USIPY_DASSERT(requestp->request_target != NULL);
    USIPY_DASSERT(requestp->parties_by_username != NULL);
    dp = usipy_msg_heap_alloc(&uap->heap, sizeof(*dp));
    if (dp == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    usipy_sip_ua_dialing_request_init(dp);
    to_userp = (dialp->to_user != NULL && dialp->to_user->l != 0) ? dialp->to_user :
      requestp->parties_by_username->to;
    rval = usipy_sip_ua_dialing_request_fill(uap, dp, requestp, authp, to_userp);
    if (rval != USIPY_SIP_TM_OK) {
        usipy_sip_ua_clear_dialing_request(uap);
        return (rval);
    }
    uap->dialingp = dp;
    return (USIPY_SIP_TM_OK);
}

const struct usipy_sip_ua_state_ops *
usipy_sip_ua_state_ops_get(enum usipy_sip_ua_state state)
{
    switch (state) {
    case USIPY_SIP_UA_STATE_IDLE:
        return (&usipy_sip_ua_idle_ops);

    case USIPY_SIP_UA_STATE_TRYING:
        return (&usipy_sip_ua_trying_ops);

    case USIPY_SIP_UA_STATE_DIALING:
        return (&usipy_sip_ua_dialing_ops);

    case USIPY_SIP_UA_STATE_CONNECTED:
        return (&usipy_sip_ua_connected_ops);

    case USIPY_SIP_UA_STATE_DISCONNECTED:
        return (&usipy_sip_ua_disconnected_ops);
    }
    USIPY_DABORT();
    return (&usipy_sip_ua_disconnected_ops);
}

void
usipy_sip_ua_transition(struct usipy_sip_ua *uap, enum usipy_sip_ua_state state)
{
    USIPY_DASSERT(uap != NULL);
    uap->state = state;
}

void
usipy_sip_ua_emit_event(struct usipy_sip_ua *uap, enum usipy_sip_ua_emit_type type,
  size_t tx_index, const struct usipy_msg *msg)
{
    struct usipy_sip_ua_emit emitp;

    USIPY_DASSERT(uap != NULL);
    if (uap->emit == NULL) {
        return;
    }
    emitp = (struct usipy_sip_ua_emit){
      .type = type,
      .state = uap->state,
      .role = uap->role,
      .transaction_index = tx_index,
      .message = msg,
      .body = (msg != NULL ? msg->body : USIPY_STR_NULL),
    };
    uap->emit(uap->emit_arg, &emitp);
}

int
usipy_sip_ua_expect_transaction(const struct usipy_sip_ua *uap, size_t tx_index,
  enum usipy_sip_tm_role role, uint8_t method_type, const struct usipy_sip_tm_tx **txpp)
{
    const struct usipy_sip_tm_tx *txp;

    if (uap == NULL || txpp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    txp = usipy_sip_tm_get_transaction(uap->tm, tx_index);
    if (txp == NULL) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    if (txp->role != role || txp->common.id.method_type != method_type) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    *txpp = txp;
    return (USIPY_SIP_TM_OK);
}

struct usipy_sip_ua *
usipy_sip_ua_ctor(const struct usipy_sip_ua_ctor_params *ipp)
{
    struct usipy_sip_ua *uap;

    if (ipp == NULL || ipp->tm == NULL) {
        return (NULL);
    }
    uap = calloc(1, sizeof(*uap) + USIPY_SIP_UA_HEAP_SIZE);
    if (uap == NULL) {
        return (NULL);
    }
    usipy_msg_heap_init(&uap->heap, usipy_sip_ua_heap_buf(uap), USIPY_SIP_UA_HEAP_SIZE,
      NULL, 0);
    uap->tm = ipp->tm;
    uap->state = USIPY_SIP_UA_STATE_IDLE;
    uap->role = USIPY_SIP_TM_ROLE_UAC;
    uap->tx_index = USIPY_SIP_TM_TX_INDEX_NONE;
    uap->emit = ipp->emit;
    uap->emit_arg = ipp->emit_arg;
    if (ipp->default_target != NULL) {
        uap->default_target = *ipp->default_target;
        uap->have_default_target = 1;
    }
    return (uap);
}

void
usipy_sip_ua_dtor(struct usipy_sip_ua *uap)
{
    if (uap == NULL) {
        return;
    }
    if (uap->dialogp != NULL) {
        usipy_sip_dialog_dtor(uap->dialogp);
    }
    free(uap);
}

enum usipy_sip_ua_state
usipy_sip_ua_get_state(const struct usipy_sip_ua *uap)
{
    if (uap == NULL) {
        return (USIPY_SIP_UA_STATE_DISCONNECTED);
    }
    return (uap->state);
}

int
usipy_sip_ua_matches_transaction(const struct usipy_sip_ua *uap, const struct usipy_msg *msg)
{
    USIPY_DASSERT(uap != NULL);
    USIPY_DASSERT(msg != NULL);
    if (uap->dialogp == NULL) {
        return (0);
    }
    return (usipy_sip_dialog_matches_uas_transaction(uap->dialogp, msg));
}

int
usipy_sip_ua_on_event(struct usipy_sip_ua *uap, const struct usipy_sip_ua_event *eventp,
  size_t *indexp)
{
    const struct usipy_sip_ua_state_ops *opp;
    size_t tx_index;

    if (uap == NULL || eventp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    opp = usipy_sip_ua_state_ops_get(uap->state);
    tx_index = USIPY_SIP_TM_TX_INDEX_NONE;
    const int rval = opp->on_event(uap, eventp, &tx_index);

    if (indexp != NULL) {
        *indexp = tx_index;
    }
    return (rval);
}

int
usipy_sip_ua_on_transaction(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    const struct usipy_sip_ua_state_ops *opp;

    if (uap == NULL || msg == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    opp = usipy_sip_ua_state_ops_get(uap->state);
    return (opp->on_transaction(uap, tx_index, msg));
}

int
usipy_sip_ua_on_tx_response(struct usipy_sip_ua *uap, size_t tx_index,
  const struct usipy_msg *msg)
{
    const struct usipy_sip_ua_state_ops *opp;

    if (uap == NULL || msg == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    opp = usipy_sip_ua_state_ops_get(uap->state);
    return (opp->on_tx_response(uap, tx_index, msg));
}
