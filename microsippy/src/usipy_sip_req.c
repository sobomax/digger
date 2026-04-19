#include <stdint.h>
#include <stdlib.h>

#include "usipy_debug.h"
#include "public/usipy_str.h"
#include "usipy_tvpair.h"
#include "public/usipy_sip_sline.h"
#include "usipy_sip_uri.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_msg.h"
#include "usipy_sip_req.h"

int
usipy_sip_req_parse_ruri(struct usipy_msg *mp)
{
    USIPY_DASSERT(mp->kind == USIPY_SIP_MSG_REQ);

    if (mp->sline.parsed.rl.ruri != NULL)
        return (0);

    mp->sline.parsed.rl.ruri = usipy_sip_uri_parse(&mp->heap,
      &mp->sline.parsed.rl.onwire.ruri);
    if (mp->sline.parsed.rl.ruri == NULL)
        return (-1);
    return (0);
}
