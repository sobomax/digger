#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "public/usipy_str.h"
#include "public/usipy_sip_hdr_types.h"
#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_response_utils.h"
#include "usipy_debug.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_res.h"
#include "usipy_sip_tid.h"

int
usipy_sip_tm_send_simple_response(struct usipy_sip_tm *tm,
  const struct usipy_sip_tm_handle_incoming_in *hin, const struct usipy_msg *msg,
  const struct usipy_sip_status *statusp)
{
    struct usipy_sip_tm_new_uas_tr_params tp = {
      .request = msg,
      .timers = hin->timers,
      .peer = hin->peer,
      .local = hin->local,
    };
    struct usipy_sip_tm_uas_response_params rp = {
      .status = statusp,
    };
    size_t tx_index;
    int rval;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(hin != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(statusp != NULL);
    USIPY_DASSERT(hin->timers != NULL);
    USIPY_DASSERT(hin->peer != NULL);
    USIPY_DASSERT(hin->local != NULL);
    rval = usipy_sip_tm_new_uas_tr(tm, &tp, &tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    return (usipy_sip_tm_send_uas_response(tm, tx_index, &rp));
}

/*
 * The To tag for a response to msg that has none yet (RFC 3261, 8.2.6.2), as
 * one sent with no transaction to keep it in: from the request's transaction
 * id, for each of its retransmissions to get the same one. Whether it takes
 * one (none for a request without the headers to tell, either).
 */
static int
usipy_sip_stateless_to_tag(const struct usipy_msg *msg, char *buf, size_t len,
  struct usipy_str *tagp)
{
    /* Only to parse its headers, which the message caches */
    struct usipy_msg *mmsg = (struct usipy_msg *)msg;
    struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *top;
    const struct usipy_str *to_tagp;
    struct usipy_sip_tid tid;
    int n;

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
    *matchp = (struct usipy_sip_hdr_match){.hdrslen = 1};
    if (usipy_sip_msg_parse_hdrs_get(mmsg, USIPY_HFT_MASK(USIPY_HF_TO), 1,
      matchp) != 0 || matchp->nhdrs == 0 ||
      (top = matchp->hdrsp[0]->parsed.to) == NULL) {
        return (0);
    }
    to_tagp = usipy_sip_hdr_nameaddr_get_param(top, "tag");
    if (to_tagp != NULL && to_tagp->l != 0) {
        return (0);
    }
    if (usipy_sip_msg_get_tid(mmsg, &tid) != 0) {
        return (0);
    }
    n = snprintf(buf, len, "s%08x", (unsigned int)tid.hash);
    if (n < 0 || (size_t)n >= len) {
        return (0);
    }
    *tagp = (struct usipy_str){.s.ro = buf, .l = (size_t)n};
    return (1);
}

int
usipy_sip_send_stateless_response(const struct usipy_msg *msg,
  const struct usipy_sip_status *statusp, usipy_sip_raw_send_cb send_cb,
  void *send_arg)
{
    struct usipy_msg *resp;
    struct usipy_str tag;
    char tagbuf[sizeof("s") + 8];
    int rval;

    if (msg == NULL || statusp == NULL || send_cb == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    resp = usipy_sip_res_ctor_fromreq_tagged(msg, statusp,
      usipy_sip_stateless_to_tag(msg, tagbuf, sizeof(tagbuf), &tag) ? &tag :
      NULL);
    if (resp == NULL) {
        return (USIPY_SIP_TM_ERR_NOMEM);
    }
    rval = send_cb(send_arg, resp->onwire.s.ro, resp->onwire.l);
    usipy_sip_msg_dtor(resp);
    return (rval == 0 ? USIPY_SIP_TM_OK : USIPY_SIP_TM_ERR_INVAL);
}
