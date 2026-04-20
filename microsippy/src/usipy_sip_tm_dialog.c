#include <stddef.h>
#include <stdint.h>

#include "usipy_port/string_compat.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_method_types.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_tm_internal.h"
#include "usipy_sip_tm_priv.h"
#include "usipy_tvpair.h"

static int usipy_sip_tm_dialog_uri_has_param(const struct usipy_sip_uri *, const char *);
static void usipy_sip_tm_dialog_set_target_from_uri(struct usipy_sip_tm_addr *,
  const struct usipy_sip_tm_addr *, const struct usipy_sip_uri *);

static int
usipy_sip_tm_dialog_resolve_target(struct usipy_msg_heap *mhp,
  const struct usipy_sip_tm_addr *basep, const struct usipy_str *remote_targetp,
  const struct usipy_str *route_srcp, size_t nroutes, int reverse,
  struct usipy_str *request_urip, struct usipy_sip_tm_addr *targetp,
  struct usipy_str **route_setpp, size_t *nroutesp)
{
    struct usipy_str remote_target;
    struct usipy_str *routes;
    struct usipy_sip_uri *first_route_uri, *target_uri;
    const struct usipy_str *target_urip;

    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(basep != NULL);
    USIPY_DASSERT(remote_targetp != NULL);
    USIPY_DASSERT(nroutes == 0 || route_srcp != NULL);
    USIPY_DASSERT(request_urip != NULL);
    USIPY_DASSERT(targetp != NULL);
    USIPY_DASSERT(route_setpp != NULL);
    USIPY_DASSERT(nroutesp != NULL);

    if (usipy_msg_heap_append(mhp, &remote_target, remote_targetp) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    *request_urip = remote_target;
    *route_setpp = NULL;
    *nroutesp = 0;
    target_urip = request_urip;
    if (nroutes != 0) {
        routes = usipy_msg_heap_alloc(mhp, sizeof(*routes) * (nroutes + 1));
        if (routes == NULL) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
        for (size_t i = 0; i < nroutes; i++) {
            const size_t sidx = reverse != 0 ? nroutes - i - 1 : i;

            if (usipy_msg_heap_append(mhp, &routes[i], &route_srcp[sidx]) != 0) {
                return (USIPY_SIP_TM_ERR_NOSPC);
            }
        }
        first_route_uri = usipy_sip_uri_parse(mhp, &routes[0]);
        if (first_route_uri == NULL || first_route_uri->host.l == 0) {
            return (USIPY_SIP_TM_ERR_BADMSG);
        }
        if (usipy_sip_tm_dialog_uri_has_param(first_route_uri, "lr")) {
            *route_setpp = routes;
            *nroutesp = nroutes;
            target_urip = &routes[0];
        } else {
            const char *qp;

            routes[nroutes] = remote_target;
            *route_setpp = &routes[1];
            *nroutesp = nroutes;
            *request_urip = routes[0];
            qp = memchr(request_urip->s.ro, '?', request_urip->l);
            if (qp != NULL) {
                request_urip->l = (size_t)(qp - request_urip->s.ro);
            }
            target_urip = request_urip;
        }
    }
    target_uri = usipy_sip_uri_parse(mhp, target_urip);
    if (target_uri == NULL || target_uri->host.l == 0) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    usipy_sip_tm_dialog_set_target_from_uri(targetp, basep, target_uri);
    return (USIPY_SIP_TM_OK);
}

static int
usipy_sip_tm_dialog_uri_has_param(const struct usipy_sip_uri *up, const char *name)
{
    const size_t nlen = strlen(name);

    USIPY_DASSERT(up != NULL);
    USIPY_DASSERT(name != NULL);

    for (int i = 0; i < up->nparams; i++) {
        if (up->parameters[i].token.l == nlen &&
          memcmp(up->parameters[i].token.s.ro, name, nlen) == 0) {
            return (1);
        }
    }
    return (0);
}

static void
usipy_sip_tm_dialog_set_target_from_uri(struct usipy_sip_tm_addr *targetp,
  const struct usipy_sip_tm_addr *basep, const struct usipy_sip_uri *urip)
{
    USIPY_DASSERT(targetp != NULL);
    USIPY_DASSERT(basep != NULL);
    USIPY_DASSERT(urip != NULL);

    *targetp = *basep;
    targetp->host = urip->host;
    if (urip->port != 0) {
        targetp->port = urip->port;
        return;
    }
    targetp->port = basep->transport == USIPY_SIP_TM_TRANSPORT_TLS ? 5061u : 5060u;
}

int
usipy_sip_tm_init_uac_dialog_request_params(const struct usipy_sip_tm *tm,
  size_t anchor_index, const struct usipy_msg *msg, uint8_t method_type,
  struct usipy_msg_heap *mhp,
  struct usipy_sip_tm_new_in_dialog_transaction_params *outp)
{
    const struct usipy_sip_tm_txi *anchorp;
    const struct usipy_sip_hdr_nameaddr *top, *contactp;
    const struct usipy_str *tagp;
    struct usipy_sip_tm_addr next_target;
    struct usipy_str request_uri;
    struct usipy_str *owned_routes = NULL;
    struct usipy_str *routes = NULL;
    struct usipy_sip_hdr_match *matchp;
    size_t nrecordroutes = 0, neffective_routes = 0;
    int rval;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(outp != NULL);
    USIPY_DASSERT(anchor_index < tm->max_transactions);

    anchorp = &tm->transactions[anchor_index];
    if (!anchorp->active) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    USIPY_DASSERT(anchorp->pub.role == USIPY_SIP_TM_ROLE_UAC);
    if (anchorp->pub.common.id.method_type != USIPY_SIP_METHOD_INVITE ||
      anchorp->pub.role_data.uac.response_class != 2 ||
      msg->kind != USIPY_SIP_MSG_RES ||
      msg->sline.parsed.sl.status.code < 200 ||
      msg->sline.parsed.sl.status.code > 299) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(msg->nhdrs));
    *matchp = (struct usipy_sip_hdr_match){.hdrslen = msg->nhdrs};
    uint64_t parse_mask = USIPY_HFT_MASK(USIPY_HF_TO) |
      USIPY_HFT_MASK(USIPY_HF_CONTACT);

    if (USIPY_MSG_HDR_PRESENT(msg, USIPY_HF_RECORDROUTE)) {
        parse_mask |= USIPY_HFT_MASK(USIPY_HF_RECORDROUTE);
    }
    if (usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)msg, parse_mask, 0,
      matchp) != 0) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    top = NULL;
    contactp = NULL;
    for (size_t i = 0; i < matchp->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = matchp->hdrsp[i];

        if (shp->hf_type->cantype == USIPY_HF_TO) {
            if (top == NULL) {
                top = shp->parsed.to;
            }
        } else if (shp->hf_type->cantype == USIPY_HF_CONTACT) {
            if (contactp == NULL) {
                contactp = shp->parsed.contact;
            }
        } else if (shp->hf_type->cantype == USIPY_HF_RECORDROUTE) {
            matchp->hdrsp[nrecordroutes++] = shp;
        }
    }
    if (top == NULL || contactp == NULL) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    tagp = usipy_sip_hdr_nameaddr_get_param(top, "tag");
    if (tagp == NULL || tagp->l == 0 || contactp->addr_spec.l == 0) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    if (nrecordroutes != 0) {
        routes = __builtin_alloca(sizeof(*routes) * nrecordroutes);
        for (size_t i = 0; i < nrecordroutes; i++) {
            const struct usipy_sip_hdr *shp = matchp->hdrsp[i];

            routes[i] = shp->parsed.recordroute->addr_spec;
        }
    }
    memset(outp, '\0', sizeof(*outp));
    rval = usipy_sip_tm_dialog_resolve_target(mhp, &anchorp->pub.common.peer,
      &contactp->addr_spec, routes, nrecordroutes, 1, &request_uri, &next_target,
      &owned_routes, &neffective_routes);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    if (usipy_msg_heap_append(mhp, &outp->request_id.call_id,
      &anchorp->cache.call_id) != 0 ||
      usipy_msg_heap_append(mhp, &outp->parties_by_uri.contact,
        &anchorp->cache.uac.contact_uri) != 0 ||
      usipy_msg_heap_append(mhp, &outp->parties_by_uri.from,
        &anchorp->cache.uac.from_uri) != 0 ||
      usipy_msg_heap_append(mhp, &outp->parties_by_uri.to,
        &anchorp->cache.uac.to_uri) != 0 ||
      usipy_msg_heap_append(mhp, &outp->dialog_tags.local_tag,
        &anchorp->cache.from_tag) != 0 ||
      usipy_msg_heap_append(mhp, &outp->dialog_tags.remote_tag, tagp) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    outp->request_target.request_uri = request_uri;
    outp->route_set.routes = owned_routes;
    outp->route_set.nroutes = neffective_routes;
    outp->request_target.target = next_target;
    outp->request_id.cseq = anchorp->cache.cseq.val;
    outp->request_id.method_type = method_type;
    outp->timers = anchorp->pub.common.timers;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_init_uas_dialog_request_params(const struct usipy_sip_tm *tm,
  size_t anchor_index, uint8_t method_type, struct usipy_msg_heap *mhp,
  struct usipy_sip_tm_new_in_dialog_transaction_params *outp)
{
    const struct usipy_sip_tm_txi *anchorp;
    const struct usipy_sip_hdr_nameaddr *rrp;
    struct usipy_sip_tm_addr next_target;
    struct usipy_str request_uri;
    struct usipy_str first_route_raw;
    struct usipy_str *owned_routes = NULL;
    size_t neffective_routes = 0;
    const struct usipy_str *remote_targetp;
    const struct usipy_str *local_contactp;
    struct usipy_sip_uri *target_uri, *first_route_uri;

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(outp != NULL);
    USIPY_DASSERT(anchor_index < tm->max_transactions);

    anchorp = &tm->transactions[anchor_index];
    if (!anchorp->active) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    USIPY_DASSERT(anchorp->pub.role == USIPY_SIP_TM_ROLE_UAS);
    if (anchorp->pub.common.id.method_type != USIPY_SIP_METHOD_INVITE ||
      anchorp->pub.role_data.uas.last_status_code >= 200 ||
      anchorp->cache.to_tag.l == 0 ||
      anchorp->cache.from_tag.l == 0 ||
      anchorp->cache.uas.from_uri.l == 0 ||
      anchorp->cache.uas.to_uri.l == 0) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    remote_targetp = &anchorp->cache.uas.contact_uri;
    if (remote_targetp->l == 0) {
        remote_targetp = &anchorp->cache.uas.request_uri;
    }
    if (remote_targetp->l == 0) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    memset(outp, '\0', sizeof(*outp));
    if (anchorp->cache.uas.nrecord_routes == 0) {
        if (usipy_msg_heap_append(mhp, &request_uri, remote_targetp) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
        target_uri = usipy_sip_uri_parse(mhp, &request_uri);
        if (target_uri == NULL || target_uri->host.l == 0) {
            return (USIPY_SIP_TM_ERR_BADMSG);
        }
    } else {
        if (usipy_msg_heap_append(mhp, &first_route_raw,
          &anchorp->cache.uas.record_routes[0]) != 0) {
            return (USIPY_SIP_TM_ERR_NOSPC);
        }
        rrp = usipy_sip_hdr_nameaddr_parse(mhp, &first_route_raw).recordroute;
        if (rrp == NULL || rrp->addr_spec.l == 0) {
            return (USIPY_SIP_TM_ERR_BADMSG);
        }
        first_route_uri = usipy_sip_uri_parse(mhp, &rrp->addr_spec);
        if (first_route_uri == NULL || first_route_uri->host.l == 0) {
            return (USIPY_SIP_TM_ERR_BADMSG);
        }
        if (usipy_sip_tm_dialog_uri_has_param(first_route_uri, "lr")) {
            owned_routes = usipy_msg_heap_alloc(mhp,
              sizeof(*owned_routes) * anchorp->cache.uas.nrecord_routes);
            if (owned_routes == NULL ||
              usipy_msg_heap_append(mhp, &request_uri, remote_targetp) != 0) {
                return (USIPY_SIP_TM_ERR_NOSPC);
            }
            for (size_t i = 0; i < anchorp->cache.uas.nrecord_routes; i++) {
                if (usipy_msg_heap_append(mhp, &owned_routes[i],
                  &anchorp->cache.uas.record_routes[i]) != 0) {
                    return (USIPY_SIP_TM_ERR_NOSPC);
                }
            }
            neffective_routes = anchorp->cache.uas.nrecord_routes;
            target_uri = first_route_uri;
        } else {
            const char *qp;

            owned_routes = usipy_msg_heap_alloc(mhp,
              sizeof(*owned_routes) * anchorp->cache.uas.nrecord_routes);
            if (owned_routes == NULL ||
              usipy_msg_heap_append(mhp, &request_uri, &rrp->addr_spec) != 0) {
                return (USIPY_SIP_TM_ERR_NOSPC);
            }
            qp = memchr(request_uri.s.ro, '?', request_uri.l);
            if (qp != NULL) {
                request_uri.l = (size_t)(qp - request_uri.s.ro);
            }
            for (size_t i = 1; i < anchorp->cache.uas.nrecord_routes; i++) {
                if (usipy_msg_heap_append(mhp, &owned_routes[i - 1],
                  &anchorp->cache.uas.record_routes[i]) != 0) {
                    return (USIPY_SIP_TM_ERR_NOSPC);
                }
            }
            if (usipy_msg_heap_append(mhp,
              &owned_routes[anchorp->cache.uas.nrecord_routes - 1],
              remote_targetp) != 0) {
                return (USIPY_SIP_TM_ERR_NOSPC);
            }
            neffective_routes = anchorp->cache.uas.nrecord_routes;
            target_uri = usipy_sip_uri_parse(mhp, &request_uri);
            if (target_uri == NULL || target_uri->host.l == 0) {
                return (USIPY_SIP_TM_ERR_BADMSG);
            }
        }
    }
    usipy_sip_tm_dialog_set_target_from_uri(&next_target,
      &anchorp->pub.common.peer, target_uri);
    local_contactp = &anchorp->cache.uas.local_contact_uri;
    if (local_contactp->l == 0) {
        local_contactp = &tm->luri;
    }
    if (
      usipy_msg_heap_append(mhp, &outp->request_id.call_id,
        &anchorp->cache.call_id) != 0 ||
      usipy_msg_heap_append(mhp, &outp->parties_by_uri.contact, local_contactp) != 0 ||
      usipy_msg_heap_append(mhp, &outp->parties_by_uri.from,
        &anchorp->cache.uas.to_uri) != 0 ||
      usipy_msg_heap_append(mhp, &outp->parties_by_uri.to,
        &anchorp->cache.uas.from_uri) != 0 ||
      usipy_msg_heap_append(mhp, &outp->dialog_tags.local_tag,
        &anchorp->cache.to_tag) != 0 ||
      usipy_msg_heap_append(mhp, &outp->dialog_tags.remote_tag,
        &anchorp->cache.from_tag) != 0) {
        return (USIPY_SIP_TM_ERR_NOSPC);
    }
    outp->request_target.request_uri = request_uri;
    outp->request_target.target = next_target;
    outp->route_set.routes = owned_routes;
    outp->route_set.nroutes = neffective_routes;
    outp->request_id.cseq = anchorp->cache.cseq.val;
    outp->request_id.method_type = method_type;
    outp->timers = anchorp->pub.common.timers;
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_tm_apply_uac_2xx_ack_dialog(const struct usipy_sip_tm *tm,
  size_t anchor_index, const struct usipy_msg *msg, struct usipy_sip_tm_txi *ackp)
{
    struct usipy_sip_tm_new_in_dialog_transaction_params tpp = {0};

    USIPY_DASSERT(tm != NULL);
    USIPY_DASSERT(msg != NULL);
    USIPY_DASSERT(ackp != NULL);

    const int rval = usipy_sip_tm_init_uac_dialog_request_params(tm, anchor_index,
      msg, USIPY_SIP_METHOD_ACK, &ackp->scratch, &tpp);

    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    ackp->cache.uac.request_uri = usipy_sip_uri_parse(&ackp->scratch,
      &tpp.request_target.request_uri);
    if (ackp->cache.uac.request_uri == NULL) {
        return (USIPY_SIP_TM_ERR_BADMSG);
    }
    ackp->cache.uac.from_uri = tpp.parties_by_uri.from;
    ackp->cache.uac.to_uri = tpp.parties_by_uri.to;
    ackp->cache.uac.contact_uri = tpp.parties_by_uri.contact;
    ackp->cache.to_tag = tpp.dialog_tags.remote_tag;
    ackp->cache.uac.routes = (struct usipy_str *)tpp.route_set.routes;
    ackp->cache.uac.nroutes = tpp.route_set.nroutes;
    ackp->pub.common.peer = tpp.request_target.target;
    ackp->outbound.pub.target = tpp.request_target.target;
    ackp->pub.common.outbound = ackp->outbound.pub;
    return (USIPY_SIP_TM_OK);
}
