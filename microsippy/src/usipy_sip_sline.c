#include <stdint.h>
#include <stdlib.h>

#include "usipy_debug.h"
#include "usipy_misc.h"
#include "public/usipy_str.h"
#include "public/usipy_sip_sline.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_msg.h"
#include "usipy_sip_method_db.h"
#include "usipy_tvpair.h"
#include "usipy_sip_uri.h"

enum usipy_sip_msg_kind
usipy_sip_sline_parse(struct usipy_msg_heap *mhp, struct usipy_sip_sline *slp)
{
    struct usipy_str s1, s2, s3, s4;
    enum usipy_sip_msg_kind r;

    if (usipy_str_split(&slp->onwire, USIPY_SP, &s1, &s2) != 0 || s1.l == 0 ||
      s2.l == 0)
        return (USIPY_SIP_MSG_UNKN);
    if (usipy_str_split(&s2, USIPY_SP, &s3, &s4) != 0 || s3.l == 0)
        return (USIPY_SIP_MSG_UNKN);
    if (usipy_verify_sip_version(&s1)) {
        if (usipy_str_atoui_range(&s3, &slp->parsed.sl.status.code,
          USIPY_SIP_SCODE_MIN, USIPY_SIP_SCODE_MAX) != 0)
            return (USIPY_SIP_MSG_UNKN);
        slp->parsed.sl.version = s1;
        slp->parsed.sl.status.reason_phrase = s4;
        r = USIPY_SIP_MSG_RES;
    } else if (s4.l != 0 && usipy_verify_sip_version(&s4)) {
        slp->parsed.rl.method = usipy_method_db_lookup(&s1);
        slp->parsed.rl.ruri = usipy_sip_uri_parse(mhp, &s3);
        if (slp->parsed.rl.method == NULL || slp->parsed.rl.ruri == NULL) {
            return (USIPY_SIP_MSG_UNKN);
        }
        slp->parsed.rl.version = s4;
        slp->parsed.rl.onwire.method = s1;
        slp->parsed.rl.onwire.ruri = s3;
        slp->parsed.rl.onwire.version = s4;
        r = USIPY_SIP_MSG_REQ;
    } else {
	return (USIPY_SIP_MSG_UNKN);
    }
    return (r);
}
