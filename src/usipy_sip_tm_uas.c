#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "usipy_port/network.h"

#include "usipy_debug.h"
#include "public/usipy_platform.h"
#include "public/usipy_sip_tm.h"
#include "usipy_append_priv.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_method_db.h"
#include "usipy_sip_res.h"
#include "usipy_sip_tid.h"
#include "usipy_sip_tm_internal.h"
#include "usipy_sip_tm_priv.h"

static void usipy_sip_tm_uas_run_out_consider(struct usipy_sip_tm_run_out *, uint64_t);
static void usipy_sip_tm_uas_mark_terminated(struct usipy_sip_tm_txi *);

static uint32_t
usipy_sip_tm_timer_j_ms(const struct usipy_sip_tm_timer_policy *tp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tp->timer_j_ms != 0 || tp->t1_ms != 0);

    if (tp->timer_j_ms != 0) {
        return (tp->timer_j_ms);
    }
    return (tp->t1_ms * 64u);
}

static uint32_t
usipy_sip_tm_timer_h_ms(const struct usipy_sip_tm_timer_policy *tp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tp->t1_ms != 0);

    return (tp->t1_ms * 64u);
}

static uint32_t
usipy_sip_tm_timer_i_ms(const struct usipy_sip_tm_timer_policy *tp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tp->t4_ms != 0);

    return (tp->t4_ms);
}

static int
usipy_sip_tm_uas_is_invite_non2xx_final(const struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    return (tp->cache.method_type == USIPY_SIP_METHOD_INVITE &&
      tp->pub.role_data.uas.last_status_code >= 300);
}

static uint32_t
usipy_sip_tm_uas_invite_next_send_delay_ms(const struct usipy_sip_tm_txi *tp)
{
    uint64_t delay_ms;
    uint32_t t1_ms;
    uint32_t t2_ms;
    uint8_t shift;

    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(tp->pub.common.timers.t1_ms != 0);
    USIPY_DASSERT(tp->pub.common.timers.t2_ms != 0);

    t1_ms = tp->pub.common.timers.t1_ms;
    t2_ms = tp->pub.common.timers.t2_ms;
    shift = tp->pub.common.retransmit_count > 0 ? tp->pub.common.retransmit_count - 1 : 0;
    if (shift >= 31) {
        return (t2_ms);
    }
    delay_ms = ((uint64_t)t1_ms) << shift;
    if (delay_ms > t2_ms) {
        delay_ms = t2_ms;
    }
    return ((uint32_t)delay_ms);
}

static uint32_t
usipy_sip_tm_uas_method_hash(const struct usipy_sip_tm_txi *tp,
  uint8_t method_type)
{
    struct usipy_sip_hdr_cseq cseq = {
      .val = tp->pub.common.id.cseq,
      .method = &usipy_method_db[method_type],
    };
    struct usipy_sip_tid tid = {
      .call_id = &tp->pub.common.id.call_id,
      .from_tag = &tp->pub.common.id.from_tag,
      .vbranch = &tp->pub.common.id.branch,
      .cseq = &cseq,
    };

    USIPY_DASSERT(tp != NULL);

    return (usipy_sip_tid_hash(&tid));
}

static int
usipy_sip_tm_uas_ack_matches_tx(const struct usipy_sip_tid *tidp,
  const struct usipy_str *to_tagp, uint32_t dialog_hash,
  const struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(tp != NULL);

    if (tidp->cseq->method->cantype != USIPY_SIP_METHOD_ACK ||
      tp->pub.role != USIPY_SIP_TM_ROLE_UAS ||
      tp->pub.common.id.method_type != USIPY_SIP_METHOD_INVITE ||
      tp->pub.state != USIPY_SIP_TM_STATE_COMPLETED ||
      tp->cache.uas.ack_hash == 0) {
        return (0);
    }
    if (tp->pub.role_data.uas.last_status_code >= 200 &&
      tp->pub.role_data.uas.last_status_code < 300) {
        if (to_tagp == NULL || to_tagp->l == 0 ||
          dialog_hash != tp->cache.uas.ack_hash) {
            return (0);
        }
        if (!usipy_str_eq(tidp->call_id, &tp->pub.common.id.call_id) ||
          !usipy_str_eq(tidp->from_tag, &tp->pub.common.id.from_tag) ||
          !usipy_str_eq(to_tagp, &tp->cache.to_tag)) {
            return (0);
        }
    } else {
        if (tidp->hash != tp->cache.uas.ack_hash) {
            return (0);
        }
        if (!usipy_str_eq(tidp->call_id, &tp->pub.common.id.call_id) ||
          !usipy_str_eq(tidp->from_tag, &tp->pub.common.id.from_tag) ||
          !usipy_str_eq(tidp->vbranch, &tp->pub.common.id.branch)) {
            return (0);
        }
    }
    return (tidp->cseq->val == tp->pub.common.id.cseq);
}

static int
usipy_sip_tm_find_uas_ack_transaction(const struct usipy_sip_tm *tm,
  struct usipy_msg *msg, const struct usipy_sip_tid *tidp, size_t *indexp)
{
    struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *top;
    const struct usipy_str *to_tagp = NULL;
    uint32_t dialog_hash = 0;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(indexp != NULL);

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(msg->nhdrs));
    *matchp = (struct usipy_sip_hdr_match){.hdrslen = msg->nhdrs};
    if (usipy_sip_msg_parse_hdrs_get(msg, USIPY_HFT_MASK(USIPY_HF_TO), 1,
      matchp) != 0 || matchp->nhdrs == 0) {
        *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
        return (-1);
    }
    top = matchp->hdrsp[0]->parsed.to;
    if (top == NULL) {
        *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
        return (-1);
    }
    to_tagp = usipy_sip_hdr_nameaddr_get_param(top, "tag");
    if (to_tagp != NULL && to_tagp->l != 0) {
        dialog_hash = usipy_sip_dialog_tid_hash(tidp->call_id, tidp->from_tag,
          to_tagp, tidp->cseq->val, USIPY_SIP_METHOD_ACK);
    }
    for (size_t i = 0; i < tm->max_transactions; i++) {
        const struct usipy_sip_tm_txi *tp = &tm->transactions[i];

        if (!tp->active) {
            continue;
        }
        if (usipy_sip_tm_uas_ack_matches_tx(tidp, to_tagp, dialog_hash, tp)) {
            *indexp = i;
            return (0);
        }
    }
    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    return (-1);
}

static void
usipy_sip_tm_uas_post_send_invite_final(struct usipy_sip_tm_txi *tp,
  const struct usipy_sip_tm_run_in *inp, struct usipy_sip_tm_run_out *outp)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(inp != NULL);

    if (tp->pub.common.timer.due_at_ms == USIPY_SIP_TM_TIME_NONE) {
        tp->pub.common.timer.due_at_ms = inp->now_ms + tp->pub.common.timer.value_ms;
    }
    if ((tp->pub.common.flags & USIPY_SIP_TM_F_RELIABLE_TRANSPORT) == 0) {
        tp->outbound.pub.next_send_at_ms = inp->now_ms +
          usipy_sip_tm_uas_invite_next_send_delay_ms(tp);
        tp->pub.common.outbound = tp->outbound.pub;
        usipy_sip_tm_uas_run_out_consider(outp, tp->outbound.pub.next_send_at_ms);
    }
    usipy_sip_tm_uas_run_out_consider(outp, tp->pub.common.timer.due_at_ms);
}

static void
usipy_sip_tm_uas_post_send_final(struct usipy_sip_tm_txi *tp, uint64_t now_ms,
  struct usipy_sip_tm_run_out *outp)
{
    USIPY_DASSERT(tp != NULL);

    if ((tp->pub.common.flags & USIPY_SIP_TM_F_RELIABLE_TRANSPORT) != 0) {
        usipy_sip_tm_uas_mark_terminated(tp);
    } else {
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_J;
        tp->pub.common.timer.value_ms = usipy_sip_tm_timer_j_ms(&tp->pub.common.timers);
        tp->pub.common.timer.due_at_ms = now_ms + tp->pub.common.timer.value_ms;
        usipy_sip_tm_uas_run_out_consider(outp, tp->pub.common.timer.due_at_ms);
    }
}

static int
usipy_sip_tm_uas_handle_ack(const struct usipy_sip_tm_handle_incoming_in *inp,
  struct usipy_sip_tm_handle_incoming_out *outp, struct usipy_sip_tm_txi *tp,
  size_t tx_index)
{
    USIPY_DASSERT(inp != NULL);
    USIPY_DASSERT(tp != NULL);

    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
    if ((tp->pub.common.flags & USIPY_SIP_TM_F_RELIABLE_TRANSPORT) != 0) {
        usipy_sip_tm_uas_mark_terminated(tp);
    } else {
        tp->pub.state = USIPY_SIP_TM_STATE_CONFIRMED;
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_I;
        tp->pub.common.timer.value_ms = usipy_sip_tm_timer_i_ms(&tp->pub.common.timers);
        tp->pub.common.timer.due_at_ms = inp->now_ms + tp->pub.common.timer.value_ms;
    }
    if (outp != NULL) {
        outp->error = USIPY_SIP_TM_OK;
        outp->consumed = 1;
        outp->match_kind = USIPY_SIP_TM_MATCH_EXISTING;
        outp->event = USIPY_SIP_TM_EVENT_ACK_RX;
        outp->transaction_index = tx_index;
        outp->message = NULL;
    }
    return (USIPY_SIP_TM_OK);
}

static void
usipy_sip_tm_uas_report_consumed_request(
  struct usipy_sip_tm_handle_incoming_out *outp, size_t tx_index)
{
    if (outp != NULL) {
        outp->error = USIPY_SIP_TM_OK;
        outp->consumed = 1;
        outp->match_kind = USIPY_SIP_TM_MATCH_EXISTING;
        outp->event = USIPY_SIP_TM_EVENT_REQUEST_RX;
        outp->transaction_index = tx_index;
        outp->message = NULL;
    }
}

static int
usipy_sip_tm_uas_handle_cancel(struct usipy_sip_tm_handle_incoming_out *outp,
  struct usipy_sip_tm_txi *tp, size_t tx_index, const struct usipy_msg *msg)
{
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(msg != NULL);

    if (tp->uas_callbacks.cancel != NULL) {
        tp->uas_callbacks.cancel(tp->uas_callbacks.arg, tx_index, &tp->pub, msg);
    }
    usipy_sip_tm_uas_report_consumed_request(outp, tx_index);
    return (USIPY_SIP_TM_OK);
}

static void
usipy_sip_tm_uas_run_out_consider(struct usipy_sip_tm_run_out *outp, uint64_t when_ms)
{
    USIPY_DASSERT(outp != NULL);

    if (when_ms == USIPY_SIP_TM_TIME_NONE) {
        return;
    }
    if (outp->next_run_at_ms == USIPY_SIP_TM_TIME_NONE || when_ms < outp->next_run_at_ms) {
        outp->next_run_at_ms = when_ms;
    }
}

static void
usipy_sip_tm_uas_mark_terminated(struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tp != NULL);

    tp->pub.state = USIPY_SIP_TM_STATE_TERMINATED;
    tp->pub.common.flags |= USIPY_SIP_TM_F_TERMINATED;
    tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
}

static void
usipy_sip_tm_uas_scode2str(unsigned int scode, char *res)
{
    res[0] = '0' + (scode / 100);
    res[1] = '0' + (scode % 100) / 10;
    res[2] = '0' + (scode % 10);
    res[3] = ' ';
}

static int
usipy_sip_tm_uas_copy_hdrs(struct usipy_msg_heap *mhp, struct usipy_str **dstpp,
  size_t *ndstp, const struct usipy_sip_hdr *const *srcp, size_t nsrc)
{
    struct usipy_str *dstp;

    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(dstpp != NULL);
    USIPY_DASSERT(ndstp != NULL);

    *dstpp = NULL;
    *ndstp = 0;
    if (nsrc == 0) {
        return (0);
    }
    dstp = usipy_msg_heap_alloc(mhp, sizeof(*dstp) * nsrc);
    if (dstp == NULL) {
        return (-1);
    }
    for (size_t i = 0; i < nsrc; i++) {
        if (usipy_msg_heap_append(mhp, &dstp[i], &srcp[i]->onwire.value) != 0) {
            return (-1);
        }
    }
    *dstpp = dstp;
    *ndstp = nsrc;
    return (0);
}

static int
usipy_sip_tm_uas_rebase_substr(const struct usipy_str *src_basep,
  const struct usipy_str *dst_basep, const struct usipy_str *srcp,
  struct usipy_str *dstp)
{
    size_t off;

    USIPY_DASSERT(src_basep != NULL);
    USIPY_DASSERT(dst_basep != NULL);
    USIPY_DASSERT(srcp != NULL);
    USIPY_DASSERT(dstp != NULL);

    if (srcp->l == 0) {
        *dstp = USIPY_STR_NULL;
        return (0);
    }
    if (srcp->s.ro < src_basep->s.ro) {
        return (-1);
    }
    off = (size_t)(srcp->s.ro - src_basep->s.ro);
    if (off > src_basep->l || srcp->l > src_basep->l - off) {
        return (-1);
    }
    dstp->s.ro = dst_basep->s.ro + off;
    dstp->l = srcp->l;
    return (0);
}

static int
usipy_sip_tm_uas_cache_request(const struct usipy_msg *reqp,
  const struct usipy_sip_tid *tidp, struct usipy_sip_tm_txi *tp)
{
    uint64_t parse_mask;
    const struct usipy_sip_hdr *fromp = NULL, *top = NULL, *cseqp = NULL, *contactp = NULL;
    const struct usipy_sip_hdr *viasp[32];
    const struct usipy_sip_hdr *rrsp[32];
    size_t nvias = 0, nrrs = 0;

    USIPY_DASSERT(reqp != NULL);
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(tp != NULL);
    parse_mask = USIPY_HFT_MASK(USIPY_HF_FROM) | USIPY_HFT_MASK(USIPY_HF_TO) |
      USIPY_HFT_MASK(USIPY_HF_CSEQ);
    if (USIPY_MSG_HDR_PRESENT(reqp, USIPY_HF_CONTACT)) {
        parse_mask |= USIPY_HFT_MASK(USIPY_HF_CONTACT);
    }
    if (usipy_sip_msg_parse_hdrs((struct usipy_msg *)reqp, parse_mask, 0) != 0) {
        return (-1);
    }

    for (unsigned int i = 0; i < reqp->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = &reqp->hdrs[i];

        switch (shp->hf_type->cantype) {
        case USIPY_HF_VIA:
            USIPY_DASSERT(nvias < sizeof(viasp) / sizeof(viasp[0]));
            viasp[nvias++] = shp;
            break;

        case USIPY_HF_FROM:
            if (fromp == NULL) {
                fromp = shp;
            }
            break;

        case USIPY_HF_TO:
            if (top == NULL) {
                top = shp;
            }
            break;

        case USIPY_HF_CSEQ:
            if (cseqp == NULL) {
                cseqp = shp;
            }
            break;

        case USIPY_HF_CONTACT:
            if (contactp == NULL) {
                contactp = shp;
            }
            break;

        case USIPY_HF_RECORDROUTE:
            USIPY_DASSERT(nrrs < sizeof(rrsp) / sizeof(rrsp[0]));
            rrsp[nrrs++] = shp;
            break;
        }
    }
    USIPY_DASSERT(fromp != NULL);
    USIPY_DASSERT(top != NULL);
    USIPY_DASSERT(cseqp != NULL);
    if (nvias == 0) {
        return (-1);
    }
    if (usipy_msg_heap_append(&tp->scratch, &tp->cache.uas.from,
      &fromp->onwire.value) != 0) {
        return (-1);
    }
    if (usipy_msg_heap_append(&tp->scratch, &tp->cache.uas.to, &top->onwire.value) != 0) {
        return (-1);
    }
    if (usipy_msg_heap_append(&tp->scratch, &tp->cache.uas.request_uri,
      &reqp->sline.parsed.rl.onwire.ruri) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_uas_rebase_substr(&fromp->onwire.value, &tp->cache.uas.from,
      &fromp->parsed.from->addr_spec, &tp->cache.uas.from_uri) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_uas_rebase_substr(&top->onwire.value, &tp->cache.uas.to,
      &top->parsed.to->addr_spec, &tp->cache.uas.to_uri) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_uas_copy_hdrs(&tp->scratch, &tp->cache.uas.vias,
      &tp->cache.uas.nvias, viasp, nvias) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_uas_rebase_substr(&viasp[0]->onwire.value, &tp->cache.uas.vias[0],
      tidp->vbranch, &tp->cache.branch) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_uas_copy_hdrs(&tp->scratch, &tp->cache.uas.record_routes,
      &tp->cache.uas.nrecord_routes, rrsp, nrrs) != 0) {
        return (-1);
    }
    if (contactp != NULL && contactp->parsed.contact != NULL &&
      contactp->parsed.contact->addr_spec.l != 0 &&
      usipy_msg_heap_append(&tp->scratch, &tp->cache.uas.contact_uri,
        &contactp->parsed.contact->addr_spec) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_uas_rebase_substr(&fromp->onwire.value, &tp->cache.uas.from,
      tidp->from_tag, &tp->cache.from_tag) != 0) {
        return (-1);
    }
    tp->cache.cseq = *cseqp->parsed.cseq;
    return (0);
}

static int
usipy_sip_tm_uas_format_local_contact_uri(struct usipy_msg_heap *mhp,
  const struct usipy_sip_tm_addr *localp, struct usipy_str *urip)
{
    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(localp != NULL);
    USIPY_DASSERT(urip != NULL);

    switch (localp->af) {
    case AF_INET:
        return (usipy_msg_heap_sprintf(mhp, urip, "sip:%.*s:%u",
          USIPY_SFMT(&localp->host), localp->port));

#ifdef IPPROTO_IPV6
    case AF_INET6:
        return (usipy_msg_heap_sprintf(mhp, urip, "sip:[%.*s]:%u",
          USIPY_SFMT(&localp->host), localp->port));
#endif

    default:
        return (-1);
    }
}

struct usipy_sip_tm_uas_build_arg {
    const struct usipy_sip_tm *tm;
    const struct usipy_sip_tm_txi *tp;
    const struct usipy_sip_tm_uas_response_params *rpp;
};

static int
usipy_sip_tm_uas_build_response_cb(void *arg, char *buf, size_t len)
{
    static const struct usipy_str sip20_sp = USIPY_2STR("SIP/2.0 ");
    static const struct usipy_str colon_sp = USIPY_2STR(": ");
    static const struct usipy_str tag_param = USIPY_2STR(";tag=");
    const struct usipy_sip_tm_uas_build_arg *barg = arg;
    const struct usipy_sip_tm *tm = barg->tm;
    const struct usipy_sip_tm_txi *tp = barg->tp;
    const struct usipy_sip_tm_uas_response_params *rpp = barg->rpp;
    const struct usipy_sip_status *slp = &rpp->status;
    const struct usipy_str *serverp = USIPY_PLATFORM.get_server();
    const struct usipy_hdr_db_entr *via_hfp = usipy_hdr_db_byid(USIPY_HF_VIA);
    const struct usipy_hdr_db_entr *from_hfp = usipy_hdr_db_byid(USIPY_HF_FROM);
    const struct usipy_hdr_db_entr *to_hfp = usipy_hdr_db_byid(USIPY_HF_TO);
    const struct usipy_hdr_db_entr *callid_hfp = usipy_hdr_db_byid(USIPY_HF_CALLID);
    const struct usipy_hdr_db_entr *cseq_hfp = usipy_hdr_db_byid(USIPY_HF_CSEQ);
    const struct usipy_hdr_db_entr *rr_hfp = usipy_hdr_db_byid(USIPY_HF_RECORDROUTE);
    const struct usipy_hdr_db_entr *contact_hfp = usipy_hdr_db_byid(USIPY_HF_CONTACT);
    const struct usipy_hdr_db_entr *server_hfp = usipy_hdr_db_byid(USIPY_HF_SERVER);
    const struct usipy_hdr_db_entr *ctype_hfp = usipy_hdr_db_byid(USIPY_HF_CONTENTTYPE);
    const struct usipy_hdr_db_entr *clen_hfp = usipy_hdr_db_byid(USIPY_HF_CONTENTLENGTH);
    const struct usipy_sip_tm_extra_header *ehp = rpp->extra_headers;
    const size_t neh = rpp->nextra_headers;
    char clen_buf[32];
    struct usipy_hdr_db_entr ehdb[neh != 0 ? neh : 1];
    size_t off = 0;
    char sbuf[4];
    int rval;

#define APPEND_MEM(bp, blen) USIPY_APPEND_MEM_OR_RETURN(-1, buf, off, len, bp, blen)
#define APPEND_STR(sp) USIPY_APPEND_STR_OR_RETURN(-1, buf, off, len, sp)
    USIPY_DASSERT(via_hfp != NULL);
    USIPY_DASSERT(from_hfp != NULL);
    USIPY_DASSERT(to_hfp != NULL);
    USIPY_DASSERT(callid_hfp != NULL);
    USIPY_DASSERT(cseq_hfp != NULL);
    USIPY_DASSERT(rr_hfp != NULL);
    USIPY_DASSERT(contact_hfp != NULL);
    USIPY_DASSERT(server_hfp != NULL);
    USIPY_DASSERT(ctype_hfp != NULL);
    USIPY_DASSERT(clen_hfp != NULL);
    APPEND_STR(&sip20_sp);
    usipy_sip_tm_uas_scode2str(slp->code, sbuf);
    APPEND_MEM(sbuf, sizeof(sbuf));
    APPEND_STR(&slp->reason_phrase);
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    for (size_t i = 0; i < tp->cache.uas.nvias; i++) {
        APPEND_STR(&via_hfp->name);
        APPEND_STR(&colon_sp);
        APPEND_STR(&tp->cache.uas.vias[i]);
        APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    }
    APPEND_STR(&from_hfp->name);
    APPEND_STR(&colon_sp);
    APPEND_STR(&tp->cache.uas.from);
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    APPEND_STR(&to_hfp->name);
    APPEND_STR(&colon_sp);
    APPEND_STR(&tp->cache.uas.to);
    if (tp->cache.to_tag.l != 0) {
        APPEND_STR(&tag_param);
        APPEND_STR(&tp->cache.to_tag);
    }
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    APPEND_STR(&callid_hfp->name);
    APPEND_STR(&colon_sp);
    APPEND_STR(&tp->cache.call_id);
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    APPEND_STR(&cseq_hfp->name);
    APPEND_STR(&colon_sp);
    rval = snprintf(buf + off, len - off, "%u %.*s", tp->cache.cseq.val,
      USIPY_SFMT(&usipy_method_db[tp->cache.method_type].name));
    if (rval < 0 || (size_t)rval >= len - off) {
        return (-1);
    }
    off += (size_t)rval;
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    for (size_t i = 0; i < tp->cache.uas.nrecord_routes; i++) {
        APPEND_STR(&rr_hfp->name);
        APPEND_STR(&colon_sp);
        APPEND_STR(&tp->cache.uas.record_routes[i]);
        APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    }
    if (tp->cache.method_type == USIPY_SIP_METHOD_INVITE &&
      slp->code >= 200 && slp->code < 300) {
        const struct usipy_str *local_contactp = &tp->cache.uas.local_contact_uri;

        if (local_contactp->l == 0) {
            local_contactp = &tm->luri;
        }
        APPEND_STR(&contact_hfp->name);
        APPEND_STR(&colon_sp);
        APPEND_MEM("<", 1);
        APPEND_STR(local_contactp);
        APPEND_MEM(">", 1);
        APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    }
    if (serverp->l != 0) {
        APPEND_STR(&server_hfp->name);
        APPEND_STR(&colon_sp);
        APPEND_STR(serverp);
        APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    }
    for (size_t i = 0; i < neh; i++) {
        const struct usipy_hdr_db_entr *eh_hfp = usipy_hdr_db_byid(ehp[i].hf_type);

        if (eh_hfp == NULL || eh_hfp->cantype != ehp[i].hf_type ||
          ehp[i].value_kind != USIPY_SIP_TM_EH_RAW) {
            return (-1);
        }
        if (eh_hfp->build != NULL) {
            ehdb[i] = *eh_hfp;
            ehdb[i].build = NULL;
            eh_hfp = &ehdb[i];
        }
        APPEND_STR(&eh_hfp->name);
        APPEND_STR(&colon_sp);
        APPEND_STR(&ehp[i].value);
        APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    }
    if (rpp->body.l != 0 && rpp->content_type.l != 0) {
        APPEND_STR(&ctype_hfp->name);
        APPEND_STR(&colon_sp);
        APPEND_STR(&rpp->content_type);
        APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    }
    APPEND_STR(&clen_hfp->name);
    APPEND_STR(&colon_sp);
    rval = snprintf(clen_buf, sizeof(clen_buf), "%zu", rpp->body.l);
    if (rval < 0 || (size_t)rval >= sizeof(clen_buf)) {
        return (-1);
    }
    APPEND_MEM(clen_buf, (size_t)rval);
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    if (rpp->body.l != 0) {
        APPEND_STR(&rpp->body);
    }
#undef APPEND_MEM
#undef APPEND_STR
    return ((int)off);
}

static int
usipy_sip_tm_uas_prepare_default_local_tag(struct usipy_sip_tm_txi *tp, size_t tx_index)
{
    USIPY_DASSERT(tp != NULL);

    return (usipy_msg_heap_sprintf(&tp->scratch, &tp->cache.to_tag, "t%zu-1", tx_index));
}

static int
usipy_sip_tm_uas_prepare_local_tag(const struct usipy_sip_tm *tm,
  struct usipy_sip_tm_txi *tp, size_t tx_index, uint32_t cseq, uint8_t method_type)
{
    struct usipy_sip_tm_id_policy_out ids = {0};
    struct usipy_sip_tm_id_policy_in in = {
      .transaction_index = tx_index,
      .cseq = cseq,
      .method_type = method_type,
    };

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tp != NULL);

    if (tm->id_policy.cb == NULL) {
        return (usipy_sip_tm_uas_prepare_default_local_tag(tp, tx_index));
    }
    if (tm->id_policy.cb(tm->id_policy.arg, &tp->scratch, &in, &ids) != 0 ||
      ids.local_tag.l == 0) {
        return (-1);
    }
    tp->cache.to_tag = ids.local_tag;
    return (0);
}

static int
usipy_sip_tm_find_uas_transaction(const struct usipy_sip_tm *tm,
  const struct usipy_sip_tid *tidp, size_t *indexp)
{
    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(indexp != NULL);

    for (size_t i = 0; i < tm->max_transactions; i++) {
        const struct usipy_sip_tm_txi *tp = &tm->transactions[i];

        if (!tp->active || tp->pub.role != USIPY_SIP_TM_ROLE_UAS) {
            continue;
        }
        if (usipy_sip_tm_tid_matches_tx(tidp, &tp->pub)) {
            *indexp = i;
            return (0);
        }
    }
    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    return (-1);
}

static int
usipy_sip_tm_uas_cancel_matches_tx(const struct usipy_sip_tid *tidp,
  const struct usipy_sip_tm_txi *tp)
{
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(tp != NULL);

    if (tp->pub.role != USIPY_SIP_TM_ROLE_UAS ||
      tp->pub.common.id.method_type != USIPY_SIP_METHOD_INVITE ||
      tidp->hash != tp->cache.uas.cancel_hash ||
      tp->pub.state == USIPY_SIP_TM_STATE_COMPLETED ||
      tp->pub.state == USIPY_SIP_TM_STATE_CONFIRMED ||
      tp->pub.state == USIPY_SIP_TM_STATE_TERMINATED) {
        return (0);
    }
    if (!usipy_str_eq(tidp->call_id, &tp->pub.common.id.call_id) ||
      !usipy_str_eq(tidp->from_tag, &tp->pub.common.id.from_tag) ||
      !usipy_str_eq(tidp->vbranch, &tp->pub.common.id.branch)) {
        return (0);
    }
    return (tidp->cseq->val == tp->pub.common.id.cseq);
}

static int
usipy_sip_tm_find_uas_cancel_transaction(const struct usipy_sip_tm *tm,
  const struct usipy_sip_tid *tidp, size_t *indexp)
{
    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(indexp != NULL);
    USIPY_DASSERT(tidp->cseq->method->cantype == USIPY_SIP_METHOD_CANCEL);

    for (size_t i = 0; i < tm->max_transactions; i++) {
        const struct usipy_sip_tm_txi *tp = &tm->transactions[i];

        if (!tp->active) {
            continue;
        }
        if (usipy_sip_tm_uas_cancel_matches_tx(tidp, tp)) {
            *indexp = i;
            return (0);
        }
    }
    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    return (-1);
}

int
usipy_sip_tm_new_uas_tr(struct usipy_sip_tm *tm,
  const struct usipy_sip_tm_new_uas_tr_params *tpp, size_t *indexp)
{
    struct usipy_sip_tid tid;
    struct usipy_sip_tm_txi *tp;
    struct usipy_sip_tm_timer_policy timers;
    uint8_t method_type;
    size_t tx_index;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(tpp != NULL);
    USIPY_DASSERT(indexp != NULL);
    USIPY_DASSERT(tpp->request != NULL);
    USIPY_DASSERT(tpp->request->kind == USIPY_SIP_MSG_REQ);

    *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    method_type = tpp->request->sline.parsed.rl.method->cantype;
    if (method_type == USIPY_SIP_METHOD_generic ||
      method_type == USIPY_SIP_METHOD_ACK) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (usipy_sip_msg_get_tid((struct usipy_msg *)tpp->request, &tid) != 0) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    if (usipy_sip_tm_find_uas_transaction(tm, &tid, &tx_index) == 0) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    tp = usipy_sip_tm_alloc_slot(tm, &tx_index);
    if (tp == NULL) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    tp->uas_callbacks = tpp->callbacks;
    tp->cache.method_type = method_type;
    if (usipy_msg_heap_append(&tp->scratch, &tp->cache.call_id, tid.call_id) != 0 ||
      usipy_sip_tm_uas_cache_request(tpp->request, &tid, tp) != 0) {
        usipy_sip_tm_tx_fini(tp);
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (usipy_sip_tm_uas_format_local_contact_uri(&tp->scratch, &tpp->local,
      &tp->cache.uas.local_contact_uri) != 0) {
        usipy_sip_tm_tx_fini(tp);
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    if (usipy_sip_tm_uas_prepare_local_tag(tm, tp, tx_index, tid.cseq->val,
      method_type) != 0) {
        usipy_sip_tm_tx_fini(tp);
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    tp->outbound.checkpoint = usipy_msg_heap_checkpoint(&tp->scratch);
    tp->active = 1;
    tm->nactive += 1;
    tp->pub.role = USIPY_SIP_TM_ROLE_UAS;
    tp->pub.state = USIPY_SIP_TM_STATE_TRYING;
    tp->pub.common.flags = (tm->transport == USIPY_SIP_TM_TRANSPORT_UDP) ? 0 :
      USIPY_SIP_TM_F_RELIABLE_TRANSPORT;
    tp->pub.common.peer = tpp->peer;
    tp->pub.common.local = tpp->local;
    tp->outbound.pub.target = tpp->peer;
    tp->outbound.pub.raw = USIPY_STR_NULL;
    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
    timers = tpp->timers.t1_ms != 0 ? tpp->timers :
      (struct usipy_sip_tm_timer_policy)USIPY_SIP_TM_TIMER_POLICY_RFC3261;
    tp->pub.common.timers = timers;
    tp->pub.common.id.hash = tid.hash;
    tp->pub.common.id.branch = tp->cache.branch;
    tp->pub.common.id.call_id = tp->cache.call_id;
    tp->pub.common.id.from_tag = tp->cache.from_tag;
    tp->pub.common.id.cseq = tid.cseq->val;
    tp->pub.common.id.method_type = tp->cache.method_type;
    tp->cache.uas.cancel_hash = 0;
    if (tp->cache.method_type == USIPY_SIP_METHOD_INVITE &&
      tp->uas_callbacks.cancel != NULL) {
        tp->cache.uas.cancel_hash = usipy_sip_tm_uas_method_hash(tp,
          USIPY_SIP_METHOD_CANCEL);
    }
    tp->pub.role_data.uas.last_status_code = 0;
    tp->pub.role_data.uas.request_retransmits = 0;
    *indexp = tx_index;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_send_uas_response(struct usipy_sip_tm *tm, size_t index,
  const struct usipy_sip_tm_uas_response_params *rpp)
{
    struct usipy_sip_tm_txi *tp;
    struct usipy_sip_tm_uas_build_arg barg;
    const struct usipy_sip_status *slp;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(rpp != NULL);
    USIPY_DASSERT(index < tm->max_transactions);
    slp = &rpp->status;
    tp = &tm->transactions[index];
    if (!tp->active) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    USIPY_DASSERT(tp->pub.role == USIPY_SIP_TM_ROLE_UAS);
    if (tp->pub.state == USIPY_SIP_TM_STATE_TERMINATED) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (tp->pub.role_data.uas.last_status_code >= 200) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    USIPY_DASSERT(tp->outbound.checkpoint != USIPY_MSG_HEAP_CHECKPOINT_NONE);
    usipy_msg_heap_rollback(&tp->scratch, tp->outbound.checkpoint);
    barg.tm = tm;
    barg.tp = tp;
    barg.rpp = rpp;
    if (usipy_msg_heap_build(&tp->scratch, &tp->outbound.pub.raw, &barg,
      usipy_sip_tm_uas_build_response_cb) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    tp->outbound.pub.target = tp->pub.common.peer;
    tp->outbound.pub.next_send_at_ms = 0;
    tp->pub.common.outbound = tp->outbound.pub;
    tp->pub.role_data.uas.last_status_code = slp->code;
    tp->pub.state = (slp->code < 200) ? USIPY_SIP_TM_STATE_PROCEEDING :
      USIPY_SIP_TM_STATE_COMPLETED;
    tp->cache.uas.ack_hash = 0;
    if (slp->code < 200) {
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
    } else if (tp->cache.method_type == USIPY_SIP_METHOD_INVITE && slp->code < 300) {
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
        tp->cache.uas.ack_hash = usipy_sip_dialog_tid_hash(&tp->pub.common.id.call_id,
          &tp->pub.common.id.from_tag, &tp->cache.to_tag, tp->pub.common.id.cseq,
          USIPY_SIP_METHOD_ACK);
        tp->uas_callbacks.no_ack = NULL;
    } else if (tp->cache.method_type == USIPY_SIP_METHOD_INVITE && slp->code >= 300) {
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_H;
        tp->pub.common.timer.value_ms = usipy_sip_tm_timer_h_ms(&tp->pub.common.timers);
        tp->pub.common.timer.due_at_ms = USIPY_SIP_TM_TIME_NONE;
        tp->cache.uas.ack_hash = usipy_sip_tm_uas_method_hash(tp,
          USIPY_SIP_METHOD_ACK);
        if (rpp->callbacks.arg != NULL) {
            tp->uas_callbacks.arg = rpp->callbacks.arg;
        }
        tp->uas_callbacks.no_ack = rpp->callbacks.no_ack;
    } else {
        tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
        tp->uas_callbacks.no_ack = NULL;
    }
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_uas_tr_cancelled(struct usipy_sip_tm *tm,
  const struct usipy_msg *cancelp, size_t index,
  const struct usipy_sip_tm_uas_response_params *rpp)
{
    const struct usipy_sip_tm_uas_response_params cancel_ok = {
      .status = usipy_sip_res_ok,
    };
    const struct usipy_sip_tm_uas_response_params def_resp = {
      .status = usipy_sip_res_req_term,
    };
    struct usipy_sip_tm_new_uas_tr_params tpp;
    const struct usipy_sip_tm_tx *txp;
    size_t cancel_index;
    int rval;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(cancelp != NULL);
    USIPY_DASSERT(index < tm->max_transactions);
    USIPY_DASSERT(cancelp->kind == USIPY_SIP_MSG_REQ);
    USIPY_DASSERT(cancelp->sline.parsed.rl.method->cantype == USIPY_SIP_METHOD_CANCEL);

    txp = usipy_sip_tm_get_transaction(tm, index);
    if (txp == NULL) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    USIPY_DASSERT(txp->role == USIPY_SIP_TM_ROLE_UAS);
    if (txp->common.id.method_type != USIPY_SIP_METHOD_INVITE) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    tpp = (struct usipy_sip_tm_new_uas_tr_params){
      .request = cancelp,
      .timers = txp->common.timers,
      .peer = txp->common.peer,
      .local = txp->common.local,
    };
    rval = usipy_sip_tm_new_uas_tr(tm, &tpp, &cancel_index);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    rval = usipy_sip_tm_send_uas_response(tm, cancel_index, &cancel_ok);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    if (rpp == NULL) {
        rpp = &def_resp;
    }
    return (usipy_sip_tm_send_uas_response(tm, index, rpp));
}

int
usipy_sip_tm_uas_run(struct usipy_sip_tm_txi *tp, size_t index,
  const struct usipy_sip_tm *tm, const struct usipy_sip_tm_run_in *inp,
  struct usipy_sip_tm_run_out *outp)
{
    int send_rval;

    (void)tm;
    USIPY_DASSERT(tp != NULL);
    USIPY_DASSERT(inp != NULL);

    if (tp->pub.common.timer.type != USIPY_SIP_TM_TIMER_NONE &&
      tp->pub.common.timer.due_at_ms <= inp->now_ms) {
        if (tp->pub.common.timer.type == USIPY_SIP_TM_TIMER_H) {
            if (tp->uas_callbacks.no_ack != NULL) {
                tp->uas_callbacks.no_ack(tp->uas_callbacks.arg, index, &tp->pub);
            }
            if (outp != NULL) {
                outp->ntimeouts += 1;
            }
        }
        usipy_sip_tm_uas_mark_terminated(tp);
    }
    if (tp->outbound.pub.next_send_at_ms != USIPY_SIP_TM_TIME_NONE) {
        usipy_sip_tm_uas_run_out_consider(outp, tp->outbound.pub.next_send_at_ms);
    }
    if (tp->pub.common.timer.type != USIPY_SIP_TM_TIMER_NONE) {
        usipy_sip_tm_uas_run_out_consider(outp, tp->pub.common.timer.due_at_ms);
    }
    if (tp->pub.state == USIPY_SIP_TM_STATE_TERMINATED ||
      tp->outbound.pub.next_send_at_ms == USIPY_SIP_TM_TIME_NONE ||
      tp->outbound.pub.next_send_at_ms > inp->now_ms) {
        return (USIPY_SIP_TM_OK);
    }
    send_rval = inp->send_to(inp->send_to_arg, index, &tp->pub, &tp->outbound.pub);
    if (send_rval != 0) {
        return (send_rval);
    }
    if (outp != NULL) {
        outp->nsent += 1;
    }
    if (tp->pub.common.created_at_ms == 0 && tp->pub.common.retransmit_count == 0) {
        tp->pub.common.created_at_ms = inp->now_ms;
    }
    tp->pub.common.updated_at_ms = inp->now_ms;
    tp->pub.common.retransmit_count += 1;
    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
    if (tp->pub.role_data.uas.last_status_code >= 200 &&
      usipy_sip_tm_uas_is_invite_non2xx_final(tp)) {
        usipy_sip_tm_uas_post_send_invite_final(tp, inp, outp);
    } else if (tp->pub.role_data.uas.last_status_code >= 200) {
        usipy_sip_tm_uas_post_send_final(tp, inp->now_ms, outp);
    }
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_handle_incoming_request(const struct usipy_sip_tm_handle_incoming_in *inp,
  struct usipy_msg *msg, const struct usipy_sip_tid *tidp,
  struct usipy_sip_tm_handle_incoming_out *outp)
{
    struct usipy_sip_tm *tm;
    size_t tx_index;

    USIPY_DASSERT(inp != NULL);
    USIPY_DASSERT(inp->tm != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(tidp != NULL);

    tm = inp->tm;
    if (msg->sline.parsed.rl.method->cantype == USIPY_SIP_METHOD_ACK &&
      usipy_sip_tm_find_uas_ack_transaction(tm, msg, tidp, &tx_index) == 0) {
        struct usipy_sip_tm_txi *tp = &tm->transactions[tx_index];

        if (tp->pub.role_data.uas.last_status_code >= 200 &&
          tp->pub.role_data.uas.last_status_code < 300) {
            if (outp != NULL) {
                outp->error = USIPY_SIP_TM_OK;
                outp->consumed = 1;
                outp->match_kind = USIPY_SIP_TM_MATCH_EXISTING;
                outp->event = USIPY_SIP_TM_EVENT_ACK_RX;
                outp->transaction_index = tx_index;
                outp->message = NULL;
            }
            return (USIPY_SIP_TM_OK);
        }
        return (usipy_sip_tm_uas_handle_ack(inp, outp, tp, tx_index));
    }
    if (msg->sline.parsed.rl.method->cantype == USIPY_SIP_METHOD_CANCEL &&
      usipy_sip_tm_find_uas_cancel_transaction(tm, tidp, &tx_index) == 0) {
        struct usipy_sip_tm_txi *tp = &tm->transactions[tx_index];

        return (usipy_sip_tm_uas_handle_cancel(outp, tp, tx_index, msg));
    }
    if (usipy_sip_tm_find_uas_transaction(tm, tidp, &tx_index) == 0) {
        struct usipy_sip_tm_txi *tp = &tm->transactions[tx_index];

        tp->pub.role_data.uas.request_retransmits += 1;
        if (tp->pub.state != USIPY_SIP_TM_STATE_CONFIRMED &&
          tp->outbound.pub.raw.l != 0) {
            tp->outbound.pub.next_send_at_ms = inp->now_ms;
            tp->pub.common.outbound = tp->outbound.pub;
        }
        if (outp != NULL) {
            outp->error = USIPY_SIP_TM_OK;
            outp->consumed = 1;
            outp->match_kind = USIPY_SIP_TM_MATCH_EXISTING;
            outp->event = USIPY_SIP_TM_EVENT_REQUEST_RETRANSMIT;
            outp->transaction_index = tx_index;
            outp->message = NULL;
        }
        return (USIPY_SIP_TM_OK);
    }
    if (tm->callbacks.incoming_request == NULL) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    tm->callbacks.incoming_request(tm->callbacks.arg, inp, msg);
    if (outp != NULL) {
        outp->error = USIPY_SIP_TM_OK;
        outp->consumed = 1;
        outp->match_kind = USIPY_SIP_TM_MATCH_NONE;
        outp->event = USIPY_SIP_TM_EVENT_REQUEST_RX;
        outp->transaction_index = USIPY_SIP_TM_TX_INDEX_NONE;
        outp->message = NULL;
    }
    if (usipy_sip_tm_find_uas_transaction(tm, tidp, &tx_index) == 0 && outp != NULL) {
        outp->match_kind = USIPY_SIP_TM_MATCH_NEW;
        outp->transaction_index = tx_index;
    }
    return (USIPY_SIP_TM_OK);
}
