#include "public/usipy_sip_tm_utils.h"
#include "usipy_sip_tm_priv.h"

void
usipy_sip_tm_reap_terminated(struct usipy_sip_tm *tm)
{
    size_t i, nactive;

    if (tm == NULL) {
        return;
    }
    nactive = usipy_sip_tm_nactive(tm);
    if (nactive == 0) {
        return;
    }
    for (i = 0; i < tm->max_transactions; i++) {
        const struct usipy_sip_tm_tx *txp = usipy_sip_tm_get_transaction(tm, i);

        if (txp != NULL && txp->state == USIPY_SIP_TM_STATE_TERMINATED) {
            (void)usipy_sip_tm_drop_transaction(tm, i);
        }
    }
}
