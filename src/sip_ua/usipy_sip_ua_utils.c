#include <stddef.h>

#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_ua_utils.h"
#include "public/usipy_str.h"
#include "usipy_tm_uac.h"
#include "usipy_tvpair.h"
#include "usipy_sip_uri.h"

bool
usipy_sip_tm_addr_same(const struct usipy_sip_tm_addr *ap,
  const struct usipy_sip_tm_addr *bp)
{

    if (ap == NULL || bp == NULL) {
        return (false);
    }
    return (ap->af == bp->af &&
      ap->port == bp->port &&
      ap->transport == bp->transport &&
      usipy_str_eq(&ap->host, &bp->host));
}

bool
usipy_sip_ua_request_targets_user(const struct usipy_msg *msg,
  const struct usipy_str *usernamep)
{
    const struct usipy_sip_uri *urip;

    if (msg == NULL || usernamep == NULL || msg->kind != USIPY_SIP_MSG_REQ) {
        return (false);
    }
    urip = msg->sline.parsed.rl.ruri;
    if (urip == NULL) {
        return (false);
    }
    return (usipy_str_eq(&urip->user, usernamep));
}

int
usipy_sip_register_start(struct usipy_sip_register_state *statep,
  const struct usipy_sip_register_start_params *paramsp, size_t *indexp)
{
    struct usipy_sip_tm_new_uac_tr_params tp = {0};
    size_t tx_index;
    int rval;

    if (statep == NULL || paramsp == NULL || paramsp->tm == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (statep->registering) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    tp.request_id.call_id = paramsp->call_id;
    tp.request_id.cseq = statep->next_cseq;
    tp.request_id.method_type = USIPY_SIP_METHOD_REGISTER;
    tp.request_target.request_uri = paramsp->request_uri;
    tp.request_target.target = paramsp->target;
    tp.parties_by_username.from = paramsp->username;
    tp.parties_by_username.to = paramsp->username;
    tp.parties_by_username.contact = paramsp->username;
    tp.contact_expires = statep->requested_expires;
    tp.callbacks = paramsp->callbacks;
    statep->auth_retry_started = 0;
    statep->registering = 1;
    statep->status = 0;
    rval = usipy_sip_tm_new_uac_tr(paramsp->tm, &tp, &tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        statep->registering = 0;
        return (rval);
    }
    if (indexp != NULL) {
        *indexp = tx_index;
    }
    return (USIPY_SIP_TM_OK);
}

int
usipy_sip_register_handle_response(struct usipy_sip_register_state *statep,
  struct usipy_sip_tm *tm, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg, const struct usipy_str *usernamep,
  const struct usipy_str *passwordp, const struct usipy_str *qopp,
  uint64_t now_ms, enum usipy_sip_register_response_result *resultp)
{
    const unsigned int scode = msg->sline.parsed.sl.status.code;
    int rval;

    if (resultp != NULL) {
        *resultp = USIPY_SIP_REGISTER_RESPONSE_ERROR;
    }
    if (statep == NULL || tm == NULL || txp == NULL || msg == NULL ||
      usernamep == NULL || passwordp == NULL || qopp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    statep->status = (uint16_t)scode;
    statep->next_cseq = txp->common.id.cseq + 1;
    if ((scode == 401 || scode == 407) && !statep->auth_retry_started) {
        rval = usipy_tm_uac_register_reply_auth(tm, tx_index, msg, usernamep,
          passwordp, qopp, NULL, 0);
        if (rval != USIPY_SIP_TM_OK) {
            statep->registering = 0;
            statep->auth_retry_started = 0;
            return (rval);
        }
        statep->auth_retry_started = 1;
        if (resultp != NULL) {
            *resultp = USIPY_SIP_REGISTER_RESPONSE_AUTH_RETRY;
        }
        return (USIPY_SIP_TM_OK);
    }
    if (scode < 200) {
        if (resultp != NULL) {
            *resultp = USIPY_SIP_REGISTER_RESPONSE_PENDING;
        }
        return (USIPY_SIP_TM_OK);
    }
    statep->registering = 0;
    statep->auth_retry_started = 0;
    if (scode < 300) {
        if (usipy_tm_uac_extract_register_expires(msg, usernamep,
              &statep->expires) != 0 ||
          usipy_sip_ua_schedule_refresh(statep->expires, now_ms,
            &statep->next_refresh_at_ms) != 0) {
            return (USIPY_SIP_TM_ERR_BADMSG);
        }
        statep->registered_once = 1;
        if (resultp != NULL) {
            *resultp = USIPY_SIP_REGISTER_RESPONSE_ESTABLISHED;
        }
        return (USIPY_SIP_TM_OK);
    }
    if (resultp != NULL) {
        *resultp = USIPY_SIP_REGISTER_RESPONSE_FINAL;
    }
    return (USIPY_SIP_TM_OK);
}

void
usipy_sip_register_handle_timeout(struct usipy_sip_register_state *statep)
{

    if (statep == NULL) {
        return;
    }
    statep->registering = 0;
    statep->auth_retry_started = 0;
}

int
usipy_sip_ua_schedule_refresh(unsigned int expires, uint64_t now_ms,
  uint64_t *next_refresh_at_msp)
{
    uint64_t refresh_delay_s;

    if (next_refresh_at_msp == NULL || expires == 0) {
        return (-1);
    }
    if (expires > 90) {
        refresh_delay_s = expires - 30u;
    } else {
        refresh_delay_s = expires / 2u;
    }
    if (refresh_delay_s == 0) {
        refresh_delay_s = 1;
    }
    *next_refresh_at_msp = now_ms + (refresh_delay_s * 1000u);
    return (0);
}

int
usipy_sip_ua_reset(struct usipy_sip_ua **uapp,
  const struct usipy_sip_ua_ctor_params *ctorp)
{
    struct usipy_sip_ua *uap;

    if (uapp == NULL || ctorp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (*uapp != NULL) {
        usipy_sip_ua_dtor(*uapp);
    }
    uap = usipy_sip_ua_ctor(ctorp);
    if (uap == NULL) {
        *uapp = NULL;
        return (USIPY_SIP_TM_ERR_NOMEM);
    }
    *uapp = uap;
    return (USIPY_SIP_TM_OK);
}
