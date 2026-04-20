#include <stddef.h>

#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_response_utils.h"
#include "usipy_sip_res.h"

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
      .status = *statusp,
    };
    size_t tx_index;
    int rval;

    if (tm == NULL || hin == NULL || msg == NULL || statusp == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    rval = usipy_sip_tm_new_uas_tr(tm, &tp, &tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        return (rval);
    }
    return (usipy_sip_tm_send_uas_response(tm, tx_index, &rp));
}

int
usipy_sip_send_stateless_response(const struct usipy_msg *msg,
  const struct usipy_sip_status *statusp, usipy_sip_raw_send_cb send_cb,
  void *send_arg)
{
    struct usipy_msg *resp;
    int rval;

    if (msg == NULL || statusp == NULL || send_cb == NULL) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    resp = usipy_sip_res_ctor_fromreq(msg, statusp);
    if (resp == NULL) {
        return (USIPY_SIP_TM_ERR_NOMEM);
    }
    rval = send_cb(send_arg, resp->onwire.s.ro, resp->onwire.l);
    usipy_sip_msg_dtor(resp);
    return (rval == 0 ? USIPY_SIP_TM_OK : USIPY_SIP_TM_ERR_INVAL);
}
