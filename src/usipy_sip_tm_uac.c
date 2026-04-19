#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "usipy_port/network.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_hdr_types.h"
#include "public/usipy_sip_method_types.h"
#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_sline.h"
#include "public/usipy_sip_tm.h"
#include "public/usipy_str.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_sip_hdr_authz.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_method_db.h"
#include "usipy_sip_tid.h"
#include "usipy_sip_tm_internal.h"
#include "usipy_sip_tm_priv.h"
#include "usipy_tvpair.h"

struct usipy_sip_tm_build_request_params {
    const struct usipy_sip_tm_request_payload *payload;
    const struct usipy_sip_tm_extra_header *extra_headers;
    size_t nextra_headers;
};

static int usipy_sip_tm_build_request(struct usipy_sip_tm_txi *, size_t,
  const struct usipy_sip_tm *, const struct usipy_sip_tm_build_request_params *);

static int
usipy_sip_tm_transition_allowed(const struct usipy_sip_tm_txi *tp,
  enum usipy_sip_tm_state next_state)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tp->pub.role == USIPY_SIP_TM_ROLE_UAC);

    if (next_state != USIPY_SIP_TM_STATE_CALLING) {
        return (0);
    }
    return (tp->pub.state == USIPY_SIP_TM_STATE_TERMINATED ||
      tp->pub.state == USIPY_SIP_TM_STATE_COMPLETED);
}

static int
usipy_sip_tm_transition(struct usipy_sip_tm_txi *tp,
  enum usipy_sip_tm_state next_state)
{
    USIPY_DASSERT(tp != NULL);

    if (!usipy_sip_tm_transition_allowed(tp, next_state)) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    tp->pub.state = next_state;
    tp->pub.common.flags &= ~USIPY_SIP_TM_F_TERMINATED;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_format_local_uri(struct usipy_sip_tm_txi *tp, const struct usipy_sip_tm *tm,
  const struct usipy_str *userp, int include_port, struct usipy_str *urip)
{
    if (userp->l == 0) {
        switch (tm->laddr.af) {
        case AF_INET:
            if (include_port) {
                return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:%.*s:%u",
                  USIPY_SFMT(&tm->laddr.host), tm->laddr.port));
            }
            return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:%.*s",
              USIPY_SFMT(&tm->laddr.host)));
#ifdef IPPROTO_IPV6
        case AF_INET6:
            if (include_port) {
                return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:[%.*s]:%u",
                  USIPY_SFMT(&tm->laddr.host), tm->laddr.port));
            }
            return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:[%.*s]",
              USIPY_SFMT(&tm->laddr.host)));
#endif
        default:
            errno = EAFNOSUPPORT;
            return (-1);
        }
    }
    switch (tm->laddr.af) {
    case AF_INET:
        if (include_port) {
            return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:%.*s@%.*s:%u",
              USIPY_SFMT(userp), USIPY_SFMT(&tm->laddr.host), tm->laddr.port));
        }
        return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:%.*s@%.*s",
          USIPY_SFMT(userp), USIPY_SFMT(&tm->laddr.host)));
#ifdef IPPROTO_IPV6
    case AF_INET6:
        if (include_port) {
            return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:%.*s@[%.*s]:%u",
              USIPY_SFMT(userp), USIPY_SFMT(&tm->laddr.host), tm->laddr.port));
        }
        return (usipy_msg_heap_sprintf(&tp->scratch, urip, "sip:%.*s@[%.*s]",
          USIPY_SFMT(userp), USIPY_SFMT(&tm->laddr.host)));
#endif
    default:
        errno = EAFNOSUPPORT;
        return (-1);
    }
}

static int
usipy_sip_tm_copy_uri(struct usipy_sip_tm_txi *tp, const struct usipy_str *srcp,
  struct usipy_str *dstp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(srcp != NULL);
    USIPY_DASSERT(dstp != NULL);

    if (srcp->l == 0) {
        *dstp = USIPY_STR_NULL;
        return (0);
    }
    return (usipy_msg_heap_append(&tp->scratch, dstp, srcp));
}

static int
usipy_sip_tm_copy_route_set(struct usipy_msg_heap *mhp, struct usipy_str **dstpp,
  size_t nroutes, const struct usipy_str *srcp)
{
    struct usipy_str *dstp;

    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(dstpp != NULL);
    USIPY_DASSERT(nroutes == 0 || srcp != NULL);

    if (nroutes == 0) {
        *dstpp = NULL;
        return (0);
    }
    dstp = usipy_msg_heap_alloc(mhp, sizeof(*dstp) * nroutes);
    if (dstp == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    for (size_t i = 0; i < nroutes; i++) {
        if (usipy_msg_heap_append(mhp, &dstp[i], &srcp[i]) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    }
    *dstpp = dstp;
    return (0);
}

static int
usipy_sip_tm_lookup_uac_method(uint8_t method_type, int in_dialog,
  const struct usipy_method_db_entr **mdpp)
{
    const struct usipy_method_db_entr *mdp;

    USIPY_DASSERT(mdpp != NULL);
    USIPY_DASSERT(method_type <= USIPY_SIP_METHOD_max);

    if (in_dialog &&
      (method_type == USIPY_SIP_METHOD_INVITE ||
      method_type == USIPY_SIP_METHOD_ACK ||
      method_type == USIPY_SIP_METHOD_CANCEL)) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    mdp = &usipy_method_db[method_type];
    USIPY_DASSERT(mdp->cantype == method_type && mdp->name.l > 0);
    if (mdp->cantype != method_type || mdp->name.l == 0) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    *mdpp = mdp;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_init_uac_request(struct usipy_sip_tm_txi *tp,
  const struct usipy_sip_tm_request_id *request_idp,
  const struct usipy_sip_tm_request_target *request_targetp,
  const struct usipy_method_db_entr *mdp)
{
    struct usipy_str req_uri;

    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(request_idp != NULL);
    USIPY_DASSERT(request_targetp != NULL);
    USIPY_DASSERT(mdp != NULL);

    if (usipy_msg_heap_append(&tp->scratch, &tp->cache.call_id,
      &request_idp->call_id) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (usipy_msg_heap_append(&tp->scratch, &req_uri,
      &request_targetp->request_uri) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    tp->cache.uac.request_uri = usipy_sip_uri_parse(&tp->scratch, &req_uri);
    if (tp->cache.uac.request_uri == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    tp->cache.method_type = request_idp->method_type;
    tp->cache.cseq.val = request_idp->cseq;
    tp->cache.cseq.method = mdp;
    tp->cache.uac.include_contact = 1;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_init_uac_parties_by_username(struct usipy_sip_tm_txi *tp,
  const struct usipy_sip_tm *tm, const struct usipy_sip_tm_request_parties *partiesp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(partiesp != NULL);

    if (usipy_sip_tm_format_local_uri(tp, tm, &partiesp->from, 0,
      &tp->cache.uac.from_uri) != 0 ||
      usipy_sip_tm_format_local_uri(tp, tm, &partiesp->to, 0,
        &tp->cache.uac.to_uri) != 0 ||
      usipy_sip_tm_format_local_uri(tp, tm, &partiesp->contact, 1,
        &tp->cache.uac.contact_uri) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_init_uac_parties_by_uri(struct usipy_sip_tm_txi *tp,
  const struct usipy_sip_tm_request_parties *partiesp,
  const struct usipy_sip_tm_dialog_tags *dialog_tagsp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(partiesp != NULL);
    USIPY_DASSERT(dialog_tagsp != NULL);

    if (usipy_sip_tm_copy_uri(tp, &partiesp->from, &tp->cache.uac.from_uri) != 0 ||
      usipy_sip_tm_copy_uri(tp, &partiesp->to, &tp->cache.uac.to_uri) != 0 ||
      usipy_sip_tm_copy_uri(tp, &partiesp->contact, &tp->cache.uac.contact_uri) != 0 ||
      usipy_msg_heap_append(&tp->scratch, &tp->cache.from_tag,
        &dialog_tagsp->local_tag) != 0 ||
      usipy_msg_heap_append(&tp->scratch, &tp->cache.to_tag,
        &dialog_tagsp->remote_tag) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    return (USIPY_SIP_TM_OK);
}

static void
usipy_sip_tm_set_route_set(struct usipy_sip_tm_txi *tp, struct usipy_str *routes,
  size_t nroutes)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(nroutes == 0 || routes != NULL);

    tp->cache.uac.routes = routes;
    tp->cache.uac.nroutes = nroutes;
}

static void
usipy_sip_tm_activate_uac_slot(struct usipy_sip_tm *tm, struct usipy_sip_tm_txi *tp,
  enum usipy_sip_tm_state state,
  const struct usipy_sip_tm_request_id *request_idp,
  const struct usipy_sip_tm_request_target *request_targetp,
  const struct usipy_sip_tm_uac_callbacks *callbacksp,
  const struct usipy_sip_tm_timer_policy *timersp)
{
    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(request_idp != NULL);
    USIPY_DASSERT(request_targetp != NULL);
    USIPY_DASSERT(callbacksp != NULL);
    USIPY_DASSERT(timersp != NULL);

    tp->callbacks = *callbacksp;
    tp->outbound.checkpoint = usipy_msg_heap_checkpoint(&tp->scratch);
    tp->active = 1;
    tm->nactive += 1;
    tp->pub.role = USIPY_SIP_TM_ROLE_UAC;
    tp->pub.state = state;
    tp->pub.common.flags = (tm->transport == USIPY_SIP_TM_TRANSPORT_UDP) ? 0 :
      USIPY_SIP_TM_F_RELIABLE_TRANSPORT;
    tp->pub.common.peer = request_targetp->target;
    tp->pub.common.local = tm->laddr;
    tp->outbound.pub.target = request_targetp->target;
    tp->outbound.pub.raw = USIPY_STR_NULL;
    tp->outbound.pub.next_send_at_ms = 0;
    tp->pub.common.outbound = tp->outbound.pub;
    tp->pub.common.timers = *timersp;
    tp->pub.common.id.hash = 0;
    tp->pub.common.id.branch = USIPY_STR_NULL;
    tp->pub.common.id.call_id = tp->cache.call_id;
    tp->pub.common.id.from_tag = tp->cache.from_tag;
    tp->pub.common.id.cseq = request_idp->cseq;
    tp->pub.common.id.method_type = tp->cache.method_type;
    tp->pub.role_data.uac.last_status_code = 0;
    tp->pub.role_data.uac.response_class = 0;
}

static int
usipy_sip_tm_prepare_default_ids(struct usipy_sip_tm_txi *tp, size_t tx_index,
  struct usipy_sip_tm_id_policy_out *outp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(outp != NULL);

    if (usipy_msg_heap_sprintf(&tp->scratch, &outp->branch, "z9hG4bK-%u-%u",
      (unsigned int)tx_index, tp->cache.cseq.val) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (usipy_msg_heap_sprintf(&tp->scratch, &outp->local_tag, "t%u-%u",
      (unsigned int)tx_index, tp->cache.cseq.val) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_prepare_ids(const struct usipy_sip_tm *tm, struct usipy_sip_tm_txi *tp,
  size_t tx_index)
{
    struct usipy_sip_tm_id_policy_out ids = {0};
    struct usipy_sip_tm_id_policy_in in = {
      .transaction_index = tx_index,
      .cseq = tp->cache.cseq.val,
      .method_type = tp->cache.method_type,
    };
    struct usipy_sip_tid tid;

    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tm != NULL);

    if (tm->id_policy.cb != NULL) {
        if (tm->id_policy.cb(tm->id_policy.arg, &tp->scratch, &in, &ids) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    } else if (usipy_sip_tm_prepare_default_ids(tp, tx_index, &ids) != USIPY_SIP_TM_OK) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (ids.branch.l == 0 || (tp->cache.from_tag.l == 0 && ids.local_tag.l == 0)) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (tp->cache.branch.l == 0) {
        tp->cache.branch = ids.branch;
    }
    if (tp->cache.from_tag.l == 0) {
        tp->cache.from_tag = ids.local_tag;
    }
    tid.call_id = &tp->cache.call_id;
    tid.from_tag = &tp->cache.from_tag;
    tid.vbranch = &tp->cache.branch;
    tid.cseq = &tp->cache.cseq;
    tid.hash = usipy_sip_tid_hash(&tid);
    tp->pub.common.id.hash = tid.hash;
    tp->pub.common.id.branch = *tid.vbranch;
    tp->pub.common.id.call_id = *tid.call_id;
    tp->pub.common.id.from_tag = *tid.from_tag;
    tp->pub.common.id.cseq = tp->cache.cseq.val;
    tp->pub.common.id.method_type = tp->cache.method_type;
    return (USIPY_SIP_TM_OK);
}

static void
usipy_sip_tm_tx_reset_uac_runtime(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    tp->pub.common.flags &= ~USIPY_SIP_TM_F_TERMINATED;
    tp->pub.common.id.hash = 0;
    tp->pub.common.id.branch = USIPY_STR_NULL;
    tp->pub.common.id.from_tag = USIPY_STR_NULL;
    tp->pub.common.id.cseq = tp->cache.cseq.val;
    tp->pub.common.retransmit_count = 0;
    tp->pub.common.created_at_ms = 0;
    tp->pub.common.updated_at_ms = 0;
    tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
    tp->pub.common.timer.value_ms = 0;
    tp->pub.common.timer.due_at_ms = 0;
    tp->pub.role_data.uac.last_status_code = 0;
    tp->pub.role_data.uac.response_class = 0;
    tp->outbound.pub.raw = USIPY_STR_NULL;
    tp->outbound.pub.next_send_at_ms = 0;
    tp->invite_timeout_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->final_reported = 0;
    tp->invite_provisional_seen = 0;
    tp->invite_cancel_state = USIPY_SIP_TM_INVITE_CANCEL_NONE;
    tp->invite_timeout_id = USIPY_SIP_TM_TIMEOUT_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
}

static void
usipy_sip_tm_release_outbound(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    if (tp->outbound.checkpoint != USIPY_MSG_HEAP_CHECKPOINT_NONE) {
        usipy_msg_heap_rollback(&tp->scratch, tp->outbound.checkpoint);
    }
    tp->pub.common.id.hash = 0;
    tp->pub.common.id.branch = USIPY_STR_NULL;
    tp->pub.common.id.from_tag = USIPY_STR_NULL;
    tp->outbound.pub.raw = USIPY_STR_NULL;
    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
}

static void
usipy_sip_tm_tx_clear_timer(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
    tp->pub.common.timer.value_ms = 0;
    tp->pub.common.timer.due_at_ms = 0;
}

static void
usipy_sip_tm_tx_clear_invite_timeout(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    tp->invite_timeout_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->invite_timeout_id = USIPY_SIP_TM_TIMEOUT_NONE;
}

static void
usipy_sip_tm_tx_mark_terminated(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    tp->pub.state = USIPY_SIP_TM_STATE_TERMINATED;
    tp->pub.common.flags |= USIPY_SIP_TM_F_TERMINATED;
    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
}

static void
usipy_sip_tm_tx_terminate_child(struct usipy_sip_tm *tm, size_t *indexp)
{
    struct usipy_sip_tm_txi *childp;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(indexp != NULL);

    if (*indexp == USIPY_SIP_TM_TX_INDEX_NONE) {
        return;
    }
    USIPY_DASSERT(*indexp < tm->max_transactions);
    childp = &tm->transactions[*indexp];
    if (childp->active) {
        usipy_sip_tm_tx_mark_terminated(childp);
        usipy_sip_tm_tx_clear_timer(childp);
        usipy_sip_tm_tx_clear_invite_timeout(childp);
        childp->parent_index = USIPY_SIP_TM_TX_INDEX_NONE;
        childp->child_index = USIPY_SIP_TM_TX_INDEX_NONE;
        usipy_sip_tm_release_outbound(childp);
    }
    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
}

static int
usipy_sip_tm_tx_is_invite(const struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    return (tp->pub.common.id.method_type == USIPY_SIP_METHOD_INVITE);
}

static int
usipy_sip_tm_tx_is_ack(const struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    return (tp->pub.common.id.method_type == USIPY_SIP_METHOD_ACK);
}

static int
usipy_sip_tm_tx_waits_for_invite_ack(const struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    return (usipy_sip_tm_tx_is_invite(tp) &&
      tp->child_index != USIPY_SIP_TM_TX_INDEX_NONE &&
      tp->pub.common.timer.type == USIPY_SIP_TM_TIMER_D);
}

static int
usipy_sip_tm_extract_response_to_tag(struct usipy_msg *msg, struct usipy_str *tagp)
{
    const struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *top;
    const struct usipy_str *vp;

    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(tagp != NULL);

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
    ((struct usipy_sip_hdr_match *)matchp)->hdrslen = 1;
    if (usipy_sip_msg_parse_hdrs_get(msg, USIPY_HFT_MASK(USIPY_HF_TO), 1,
      (struct usipy_sip_hdr_match *)matchp) != 0 || matchp->nhdrs == 0) {
        return (-1);
    }
    top = matchp->hdrsp[0]->parsed.to;
    if (top == NULL) {
        return (-1);
    }
    vp = usipy_sip_hdr_nameaddr_get_param(top, "tag");
    if (vp == NULL || vp->l == 0) {
        return (-1);
    }
    *tagp = *vp;
    return (0);
}

struct usipy_sip_tm_build_uri_arg {
    const struct usipy_sip_uri *urip;
};

static int
usipy_sip_tm_build_uri_cb(void *arg, char *buf, size_t len)
{
    const struct usipy_sip_tm_build_uri_arg *uarg = arg;

    USIPY_DASSERT(uarg != NULL);
    USIPY_DASSERT(uarg->urip != NULL);
    return (usipy_sip_uri_build(uarg->urip, buf, len));
}

struct usipy_sip_tm_build_authz_arg {
    const struct usipy_sip_hdr_authz *authzp;
};

static int
usipy_sip_tm_build_authz_cb(void *arg, char *buf, size_t len)
{
    const struct usipy_sip_tm_build_authz_arg *aarg = arg;
    union usipy_sip_hdr_parsed up;

    USIPY_DASSERT(aarg != NULL);
    USIPY_DASSERT(aarg->authzp != NULL);
    up.authz = (struct usipy_sip_hdr_authz *)aarg->authzp;
    return (usipy_sip_hdr_authz_build(&up, buf, len));
}

static void
usipy_sip_tm_uac_arm_send_now(struct usipy_sip_tm_txi *tp, uint64_t now_ms)
{
    USIPY_DASSERT(tp != NULL);

    tp->outbound.pub.next_send_at_ms = now_ms;
    tp->pub.common.outbound = tp->outbound.pub;
}

static int
usipy_sip_tm_clone_uac_invite(struct usipy_sip_tm *tm, size_t parent_index,
  uint8_t method_type, struct usipy_sip_tm_txi **childpp)
{
    const struct usipy_sip_tm_txi *srcp;
    struct usipy_sip_tm_txi *childp;
    size_t child_index;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(parent_index < tm->max_transactions);
    USIPY_DASSERT(childpp != NULL);
    USIPY_DASSERT(method_type == USIPY_SIP_METHOD_ACK ||
      method_type == USIPY_SIP_METHOD_CANCEL);

    srcp = &tm->transactions[parent_index];
    childp = usipy_sip_tm_alloc_slot(tm, &child_index);
    if (childp == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (method_type == USIPY_SIP_METHOD_CANCEL && srcp->cache.call_id.l != 0 &&
      usipy_msg_heap_append(&childp->scratch, &childp->cache.call_id,
        &srcp->cache.call_id) != 0) {
        usipy_sip_tm_tx_fini(childp);
        return (USIPY_SIP_TM_ERR_NOSPC);
    } else if (method_type != USIPY_SIP_METHOD_CANCEL) {
        childp->cache.call_id = srcp->cache.call_id;
    }
    childp->cache.uac.request_uri = srcp->cache.uac.request_uri;
    childp->cache.uac.from_uri = srcp->cache.uac.from_uri;
    childp->cache.uac.to_uri = srcp->cache.uac.to_uri;
    childp->cache.uac.contact_uri = srcp->cache.uac.contact_uri;
    childp->cache.method_type = method_type;
    if (srcp->cache.from_tag.l != 0 &&
      usipy_msg_heap_append(&childp->scratch, &childp->cache.from_tag,
        &srcp->cache.from_tag) != 0) {
        usipy_sip_tm_tx_fini(childp);
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (method_type == USIPY_SIP_METHOD_CANCEL && srcp->cache.branch.l != 0 &&
      usipy_msg_heap_append(&childp->scratch, &childp->cache.branch,
        &srcp->cache.branch) != 0) {
        usipy_sip_tm_tx_fini(childp);
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    childp->cache.uac.invite_expires = srcp->cache.uac.invite_expires;
    childp->cache.cseq.val = srcp->cache.cseq.val;
    childp->cache.cseq.method = &usipy_method_db[method_type];
    childp->cache.uac.include_contact = 0;
    childp->outbound.checkpoint = usipy_msg_heap_checkpoint(&childp->scratch);
    childp->active = 1;
    tm->nactive += 1;
    childp->parent_index = method_type == USIPY_SIP_METHOD_ACK ? parent_index :
      USIPY_SIP_TM_TX_INDEX_NONE;
    childp->pub.role = USIPY_SIP_TM_ROLE_UAC;
    childp->pub.state = method_type == USIPY_SIP_METHOD_ACK ?
      USIPY_SIP_TM_STATE_CALLING : USIPY_SIP_TM_STATE_TRYING;
    childp->pub.common.flags = srcp->pub.common.flags;
    childp->pub.common.peer = srcp->pub.common.peer;
    childp->pub.common.local = srcp->pub.common.local;
    childp->outbound.pub.target = srcp->outbound.pub.target;
    childp->outbound.pub.raw = USIPY_STR_NULL;
    childp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    childp->pub.common.outbound = childp->outbound.pub;
    childp->pub.common.timers = srcp->pub.common.timers;
    childp->pub.common.id.call_id = childp->cache.call_id;
    childp->pub.common.id.cseq = childp->cache.cseq.val;
    childp->pub.common.id.method_type = childp->cache.method_type;
    *childpp = childp;
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_uac_start_cancel(struct usipy_sip_tm *tm, size_t parent_index)
{
    struct usipy_sip_tm_txi *parentp;
    struct usipy_sip_tm_txi *cancelp;
    size_t cancel_index;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(parent_index < tm->max_transactions);

    parentp = &tm->transactions[parent_index];
    if (parentp->invite_cancel_state == USIPY_SIP_TM_INVITE_CANCEL_ONWIRE) {
        return (USIPY_SIP_TM_OK);
    }
    if (!parentp->invite_provisional_seen) {
        parentp->invite_cancel_state = USIPY_SIP_TM_INVITE_CANCEL_SCHEDULED;
        return (USIPY_SIP_TM_OK);
    }
    if (usipy_sip_tm_clone_uac_invite(tm, parent_index, USIPY_SIP_METHOD_CANCEL,
      &cancelp) != USIPY_SIP_TM_OK) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    cancel_index = (size_t)(cancelp - tm->transactions);
    cancelp->callbacks.arg = parentp->callbacks.arg;
    cancelp->callbacks.response = NULL;
    cancelp->callbacks.timeout = NULL;
    if (usipy_sip_tm_build_request(cancelp, cancel_index, tm, NULL) != USIPY_SIP_TM_OK) {
        usipy_sip_tm_tx_fini(cancelp);
        if (tm->nactive > 0) {
            tm->nactive -= 1;
        }
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    parentp->invite_cancel_state = USIPY_SIP_TM_INVITE_CANCEL_ONWIRE;
    usipy_sip_tm_uac_arm_send_now(cancelp, 0);
    return (USIPY_SIP_TM_OK);
}

static uint32_t
usipy_sip_tm_timer_d_ms(const struct usipy_sip_tm_timer_policy *tp)
{
    uint32_t dms;

    if (tp->timer_d_ms != 0) {
        return (tp->timer_d_ms);
    }
    dms = tp->t4_ms;
    if (dms != 0) {
        return (dms);
    }
    return (5000u);
}

static uint32_t
usipy_sip_tm_timer_f_ms(const struct usipy_sip_tm_timer_policy *tp)
{
    uint64_t fms;

    if (tp->timer_f_ms != 0) {
        return (tp->timer_f_ms);
    }
    fms = (uint64_t)tp->t1_ms * 64u;
    USIPY_DASSERT(fms <= UINT32_MAX);
    return ((uint32_t)fms);
}

static int
usipy_sip_tm_uac_handle_invite_final(struct usipy_sip_tm *tm,
  struct usipy_sip_tm_txi *tp, size_t index, struct usipy_msg *msg,
  uint8_t sclass, uint64_t now_ms, int *deliver_responsep)
{
    struct usipy_sip_tm_txi *ackp;
    size_t ack_index;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(sclass >= 2);
    USIPY_DASSERT(deliver_responsep != NULL);

    usipy_sip_tm_tx_clear_invite_timeout(tp);
    tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_D;
    tp->pub.common.timer.value_ms = usipy_sip_tm_timer_d_ms(&tp->pub.common.timers);
    tp->pub.common.timer.due_at_ms = now_ms + tp->pub.common.timer.value_ms;
    tp->pub.state = USIPY_SIP_TM_STATE_COMPLETED;
    *deliver_responsep = (tp->final_reported == 0);
    if (tp->child_index == USIPY_SIP_TM_TX_INDEX_NONE) {
        const int rval = usipy_sip_tm_clone_uac_invite(tm, index,
          USIPY_SIP_METHOD_ACK, &ackp);

        if (rval != USIPY_SIP_TM_OK) {
            return (rval);
        }
        ack_index = (size_t)(ackp - tm->transactions);
        tp->child_index = ack_index;
    } else {
        ack_index = tp->child_index;
        ackp = &tm->transactions[ack_index];
        USIPY_DASSERT(ackp->active);
        USIPY_DASSERT(usipy_sip_tm_tx_is_ack(ackp));
    }
    if (ackp->outbound.pub.raw.l == 0) {
        if (sclass == 2) {
            const int rval = usipy_sip_tm_apply_uac_2xx_ack_dialog(tm, index, msg, ackp);

            if (rval != USIPY_SIP_TM_OK) {
                return (rval);
            }
        } else {
            struct usipy_str to_tag;

            if (usipy_sip_tm_extract_response_to_tag(msg, &to_tag) != 0) {
                return (USIPY_SIP_TM_ERR_BADMSG);
            }
            if (ackp->cache.branch.l == 0 && tp->cache.branch.l != 0 &&
              usipy_msg_heap_append(&ackp->scratch, &ackp->cache.branch,
                &tp->cache.branch) != 0) {
                return (USIPY_SIP_TM_ERR_NOSPC);
            }
            if (usipy_msg_heap_append(&ackp->scratch, &ackp->cache.to_tag, &to_tag) != 0) {
                return (USIPY_SIP_TM_ERR_NOSPC);
            }
        }
        ackp->outbound.checkpoint = usipy_msg_heap_checkpoint(&ackp->scratch);
        if (usipy_sip_tm_build_request(ackp, ack_index, tm, NULL) != USIPY_SIP_TM_OK) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    }
    usipy_sip_tm_uac_arm_send_now(ackp, now_ms);
    tp->final_reported = 1;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_handle_incoming_response(const struct usipy_sip_tm_handle_incoming_in *inp,
  struct usipy_msg *msg, const struct usipy_sip_tid *tidp,
  struct usipy_sip_tm_handle_incoming_out *outp)
{
    struct usipy_sip_tm *tm = inp->tm;
    const unsigned int scode = msg->sline.parsed.sl.status.code;
    const uint8_t sclass = (uint8_t)(scode / 100u);

    for (size_t i = 0; i < tm->max_transactions; i++) {
        struct usipy_sip_tm_txi *tp = &tm->transactions[i];
        int deliver_response = 1;

        if (!tp->active || tp->pub.role != USIPY_SIP_TM_ROLE_UAC) {
            continue;
        }
        if (!usipy_sip_tm_tid_matches_tx(tidp, &tp->pub)) {
            continue;
        }
        tp->pub.common.updated_at_ms = inp->now_ms;
        tp->pub.common.outbound.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
        tp->pub.common.outbound.raw = USIPY_STR_NULL;
        tp->outbound.pub = tp->pub.common.outbound;
        tp->pub.role_data.uac.last_status_code = (uint16_t)scode;
        tp->pub.role_data.uac.response_class = sclass;
        if (usipy_sip_tm_tx_is_invite(tp) && sclass == 1) {
            tp->pub.state = USIPY_SIP_TM_STATE_PROCEEDING;
            tp->invite_provisional_seen = 1;
            tp->invite_timeout_id = USIPY_SIP_TM_TIMEOUT_FR;
            if (tp->invite_cancel_state ==
              USIPY_SIP_TM_INVITE_CANCEL_SCHEDULED) {
                const int rval = usipy_sip_tm_uac_start_cancel(tm, i);

                if (rval != USIPY_SIP_TM_OK) {
                    return (rval);
                }
            }
        } else if (usipy_sip_tm_tx_is_invite(tp) && sclass >= 2) {
            const int rval = usipy_sip_tm_uac_handle_invite_final(tm, tp, i, msg,
              sclass, inp->now_ms, &deliver_response);

            if (rval != USIPY_SIP_TM_OK) {
                return (rval);
            }
        } else if (sclass >= 2) {
            usipy_sip_tm_tx_clear_invite_timeout(tp);
            usipy_sip_tm_tx_clear_timer(tp);
            tp->pub.state = USIPY_SIP_TM_STATE_COMPLETED;
        }
        if (deliver_response && tp->callbacks.response != NULL) {
            tp->callbacks.response(tp->callbacks.arg, i, &tp->pub, msg);
        }
        if (outp != NULL) {
            outp->error = USIPY_SIP_TM_OK;
            outp->consumed = 1;
            outp->match_kind = USIPY_SIP_TM_MATCH_EXISTING;
            outp->event = sclass < 2 ? USIPY_SIP_TM_EVENT_RESPONSE_1XX :
              USIPY_SIP_TM_EVENT_RESPONSE_FINAL;
            outp->transaction_index = i;
            outp->message = NULL;
        }
        if (sclass >= 2 && tp->pub.state != USIPY_SIP_TM_STATE_CALLING &&
          !usipy_sip_tm_tx_waits_for_invite_ack(tp)) {
            usipy_sip_tm_release_outbound(tp);
        }
        return (USIPY_SIP_TM_OK);
    }
    if (outp != NULL) {
        outp->error = USIPY_SIP_TM_ERR_NOT_FOUND;
    }
    return (USIPY_SIP_TM_ERR_NOT_FOUND);
}

static int
usipy_sip_tm_build_request(struct usipy_sip_tm_txi *tp, size_t tx_index,
  const struct usipy_sip_tm *tm, const struct usipy_sip_tm_build_request_params *bpp)
{
    struct usipy_msg tmsg = {0};
    const struct usipy_sip_tm_request_payload empty_payload = {0};
    const struct usipy_sip_tm_request_payload *payloadp =
      bpp != NULL && bpp->payload != NULL ? bpp->payload : &empty_payload;
    const struct usipy_sip_tm_extra_header *ehp =
      bpp != NULL ? bpp->extra_headers : NULL;
    const size_t neh = bpp != NULL ? bpp->nextra_headers : 0;
    const struct usipy_str *content_typep = &payloadp->content_type;
    const struct usipy_str *bodyp = &payloadp->body;
    const int include_expires = tp->pub.common.id.method_type == USIPY_SIP_METHOD_INVITE;
    const int include_ctype = bodyp->l != 0 && content_typep->l != 0;
    const size_t nbase_hdrs = 6 + tp->cache.uac.nroutes +
      (tp->cache.uac.include_contact != 0 ? 1 : 0) + (include_expires ? 1 : 0) +
      (include_ctype ? 1 : 0);
    struct usipy_sip_hdr thdrs[nbase_hdrs + neh];
    struct usipy_hdr_db_entr ehdb[neh];
    struct usipy_sip_tm_default_via via;
    struct usipy_sip_tm_default_nameaddr from;
    struct usipy_sip_tm_default_nameaddr to;
    struct usipy_sip_tm_default_nameaddr contact;
    const struct usipy_hdr_db_entr *hfp;
    struct usipy_str clen = USIPY_2STR("0");
    struct usipy_str expires = USIPY_STR_NULL;
    struct usipy_str rawmsg;
    size_t hindex;
    int rval;

    USIPY_DASSERT(tp->outbound.checkpoint != USIPY_MSG_HEAP_CHECKPOINT_NONE);
    usipy_msg_heap_rollback(&tp->scratch, tp->outbound.checkpoint);
    rval = usipy_sip_tm_prepare_ids(tm, tp, tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    memset(thdrs, '\0', sizeof(thdrs));
    via = tm->default_via;
    via.params[0].value = tp->cache.branch;
    from = tm->default_from;
    from.nameaddr.addr_spec = tp->cache.uac.from_uri;
    from.params[0].value = tp->cache.from_tag;
    to = tm->default_to;
    to.nameaddr.addr_spec = tp->cache.uac.to_uri;
    if (tp->cache.to_tag.l != 0) {
        to.nameaddr.nparams = 1;
        to.params[0].token = (struct usipy_str)USIPY_2STR("tag");
        to.params[0].value = tp->cache.to_tag;
    }
    contact = tm->default_contact;
    contact.nameaddr.addr_spec = tp->cache.uac.contact_uri;
    if (tp->cache.uac.include_contact != 0 && tp->cache.uac.contact_expires != 0) {
        if (usipy_msg_heap_sprintf(&tp->scratch, &expires, "%u",
          tp->cache.uac.contact_expires) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
        contact.nameaddr.nparams = 1;
        contact.params[0].token = (struct usipy_str)USIPY_2STR("expires");
        contact.params[0].value = expires;
    }
    if (include_expires) {
        if (usipy_msg_heap_sprintf(&tp->scratch, &expires, "%u",
          tp->cache.uac.invite_expires) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    }
    if (bodyp->l != 0) {
        if (content_typep->l == 0) {
            return (USIPY_SIP_TM_ERR_INVAL);
        }
        if (usipy_msg_heap_sprintf(&tp->scratch, &clen, "%zu",
          bodyp->l) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
    }

    tmsg.kind = USIPY_SIP_MSG_REQ;
    tmsg.sline.parsed.rl.method = tp->cache.cseq.method;
    tmsg.sline.parsed.rl.ruri = tp->cache.uac.request_uri;
    tmsg.sline.parsed.rl.version = (struct usipy_str)USIPY_2STR("SIP/2.0");
    tmsg.body = *bodyp;
    tmsg.hdrs = thdrs;
    tmsg.nhdrs = nbase_hdrs + neh;

    hindex = 0;
    hfp = usipy_hdr_db_byid(USIPY_HF_VIA);
    thdrs[hindex].hf_type = hfp;
    thdrs[hindex].parsed.via = &via.via;
    hindex += 1;

    hfp = usipy_hdr_db_byid(USIPY_HF_FROM);
    thdrs[hindex].hf_type = hfp;
    thdrs[hindex].parsed.from = &from.nameaddr;
    hindex += 1;

    hfp = usipy_hdr_db_byid(USIPY_HF_TO);
    thdrs[hindex].hf_type = hfp;
    thdrs[hindex].parsed.to = &to.nameaddr;
    hindex += 1;

    for (size_t i = 0; i < tp->cache.uac.nroutes; i++) {
        hfp = usipy_hdr_db_byid(USIPY_HF_ROUTE);
        thdrs[hindex].hf_type = hfp;
        thdrs[hindex].parsed.generic = &tp->cache.uac.routes[i];
        hindex += 1;
    }

    if (tp->cache.uac.include_contact != 0) {
        hfp = usipy_hdr_db_byid(USIPY_HF_CONTACT);
        thdrs[hindex].hf_type = hfp;
        thdrs[hindex].parsed.contact = &contact.nameaddr;
        hindex += 1;
    }

    hfp = usipy_hdr_db_byid(USIPY_HF_CALLID);
    thdrs[hindex].hf_type = hfp;
    thdrs[hindex].parsed.generic = &tp->cache.call_id;
    hindex += 1;

    hfp = usipy_hdr_db_byid(USIPY_HF_CSEQ);
    thdrs[hindex].hf_type = hfp;
    thdrs[hindex].parsed.cseq = &tp->cache.cseq;
    hindex += 1;

    if (include_expires) {
        hfp = usipy_hdr_db_byid(USIPY_HF_EXPIRES);
        thdrs[hindex].hf_type = hfp;
        thdrs[hindex].parsed.generic = &expires;
        hindex += 1;
    }
    if (include_ctype) {
        hfp = usipy_hdr_db_byid(USIPY_HF_CONTENTTYPE);
        thdrs[hindex].hf_type = hfp;
        thdrs[hindex].parsed.generic = content_typep;
        hindex += 1;
    }

    for (size_t i = 0; i < neh; i++) {
        hfp = usipy_hdr_db_byid(ehp[i].hf_type);
        if (hfp == NULL || hfp->cantype != ehp[i].hf_type) {
            return (USIPY_SIP_TM_ERR_INVAL);
        }
        thdrs[hindex].hf_type = hfp;
        if (ehp[i].value_kind == USIPY_SIP_TM_EH_PARSED) {
            switch (ehp[i].hf_type) {
            case USIPY_HF_AUTHORIZATION:
            case USIPY_HF_PROXYAUTHORIZATION:
                thdrs[hindex].parsed.authz =
                  (struct usipy_sip_hdr_authz *)ehp[i].parsed;
                break;

            default:
                return (USIPY_SIP_TM_ERR_INVAL);
            }
            hindex += 1;
            continue;
        }
        if (ehp[i].value_kind != USIPY_SIP_TM_EH_RAW) {
            return (USIPY_SIP_TM_ERR_INVAL);
        }
        if (hfp->build != NULL) {
            ehdb[i] = *hfp;
            ehdb[i].build = NULL;
            hfp = &ehdb[i];
            thdrs[hindex].hf_type = hfp;
        }
        thdrs[hindex].parsed.generic = &ehp[i].value;
        hindex += 1;
    }

    hfp = usipy_hdr_db_byid(USIPY_HF_CONTENTLENGTH);
    thdrs[hindex].hf_type = hfp;
    thdrs[hindex].parsed.generic = &clen;

    if (usipy_sip_msg_build(&tp->scratch, &tmsg, &rawmsg) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    tp->outbound.pub.raw = rawmsg;
    tp->pub.common.outbound = tp->outbound.pub;
    return (USIPY_SIP_TM_OK);
}

static uint32_t
usipy_sip_tm_uac_next_send_delay_ms(const struct usipy_sip_tm_txi *tp)
{
    uint64_t delay;

    USIPY_DASSERT(tp != NULL);
    if (usipy_sip_tm_tx_is_invite(tp)) {
        delay = tp->pub.common.timers.timer_a_ms != 0 ?
          tp->pub.common.timers.timer_a_ms : tp->pub.common.timers.t1_ms;
        for (uint8_t i = 1; i < tp->pub.common.retransmit_count; i++) {
            delay <<= 1;
        }
        USIPY_DASSERT(delay <= UINT32_MAX);
        return ((uint32_t)delay);
    }
    delay = tp->pub.common.timers.timer_e_ms != 0 ? tp->pub.common.timers.timer_e_ms :
      tp->pub.common.timers.t1_ms;

    for (uint8_t i = 1; i < tp->pub.common.retransmit_count; i++) {
        delay <<= 1;
        if (delay >= tp->pub.common.timers.t2_ms) {
            return (tp->pub.common.timers.t2_ms);
        }
    }
    USIPY_DASSERT(delay <= UINT32_MAX);
    return ((uint32_t)delay);
}

static void
usipy_sip_tm_run_out_init(struct usipy_sip_tm_run_out *outp)
{
    if (outp == NULL) {
        return;
    }
    memset(outp, '\0', sizeof(*outp));
    outp->next_run_at_ms = USIPY_SIP_TM_TIME_NONE;
}

static void
usipy_sip_tm_run_out_consider(struct usipy_sip_tm_run_out *outp, uint64_t when_ms)
{
    if (outp == NULL || when_ms == USIPY_SIP_TM_TIME_NONE) {
        return;
    }
    if (outp->next_run_at_ms == USIPY_SIP_TM_TIME_NONE || when_ms < outp->next_run_at_ms) {
        outp->next_run_at_ms = when_ms;
    }
}

static void
usipy_sip_tm_run_out_consider_timer(struct usipy_sip_tm_run_out *outp,
  const struct usipy_sip_tm_timer *tp)
{
    if (tp->type == USIPY_SIP_TM_TIMER_NONE) {
        return;
    }
    usipy_sip_tm_run_out_consider(outp, tp->due_at_ms);
}

static int
usipy_sip_tm_uac_note_send(struct usipy_sip_tm_txi *tp, uint64_t now_ms)
{
    USIPY_DASSERT(tp != NULL);

    if (tp->pub.common.created_at_ms == 0 && tp->pub.common.retransmit_count == 0) {
        tp->pub.common.created_at_ms = now_ms;
    }
    tp->pub.common.updated_at_ms = now_ms;
    tp->pub.common.retransmit_count += 1;
    return (0);
}

static void
usipy_sip_tm_uac_post_send_ack(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
}

static void
usipy_sip_tm_uac_post_send_invite(struct usipy_sip_tm_txi *tp, uint64_t now_ms)
{
    USIPY_DASSERT(tp != NULL);

    if (usipy_sip_tm_tx_waits_for_invite_ack(tp)) {
        tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
        return;
    }
    if (tp->invite_timeout_at_ms == USIPY_SIP_TM_TIME_NONE) {
        tp->invite_timeout_at_ms = now_ms + ((uint64_t)tp->cache.uac.invite_expires * 1000u);
        tp->invite_timeout_id = USIPY_SIP_TM_TIMEOUT_PR;
    }
    if (tp->invite_provisional_seen != 0 ||
      (tp->pub.common.flags & USIPY_SIP_TM_F_RELIABLE_TRANSPORT) != 0) {
        tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
        return;
    }
    tp->outbound.pub.next_send_at_ms = now_ms + usipy_sip_tm_uac_next_send_delay_ms(tp);
}

static void
usipy_sip_tm_uac_post_send_noninvite(struct usipy_sip_tm_txi *tp, uint64_t now_ms)
{
    USIPY_DASSERT(tp != NULL);

    if (tp->pub.common.timer.type == USIPY_SIP_TM_TIMER_NONE) {
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_F;
        tp->pub.common.timer.value_ms = usipy_sip_tm_timer_f_ms(&tp->pub.common.timers);
        tp->pub.common.timer.due_at_ms = now_ms + tp->pub.common.timer.value_ms;
    }
    if ((tp->pub.common.flags & USIPY_SIP_TM_F_RELIABLE_TRANSPORT) != 0) {
        tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
        return;
    }
    tp->outbound.pub.next_send_at_ms = now_ms + usipy_sip_tm_uac_next_send_delay_ms(tp);
}

int
usipy_sip_tm_uac_run(struct usipy_sip_tm_txi *tp, size_t index, const struct usipy_sip_tm *tm,
  const struct usipy_sip_tm_run_in *inp, struct usipy_sip_tm_run_out *outp)
{
    enum usipy_sip_tm_uac_timeout_id timeout_id;
    int rval;

    if (tp->pub.state == USIPY_SIP_TM_STATE_TERMINATED ||
      (tp->pub.common.flags & USIPY_SIP_TM_F_TERMINATED) != 0) {
        return (USIPY_SIP_TM_OK);
    }
    if (tp->invite_timeout_at_ms != USIPY_SIP_TM_TIME_NONE &&
      tp->invite_timeout_at_ms <= inp->now_ms) {
        timeout_id = (enum usipy_sip_tm_uac_timeout_id)tp->invite_timeout_id;
        if (timeout_id == USIPY_SIP_TM_TIMEOUT_FR) {
            rval = usipy_sip_tm_uac_start_cancel((struct usipy_sip_tm *)tm, index);
            if (rval != USIPY_SIP_TM_OK) {
                return (rval);
            }
        }
        usipy_sip_tm_tx_mark_terminated(tp);
        usipy_sip_tm_tx_clear_invite_timeout(tp);
        if (tp->callbacks.timeout != NULL) {
            tp->callbacks.timeout(tp->callbacks.arg, index, &tp->pub, timeout_id);
        }
        usipy_sip_tm_release_outbound(tp);
        if (outp != NULL) {
            outp->ntimeouts += 1;
        }
        return (USIPY_SIP_TM_OK);
    }
    if (tp->pub.common.timer.type != USIPY_SIP_TM_TIMER_NONE &&
      tp->pub.common.timer.due_at_ms <= inp->now_ms) {
        usipy_sip_tm_tx_mark_terminated(tp);
        usipy_sip_tm_tx_clear_timer(tp);
        usipy_sip_tm_tx_terminate_child((struct usipy_sip_tm *)tm, &tp->child_index);
        if (tp->final_reported == 0 && tp->callbacks.timeout != NULL) {
            tp->callbacks.timeout(tp->callbacks.arg, index, &tp->pub,
              USIPY_SIP_TM_TIMEOUT_NONE);
        }
        if (tp->pub.state != USIPY_SIP_TM_STATE_CALLING) {
            usipy_sip_tm_release_outbound(tp);
        }
        if (outp != NULL) {
            outp->ntimeouts += 1;
        }
        return (USIPY_SIP_TM_OK);
    }
    if (tp->outbound.pub.next_send_at_ms == USIPY_SIP_TM_TIME_NONE ||
      tp->outbound.pub.next_send_at_ms > inp->now_ms) {
        usipy_sip_tm_run_out_consider(outp, tp->outbound.pub.next_send_at_ms);
        usipy_sip_tm_run_out_consider(outp, tp->invite_timeout_at_ms);
        usipy_sip_tm_run_out_consider_timer(outp, &tp->pub.common.timer);
        return (USIPY_SIP_TM_OK);
    }
    USIPY_DASSERT(inp->send_to != NULL);
    if (tp->outbound.pub.raw.l == 0) {
        rval = usipy_sip_tm_build_request(tp, index, tm, NULL);
        if (rval != 0) {
            return (rval);
        }
    }
    rval = inp->send_to(inp->send_to_arg, index, &tp->pub, &tp->outbound.pub);
    if (rval != 0) {
        return (rval);
    }
    usipy_sip_tm_uac_note_send(tp, inp->now_ms);
    if (usipy_sip_tm_tx_is_ack(tp)) {
        usipy_sip_tm_uac_post_send_ack(tp);
    } else if (usipy_sip_tm_tx_is_invite(tp)) {
        usipy_sip_tm_uac_post_send_invite(tp, inp->now_ms);
    } else {
        usipy_sip_tm_uac_post_send_noninvite(tp, inp->now_ms);
    }
    tp->pub.common.outbound = tp->outbound.pub;
    if (outp != NULL) {
        outp->nsent += 1;
    }
    usipy_sip_tm_run_out_consider(outp, tp->outbound.pub.next_send_at_ms);
    usipy_sip_tm_run_out_consider(outp, tp->invite_timeout_at_ms);
    usipy_sip_tm_run_out_consider_timer(outp, &tp->pub.common.timer);
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_new_uac_tr(struct usipy_sip_tm *tm,
  const struct usipy_sip_tm_new_uac_tr_params *tpp, size_t *indexp)
{
    struct usipy_sip_tm_txi *tp;
    const struct usipy_method_db_entr *mdp;
    const struct usipy_sip_tm_timer_policy timers =
      USIPY_SIP_TM_TIMER_POLICY_RFC3261;
    int rval;
    size_t tx_index;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tpp != NULL);
    USIPY_DASSERT(indexp != NULL);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    if (tpp->request_id.call_id.l == 0 ||
      tpp->request_target.request_uri.l == 0) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    rval = usipy_sip_tm_lookup_uac_method(tpp->request_id.method_type, 0, &mdp);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    tp = usipy_sip_tm_alloc_slot(tm, &tx_index);
    if (tp == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    rval = usipy_sip_tm_init_uac_request(tp, &tpp->request_id,
      &tpp->request_target, mdp);
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    rval = usipy_sip_tm_init_uac_parties_by_username(tp, tm,
      &tpp->parties_by_username);
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    tp->cache.uac.contact_expires = tpp->contact_expires;
    tp->cache.uac.invite_expires = tpp->invite_expires != 0 ? tpp->invite_expires : 300u;
    usipy_sip_tm_activate_uac_slot(tm, tp,
      tpp->request_id.method_type == USIPY_SIP_METHOD_INVITE ?
      USIPY_SIP_TM_STATE_CALLING : USIPY_SIP_TM_STATE_TRYING,
      &tpp->request_id, &tpp->request_target, &tpp->callbacks, &timers);
    rval = usipy_sip_tm_build_request(tp, tx_index, tm,
      &(struct usipy_sip_tm_build_request_params){
        .payload = &(struct usipy_sip_tm_request_payload){
            .content_type = tpp->content_type,
            .body = tpp->body,
        },
      });
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    *indexp = tx_index;
    return (USIPY_SIP_TM_OK);

nospc:
    usipy_sip_tm_tx_fini(tp);
    return (USIPY_SIP_TM_ERR_NOSPC);
}

int
usipy_sip_tm_new_in_dialog_transaction(struct usipy_sip_tm *tm,
  const struct usipy_sip_tm_new_in_dialog_transaction_params *tpp, size_t *indexp)
{
    struct usipy_sip_tm_txi *tp;
    const struct usipy_method_db_entr *mdp;
    struct usipy_sip_tm_timer_policy timers;
    int rval;
    size_t tx_index;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tpp != NULL);
    USIPY_DASSERT(indexp != NULL);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    if (tpp->request_id.method_type > USIPY_SIP_METHOD_max ||
      tpp->request_id.call_id.l == 0 ||
      tpp->request_target.request_uri.l == 0 ||
      tpp->parties_by_uri.contact.l == 0 || tpp->parties_by_uri.from.l == 0 ||
      tpp->parties_by_uri.to.l == 0 ||
      (tpp->route_set.nroutes != 0 && tpp->route_set.routes == NULL) ||
      tpp->dialog_tags.local_tag.l == 0 ||
      tpp->dialog_tags.remote_tag.l == 0) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    rval = usipy_sip_tm_lookup_uac_method(tpp->request_id.method_type, 1, &mdp);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    tp = usipy_sip_tm_alloc_slot(tm, &tx_index);
    if (tp == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    rval = usipy_sip_tm_init_uac_request(tp, &tpp->request_id,
      &tpp->request_target, mdp);
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    rval = usipy_sip_tm_init_uac_parties_by_uri(tp, &tpp->parties_by_uri,
      &tpp->dialog_tags);
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    rval = usipy_sip_tm_copy_route_set(&tp->scratch, &tp->cache.uac.routes,
      tpp->route_set.nroutes, tpp->route_set.routes);
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    usipy_sip_tm_set_route_set(tp, tp->cache.uac.routes, tpp->route_set.nroutes);
    timers = tpp->timers.t1_ms != 0 ? tpp->timers :
      (struct usipy_sip_tm_timer_policy)USIPY_SIP_TM_TIMER_POLICY_RFC3261;
    usipy_sip_tm_activate_uac_slot(tm, tp, USIPY_SIP_TM_STATE_TRYING,
      &tpp->request_id, &tpp->request_target, &tpp->callbacks, &timers);
    rval = usipy_sip_tm_prepare_ids(tm, tp, tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        goto nospc;
    }
    *indexp = tx_index;
    return (USIPY_SIP_TM_OK);

nospc:
    usipy_sip_tm_tx_fini(tp);
    return (USIPY_SIP_TM_ERR_NOSPC);
}

int
usipy_sip_tm_gen_authz_hf(const struct usipy_sip_tm *tm, size_t index, uint8_t hf_type,
  struct usipy_msg_heap *mhp, const struct usipy_sip_hdr_auth *challengep,
  const struct usipy_str *usernamep, const struct usipy_str *passwordp,
  const struct usipy_str *bodyp, const struct usipy_str *qopp,
  struct usipy_sip_tm_extra_header *ehp)
{
    const struct usipy_sip_tm_txi *tp;
    struct usipy_sip_tm_build_uri_arg uarg;
    struct usipy_sip_hdr_authz *authzp;
    struct usipy_str uris;

    if (tm == NULL || mhp == NULL || challengep == NULL || usernamep == NULL ||
      passwordp == NULL || ehp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (hf_type != USIPY_HF_AUTHORIZATION &&
      hf_type != USIPY_HF_PROXYAUTHORIZATION) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (index >= tm->max_transactions) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    tp = &tm->transactions[index];
    if (!tp->active) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    USIPY_DASSERT(tp->pub.role == USIPY_SIP_TM_ROLE_UAC);
    uarg.urip = tp->cache.uac.request_uri;
    if (usipy_msg_heap_build(mhp, &uris, &uarg, usipy_sip_tm_build_uri_cb) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    authzp = gen_auth_hf(mhp, challengep, usernamep, passwordp,
      &usipy_method_db[tp->cache.method_type].name, &uris, bodyp, qopp);
    if (authzp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    ehp->hf_type = hf_type;
    ehp->value_kind = USIPY_SIP_TM_EH_PARSED;
    ehp->value = USIPY_STR_NULL;
    ehp->parsed = authzp;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_next_transaction(struct usipy_sip_tm *tm, size_t index,
  const struct usipy_sip_tm_request_payload *payloadp,
  const struct usipy_sip_tm_extra_header *ehp, size_t neh)
{
    struct usipy_sip_tm_txi *tp;
    struct usipy_sip_tm_txi *childp;
    int rval;

    USIPY_DASSERT(tm != NULL);
    if (neh != 0 && ehp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (index >= tm->max_transactions) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    tp = &tm->transactions[index];
    if (!tp->active) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    if (tp->child_index != USIPY_SIP_TM_TX_INDEX_NONE) {
        USIPY_DASSERT(tp->child_index < tm->max_transactions);
        childp = &tm->transactions[tp->child_index];
        if (!childp->active || !usipy_sip_tm_tx_is_ack(childp)) {
            return (USIPY_SIP_TM_ERR_UNSUPPORTED);
        }
        tp->child_index = USIPY_SIP_TM_TX_INDEX_NONE;
    }
    if (tp->invite_cancel_state == USIPY_SIP_TM_INVITE_CANCEL_SCHEDULED) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    rval = usipy_sip_tm_transition(tp, USIPY_SIP_TM_STATE_CALLING);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    tp->cache.cseq.val += 1;
    tp->pub.common.id.cseq = tp->cache.cseq.val;
    usipy_sip_tm_tx_reset_uac_runtime(tp);
    return (usipy_sip_tm_build_request(tp, index, tm,
      &(struct usipy_sip_tm_build_request_params){
        .payload = payloadp,
        .extra_headers = ehp,
        .nextra_headers = neh,
      }));
}

int
usipy_sip_tm_cancel(struct usipy_sip_tm *tm, size_t index)
{
    struct usipy_sip_tm_txi *tp;

    if (tm == NULL || index >= tm->max_transactions) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    tp = &tm->transactions[index];
    if (!tp->active) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    USIPY_DASSERT(tp->pub.role == USIPY_SIP_TM_ROLE_UAC);
    if (!usipy_sip_tm_tx_is_invite(tp) ||
      tp->pub.state == USIPY_SIP_TM_STATE_COMPLETED ||
      tp->pub.state == USIPY_SIP_TM_STATE_TERMINATED) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (tp->child_index != USIPY_SIP_TM_TX_INDEX_NONE || tp->final_reported != 0) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    return (usipy_sip_tm_uac_start_cancel(tm, index));
}
