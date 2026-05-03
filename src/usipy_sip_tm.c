#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "usipy_port/network.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_str.h"
#include "public/usipy_sip_method_types.h"
#include "public/usipy_sip_tm.h"
#include "public/usipy_sip_hdr_types.h"
#include "public/usipy_sip_sline.h"
#include "public/usipy_sip_msg.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_sip_hdr_authz.h"
#include "usipy_sip_method_db.h"
#include "usipy_sip_tid.h"
#include "usipy_tvpair.h"
#include "usipy_sip_tm_internal.h"
#include "usipy_sip_tm_priv.h"

#define USIPY_SIP_TM_HEAP_SIZE 512u
#define USIPY_SIP_TM_TX_SCRATCH_SIZE 4096u
#define USIPY_SIP_TM_TX_NCHECKPOINTS 2u

static void usipy_sip_tm_tx_reset(struct usipy_sip_tm_txi *);

static size_t
usipy_sip_tm_ctor_size(size_t max_transactions)
{
    size_t txs_size, scratch_size, total_size;

    USIPY_DASSERT(max_transactions > 0);
    USIPY_DASSERT(max_transactions <= SIZE_MAX / sizeof(struct usipy_sip_tm_txi));
    txs_size = sizeof(struct usipy_sip_tm_txi) * max_transactions;
    USIPY_DASSERT(max_transactions <= SIZE_MAX / USIPY_SIP_TM_TX_SCRATCH_SIZE);
    scratch_size = USIPY_SIP_TM_TX_SCRATCH_SIZE * max_transactions;
    USIPY_DASSERT(sizeof(struct usipy_sip_tm) <= SIZE_MAX - txs_size);
    total_size = sizeof(struct usipy_sip_tm) + txs_size;
    USIPY_DASSERT(total_size <= SIZE_MAX - USIPY_SIP_TM_HEAP_SIZE);
    total_size += USIPY_SIP_TM_HEAP_SIZE;
    USIPY_DASSERT(total_size <= SIZE_MAX - scratch_size);
    total_size += scratch_size;
    return (total_size);
}

static struct usipy_str
usipy_sip_tm_transport_name(enum usipy_sip_tm_transport transport)
{
    switch (transport) {
    case USIPY_SIP_TM_TRANSPORT_UDP:
        return ((struct usipy_str)USIPY_2STR("UDP"));

    case USIPY_SIP_TM_TRANSPORT_TCP:
        return ((struct usipy_str)USIPY_2STR("TCP"));

    case USIPY_SIP_TM_TRANSPORT_TLS:
        return ((struct usipy_str)USIPY_2STR("TLS"));

    case USIPY_SIP_TM_TRANSPORT_SCTP:
        return ((struct usipy_str)USIPY_2STR("SCTP"));

    default:
        USIPY_DABORT();
        return ((struct usipy_str)USIPY_2STR(""));
    }
}

struct usipy_sip_tm_txi *
usipy_sip_tm_alloc_slot(struct usipy_sip_tm *tm, size_t *indexp)
{
    for (size_t i = 0; i < tm->max_transactions; i++) {
        if (tm->transactions[i].active) {
            continue;
        }
        usipy_sip_tm_tx_reset(&tm->transactions[i]);
        if (indexp != NULL) {
            *indexp = i;
        }
        return (&tm->transactions[i]);
    }
    if (indexp != NULL) {
        *indexp = USIPY_SIP_TM_TX_INDEX_NONE;
    }
    return (NULL);
}

static int
usipy_sip_tm_init_default_via(struct usipy_sip_tm *tm)
{
    tm->default_via.via.sent_protocol.name = (struct usipy_str)USIPY_2STR("SIP");
    tm->default_via.via.sent_protocol.version = (struct usipy_str)USIPY_2STR("2.0");
    tm->default_via.via.sent_protocol.transport = usipy_sip_tm_transport_name(tm->transport);
    tm->default_via.via.sent_by.host = tm->laddr.host;
    tm->default_via.via.sent_by.port = tm->laddr.port;
    tm->default_via.via.nparams = 2;
    tm->default_via.params[0].token = (struct usipy_str)USIPY_2STR("branch");
    tm->default_via.params[0].value = USIPY_STR_NULL;
    tm->default_via.params[1].token = (struct usipy_str)USIPY_2STR("rport");
    tm->default_via.params[1].value = USIPY_STR_NULL;
    return (0);
}

static int
usipy_sip_tm_init_default_nameaddrs(struct usipy_sip_tm *tm)
{
    tm->default_from.nameaddr.addr_spec = tm->luri;
    tm->default_from.nameaddr.nparams = 1;
    tm->default_from.params[0].token = (struct usipy_str)USIPY_2STR("tag");
    tm->default_from.params[0].value = USIPY_STR_NULL;

    tm->default_to.nameaddr.addr_spec = tm->luri;
    tm->default_to.nameaddr.nparams = 0;
    tm->default_to.params[0].token = (struct usipy_str)USIPY_2STR("tag");
    tm->default_to.params[0].value = USIPY_STR_NULL;

    tm->default_contact.nameaddr.addr_spec = tm->luri;
    tm->default_contact.nameaddr.nparams = 0;
    tm->default_contact.params[0].token = (struct usipy_str)USIPY_2STR("expires");
    tm->default_contact.params[0].value = USIPY_STR_NULL;
    return (0);
}

static int
usipy_sip_tm_init_luri(struct usipy_sip_tm *tm)
{
    switch (tm->laddr.af) {
    case AF_INET:
        return (usipy_msg_heap_sprintf(&tm->heap, &tm->luri, "sip:%.*s:%u",
          USIPY_SFMT(&tm->laddr.host), tm->laddr.port));

#ifdef IPPROTO_IPV6
    case AF_INET6:
        return (usipy_msg_heap_sprintf(&tm->heap, &tm->luri, "sip:[%.*s]:%u",
          USIPY_SFMT(&tm->laddr.host), tm->laddr.port));
#endif

    default:
        errno = EAFNOSUPPORT;
        return (-1);
    }
}

static int
usipy_sip_tm_init_laddr(struct usipy_sip_tm *tm)
{
    char abuf[INET6_ADDRSTRLEN];
    const char *aname;
    socklen_t alen;
    union {
        struct sockaddr sa;
        struct sockaddr_in sin;
#ifdef IPPROTO_IPV6
        struct sockaddr_in6 sin6;
#endif
    } name;

    memset(&name, '\0', sizeof(name));
    alen = sizeof(name);
    if (getsockname(tm->sock, &name.sa, &alen) != 0) {
        return (-1);
    }
    tm->laddr.transport = tm->transport;
    switch (name.sa.sa_family) {
    case AF_INET:
        aname = inet_ntop(AF_INET, &name.sin.sin_addr, abuf, sizeof(abuf));
        if (aname == NULL) {
            return (-1);
        }
        tm->laddr.af = AF_INET;
        tm->laddr.port = ntohs(name.sin.sin_port);
        if (usipy_msg_heap_sprintf(&tm->heap, &tm->laddr.host, "%s", aname) != 0) {
            return (-1);
        }
        break;

#ifdef IPPROTO_IPV6
    case AF_INET6:
        aname = inet_ntop(AF_INET6, &name.sin6.sin6_addr, abuf, sizeof(abuf));
        if (aname == NULL) {
            return (-1);
        }
        tm->laddr.af = AF_INET6;
        tm->laddr.port = ntohs(name.sin6.sin6_port);
        if (usipy_msg_heap_sprintf(&tm->heap, &tm->laddr.host, "%s", aname) != 0) {
            return (-1);
        }
        break;
#endif

    default:
        errno = EAFNOSUPPORT;
        return (-1);
    }
    if (usipy_sip_tm_init_luri(tm) != 0) {
        return (-1);
    }
    if (usipy_sip_tm_init_default_via(tm) != 0) {
        return (-1);
    }
    return (usipy_sip_tm_init_default_nameaddrs(tm));
}

static void
usipy_sip_tm_tx_reset(struct usipy_sip_tm_txi *tp)
{
    memset(&tp->pub, '\0', sizeof(tp->pub));
    memset(&tp->cache, '\0', sizeof(tp->cache));
    tp->pub.common.timer.type = USIPY_SIP_TM_TIMER_NONE;
    tp->pub.common.timer.value_ms = 0;
    tp->pub.common.timer.due_at_ms = 0;
    memset(&tp->callbacks, '\0', sizeof(tp->callbacks));
    memset(&tp->uas_callbacks, '\0', sizeof(tp->uas_callbacks));
    memset(&tp->outbound, '\0', sizeof(tp->outbound));
    tp->outbound.checkpoint = USIPY_MSG_HEAP_CHECKPOINT_NONE;
    tp->outbound.pub.raw = USIPY_STR_NULL;
    tp->outbound.pub.next_send_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->parent_index = USIPY_SIP_TM_TX_INDEX_NONE;
    tp->child_index = USIPY_SIP_TM_TX_INDEX_NONE;
    tp->invite_timeout_at_ms = USIPY_SIP_TM_TIME_NONE;
    tp->final_reported = 0;
    tp->invite_provisional_seen = 0;
    tp->invite_cancel_state = USIPY_SIP_TM_INVITE_CANCEL_NONE;
    tp->invite_timeout_id = USIPY_SIP_TM_TIMEOUT_NONE;
    tp->pub.common.outbound = tp->outbound.pub;
    usipy_msg_heap_init(&tp->scratch, tp->scratch_buf, tp->scratch_capacity,
      tp->scratch_checkpoints, USIPY_SIP_TM_TX_NCHECKPOINTS);
    tp->active = 0;
}

void
usipy_sip_tm_tx_fini(struct usipy_sip_tm_txi *tp)
{
    usipy_sip_tm_tx_reset(tp);
}

struct usipy_sip_tm *
usipy_sip_tm_ctor(const struct usipy_sip_tm_ctor_params *ipp)
{
    static const struct usipy_sip_tm_callbacks empty_callbacks;
    static const struct usipy_sip_tm_id_policy empty_id_policy;
    const struct usipy_sip_tm_callbacks *callbacksp;
    const struct usipy_sip_tm_id_policy *id_policyp;
    struct usipy_sip_tm *rp;
    size_t total_size;

    if (ipp == NULL) {
        return (NULL);
    }
    USIPY_DASSERT(ipp != NULL);
    total_size = usipy_sip_tm_ctor_size(ipp->max_transactions);
    USIPY_DASSERT(ipp->max_transactions > 0);
    USIPY_DASSERT(ipp->transport != USIPY_SIP_TM_TRANSPORT_UNSPEC);
    callbacksp = ipp->callbacks != NULL ? ipp->callbacks : &empty_callbacks;
    id_policyp = ipp->id_policy != NULL ? ipp->id_policy : &empty_id_policy;
    rp = malloc(total_size);
    if (rp == NULL) {
        return (NULL);
    }
    memset(rp, '\0', total_size);
    rp->sock = ipp->sock;
    rp->transport = ipp->transport;
    rp->max_transactions = ipp->max_transactions;
    rp->transactions = (struct usipy_sip_tm_txi *)(rp + 1);
    rp->heap_buf = rp->transactions + ipp->max_transactions;
    rp->callbacks = *callbacksp;
    rp->id_policy = *id_policyp;
    usipy_msg_heap_init(&rp->heap, rp->heap_buf, USIPY_SIP_TM_HEAP_SIZE, NULL, 0);
    if (usipy_sip_tm_init_laddr(rp) != 0) {
        free(rp);
        return (NULL);
    }
    unsigned char *scratch = (unsigned char *)rp->heap_buf + USIPY_SIP_TM_HEAP_SIZE;
    for (size_t i = 0; i < ipp->max_transactions; i++) {
        struct usipy_sip_tm_txi *tp = &rp->transactions[i];

        tp->scratch_buf = scratch + (i * USIPY_SIP_TM_TX_SCRATCH_SIZE);
        tp->scratch_capacity = USIPY_SIP_TM_TX_SCRATCH_SIZE;
        usipy_sip_tm_tx_reset(tp);
    }
    return (rp);
}

void
usipy_sip_tm_dtor(struct usipy_sip_tm *tm)
{
    USIPY_DASSERT(tm != NULL);
    if (tm->transactions != NULL) {
        for (size_t i = 0; i < tm->max_transactions; i++) {
            usipy_sip_tm_tx_fini(&tm->transactions[i]);
        }
    }
    free(tm);
}

size_t
usipy_sip_tm_nactive(const struct usipy_sip_tm *tm)
{
    if (tm == NULL) {
        return (0);
    }
    return (tm->nactive);
}

const struct usipy_sip_tm_tx *
usipy_sip_tm_get_transaction(const struct usipy_sip_tm *tm, size_t index)
{
    if (tm == NULL || index >= tm->max_transactions) {
        return (NULL);
    }
    if (!tm->transactions[index].active) {
        return (NULL);
    }
    return (&tm->transactions[index].pub);
}

int
usipy_sip_tm_drop_transaction(struct usipy_sip_tm *tm, size_t index)
{
    struct usipy_sip_tm_txi *tp;

    if (tm == NULL || index >= tm->max_transactions) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (!tm->transactions[index].active) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    tp = &tm->transactions[index];
    for (size_t i = 0; i < tm->max_transactions; i++) {
        struct usipy_sip_tm_txi *childp = &tm->transactions[i];

        if (!childp->active || childp->parent_index != index) {
            continue;
        }
        usipy_sip_tm_tx_fini(childp);
        if (tm->nactive > 0) {
            tm->nactive -= 1;
        }
    }
    if (tp->parent_index != USIPY_SIP_TM_TX_INDEX_NONE &&
      tp->parent_index < tm->max_transactions &&
      tm->transactions[tp->parent_index].active) {
        struct usipy_sip_tm_txi *parentp = &tm->transactions[tp->parent_index];

        if (parentp->child_index == index) {
            parentp->child_index = USIPY_SIP_TM_TX_INDEX_NONE;
        }
    }
    usipy_sip_tm_tx_fini(tp);
    if (tm->nactive > 0) {
        tm->nactive -= 1;
    }
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_set_timer_policy(struct usipy_sip_tm *tm, size_t index,
  const struct usipy_sip_tm_timer_policy *policy)
{
    if (tm == NULL || policy == NULL || index >= tm->max_transactions) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (!tm->transactions[index].active) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    tm->transactions[index].pub.common.timers = *policy;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_run(struct usipy_sip_tm_run_in *inp, struct usipy_sip_tm_run_out *outp)
{
    struct usipy_sip_tm *tm;
    int rval;

    USIPY_DASSERT(inp != NULL);
    USIPY_DASSERT(inp->tm != NULL);
    USIPY_DASSERT(inp->send_to != NULL);
    tm = inp->tm;
    if (outp != NULL) {
        memset(outp, '\0', sizeof(*outp));
        outp->next_run_at_ms = USIPY_SIP_TM_TIME_NONE;
    }
    if (inp->send_to == NULL) {
        if (outp != NULL) {
            outp->error = USIPY_SIP_TM_ERR_INVAL;
        }
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    for (size_t i = 0; i < tm->max_transactions; i++) {
        struct usipy_sip_tm_txi *tp = &tm->transactions[i];

        if (!tp->active) {
            continue;
        }
        switch (tp->pub.role) {
        case USIPY_SIP_TM_ROLE_UAC:
            rval = usipy_sip_tm_uac_run(tp, i, tm, inp, outp);
            if (rval != 0) {
                if (outp != NULL) {
                    outp->error = rval;
                }
                return (rval);
            }
            break;
        case USIPY_SIP_TM_ROLE_UAS:
            rval = usipy_sip_tm_uas_run(tp, i, tm, inp, outp);
            if (rval != 0) {
                if (outp != NULL) {
                    outp->error = rval;
                }
                return (rval);
            }
            break;
        default:
            USIPY_DABORT();
        }
    }
    if (outp != NULL) {
        outp->error = USIPY_SIP_TM_OK;
    }
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_handle_incoming(const struct usipy_sip_tm_handle_incoming_in *inp,
  struct usipy_sip_tm_handle_incoming_out *outp)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_sip_tid tid;
    struct usipy_msg *msg;
    int rval;

    USIPY_DASSERT(inp != NULL);
    USIPY_DASSERT(inp->tm != NULL);
    USIPY_DASSERT(inp->buf != NULL);
    if (outp != NULL) {
        memset(outp, '\0', sizeof(*outp));
        outp->error = USIPY_SIP_TM_ERR_UNSUPPORTED;
        outp->transaction_index = USIPY_SIP_TM_TX_INDEX_NONE;
    }
    msg = usipy_sip_msg_ctor_fromwire(inp->buf, inp->len, &perr);
    if (msg == NULL) {
        if (outp != NULL) {
            outp->error = USIPY_SIP_TM_ERR_PARSE;
        }
        return (USIPY_SIP_TM_ERR_PARSE);
    }
    if (usipy_sip_msg_get_tid(msg, &tid) != 0) {
        usipy_sip_msg_dtor(msg);
        if (outp != NULL) {
            outp->error = USIPY_SIP_TM_ERR_BADMSG;
        }
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    if (msg->kind == USIPY_SIP_MSG_RES) {
        rval = usipy_sip_tm_handle_incoming_response(inp, msg, &tid, outp);
    } else if (msg->kind == USIPY_SIP_MSG_REQ) {
        rval = usipy_sip_tm_handle_incoming_request(inp, msg, &tid, outp);
    } else {
        rval = USIPY_SIP_TM_ERR_UNSUPPORTED;
    }
    if (rval != USIPY_SIP_TM_OK) {
        usipy_sip_msg_dtor(msg);
        if (outp != NULL) {
            outp->error = rval;
        }
        return (rval);
    }
    usipy_sip_msg_dtor(msg);
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_tid_matches_tx(const struct usipy_sip_tid *tidp,
  const struct usipy_sip_tm_tx *txp)
{
    USIPY_DASSERT(tidp != NULL);
    USIPY_DASSERT(txp != NULL);
    USIPY_DASSERT(tidp->call_id != NULL);
    USIPY_DASSERT(tidp->from_tag != NULL);
    USIPY_DASSERT(tidp->vbranch != NULL);
    USIPY_DASSERT(tidp->cseq != NULL);

    if (tidp->hash != txp->common.id.hash) {
        return (0);
    }
    if (!usipy_str_eq(tidp->call_id, &txp->common.id.call_id) ||
      !usipy_str_eq(tidp->from_tag, &txp->common.id.from_tag) ||
      !usipy_str_eq(tidp->vbranch, &txp->common.id.branch)) {
        return (0);
    }
    if (tidp->cseq->val != txp->common.id.cseq ||
      tidp->cseq->method->cantype != txp->common.id.method_type) {
        return (0);
    }
    return (1);
}
