/* Digger Reloaded
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim_sip_internal.h"

#include <stdio.h>
#include <string.h>

#include "microsippy/src/usipy_sip_hdr.h"
#include "microsippy/src/usipy_sip_hdr_nameaddr.h"
#include "microsippy/src/usipy_sip_uri.h"
#include "microsippy/src/usipy_sip_res.h"
#include "microsippy/src/public/usipy_sip_response_utils.h"

#if NETSIM_PLATFORM_SUPPORTED

void
peer_binding_clear(struct netsim_sip_peer_binding *bp)
{

  memset(bp, '\0', sizeof(*bp));
}

static bool
peer_binding_set_peer_user(struct netsim_sip_peer_binding *bp,
  const struct usipy_str *peer_user)
{

  if (peer_user->l == 0 || peer_user->l >= sizeof(bp->peer_user_buf))
    return (false);
  memcpy(bp->peer_user_buf, peer_user->s.ro, peer_user->l);
  bp->peer_user_buf[peer_user->l] = '\0';
  bp->peer_user = (struct usipy_str){
    .s.ro = bp->peer_user_buf,
    .l = peer_user->l,
  };
  return (true);
}

bool
peer_bindings_valid(const struct netsim_sip *sp)
{
  size_t i;

  for (i = 0; i < NETSIM_SIP_MAX_REGISTERED; i++) {
    if (sp->peer_bindings[i].valid)
      return (true);
  }
  return (false);
}

struct netsim_sip_peer_binding *
peer_binding_find(struct netsim_sip *sp, const struct usipy_str *peer_user)
{
  size_t i;

  if (peer_user->l == 0)
    return (NULL);
  for (i = 0; i < NETSIM_SIP_MAX_REGISTERED; i++) {
    if (!sp->peer_bindings[i].valid)
      continue;
    if (usipy_str_eq(&sp->peer_bindings[i].peer_user, peer_user))
      return (&sp->peer_bindings[i]);
  }
  return (NULL);
}

const struct netsim_sip_peer_binding *
peer_binding_find_const(const struct netsim_sip *sp, const struct usipy_str *peer_user)
{
  size_t i;

  if (peer_user->l == 0)
    return (NULL);
  for (i = 0; i < NETSIM_SIP_MAX_REGISTERED; i++) {
    if (!sp->peer_bindings[i].valid)
      continue;
    if (usipy_str_eq(&sp->peer_bindings[i].peer_user, peer_user))
      return (&sp->peer_bindings[i]);
  }
  return (NULL);
}

struct netsim_sip_peer_binding *
peer_binding_store_slot(struct netsim_sip *sp, const struct usipy_str *peer_user)
{
  size_t i;
  struct netsim_sip_peer_binding *free_slot;

  if (peer_user->l == 0)
    return (NULL);
  free_slot = NULL;
  for (i = 0; i < NETSIM_SIP_MAX_REGISTERED; i++) {
    if (sp->peer_bindings[i].valid) {
      if (usipy_str_eq(&sp->peer_bindings[i].peer_user, peer_user))
        return (&sp->peer_bindings[i]);
      continue;
    }
    if (free_slot == NULL)
      free_slot = &sp->peer_bindings[i];
  }
  return (free_slot);
}

int
extract_register_binding(const struct usipy_msg *msg,
  const struct usipy_str *peer_user, struct netsim_sip_register_binding *outp)
{
  struct usipy_msg *cmsg;
  struct usipy_sip_hdr_match *from_hdrs;
  const struct usipy_sip_hdr_nameaddr *contactp;
  const struct usipy_str *actual_peer_user;
  struct usipy_sip_uri *urip;
  unsigned int expires;

  if (msg == NULL || outp == NULL)
    return (USIPY_SIP_TM_ERR_INVAL);
  memset(outp, '\0', sizeof(*outp));
  cmsg = (struct usipy_msg *)msg;
  from_hdrs = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
  *from_hdrs = (struct usipy_sip_hdr_match){.hdrslen = 1};
  if (usipy_sip_msg_parse_hdrs_get(cmsg, USIPY_HFT_MASK(USIPY_HF_FROM), 1,
        from_hdrs) != 0)
    return (USIPY_SIP_TM_ERR_PARSE);
  if (from_hdrs->nhdrs == 0)
    return (USIPY_SIP_TM_ERR_BADMSG);
  {
    const struct usipy_sip_hdr_nameaddr *fromp;
    struct usipy_sip_uri *from_uri;

    fromp = from_hdrs->hdrsp[0]->parsed.from;
    if (fromp == NULL)
      return (USIPY_SIP_TM_ERR_BADMSG);
    from_uri = usipy_sip_uri_parse(&cmsg->heap, &fromp->addr_spec);
    if (from_uri == NULL || from_uri->user.l == 0 ||
        from_uri->user.l >= sizeof(outp->peer_user))
      return (USIPY_SIP_TM_ERR_BADMSG);
    actual_peer_user = &from_uri->user;
    if (peer_user != NULL && peer_user->l != 0 &&
        !usipy_str_eq(actual_peer_user, peer_user))
      return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    memcpy(outp->peer_user, actual_peer_user->s.ro, actual_peer_user->l);
    outp->peer_user[actual_peer_user->l] = '\0';
  }
  if (usipy_tm_uac_extract_register_expires(msg, actual_peer_user, &expires,
        &contactp) != 0 || contactp == NULL || contactp->addr_spec.l == 0)
    return (USIPY_SIP_TM_ERR_BADMSG);
  urip = usipy_sip_uri_parse(&cmsg->heap, &contactp->addr_spec);
  if (urip == NULL || urip->host.l == 0 || urip->host.l >= sizeof(outp->target_host))
    return (USIPY_SIP_TM_ERR_BADMSG);
  memcpy(outp->target_host, urip->host.s.ro, urip->host.l);
  outp->target_host[urip->host.l] = '\0';
  outp->target_port = (uint16_t)(urip->port != 0 ? urip->port : 5060);
  outp->contact_uri = contactp->addr_spec;
  outp->expires = expires;
  return (USIPY_SIP_TM_OK);
}

int
send_register_ok(struct netsim_sip *sp,
  const struct usipy_sip_tm_handle_incoming_in *hin, const struct usipy_msg *msg,
  const struct usipy_str *contact_urip, unsigned int expires)
{
  struct usipy_sip_tm_new_uas_tr_params tp;
  struct usipy_sip_tm_uas_response_params rp;
  struct usipy_sip_tm_extra_header eh;
  struct usipy_str contact_value;
  char contact_raw[NETSIM_SIP_CONTACT_BUFSIZE];
  size_t tx_index;
  int blen;
  int rval;

  tp = (struct usipy_sip_tm_new_uas_tr_params){
    .request = msg,
    .timers = hin->timers,
    .peer = hin->peer,
    .local = hin->local,
  };
  rp = (struct usipy_sip_tm_uas_response_params){
    .status = &usipy_sip_res_ok,
  };
  eh = (struct usipy_sip_tm_extra_header){
    .hf_type = USIPY_HF_CONTACT,
    .value_kind = USIPY_SIP_TM_EH_RAW,
  };
  blen = snprintf(contact_raw, sizeof(contact_raw), "<%.*s>;expires=%u",
    (int)contact_urip->l, contact_urip->s.ro, expires);
  if (blen < 0 || (size_t)blen >= sizeof(contact_raw))
    return (USIPY_SIP_TM_ERR_INVAL);
  contact_value = (struct usipy_str){.s.ro = contact_raw, .l = (size_t)blen};
  eh.value = &contact_value;
  rp.extra_headers = &eh;
  rp.nextra_headers = 1;
  rval = usipy_sip_tm_new_uas_tr(sp->tm, &tp, &tx_index);
  if (rval != USIPY_SIP_TM_OK)
    return (rval);
  return (usipy_sip_tm_send_uas_response(sp->tm, tx_index, &rp));
}

int
store_peer_registration(struct netsim_sip *sp,
  const struct usipy_sip_tm_handle_incoming_in *hin,
  const struct usipy_str *peer_user,
  const struct usipy_str *contact_urip, const char *target_host,
  uint16_t target_port, unsigned int expires)
{
  struct netsim_sip_peer_binding *bp;

  if (peer_user->l == 0)
    return (USIPY_SIP_TM_ERR_INVAL);
  bp = peer_binding_find(sp, peer_user);
  if (expires == 0) {
    if (bp != NULL)
      peer_binding_clear(bp);
    return (USIPY_SIP_TM_OK);
  }
  if (bp == NULL) {
    bp = peer_binding_store_slot(sp, peer_user);
    if (bp == NULL)
      return (USIPY_SIP_TM_ERR_NOSPC);
  }
  peer_binding_clear(bp);
  if (!peer_binding_set_peer_user(bp, peer_user) ||
      contact_urip->l >= sizeof(bp->contact_uri_buf))
    return (USIPY_SIP_TM_ERR_NOSPC);
  memcpy(bp->contact_uri_buf, contact_urip->s.ro, contact_urip->l);
  bp->contact_uri_buf[contact_urip->l] = '\0';
  strcpy(bp->target_host, target_host);
  bp->contact_uri = (struct usipy_str){
    .s.ro = bp->contact_uri_buf,
    .l = contact_urip->l,
  };
  bp->target.af = AF_INET;
  bp->target.port = target_port;
  bp->target.transport = USIPY_SIP_TM_TRANSPORT_UDP;
  bp->target.host = (struct usipy_str){
    .s.ro = bp->target_host,
    .l = strlen(bp->target_host),
  };
  bp->expires_at_ms = hin->now_ms + ((uint64_t)expires * 1000u);
  bp->valid = true;
  event_push_registered(sp, peer_user);
  return (USIPY_SIP_TM_OK);
}

#endif
