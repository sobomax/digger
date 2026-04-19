#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "usipy_port/log.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "usipy_msg_heap_rb.h"
#include "usipy_msg_heap_inl.h"
#include "public/usipy_str.h"
#include "usipy_sip_hdr.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr_via.h"

#define VH_SIZEOF(nparams) ( \
  sizeof(struct usipy_sip_hdr_via) + (sizeof(struct usipy_tvpair) * (nparams)) \
)

union usipy_sip_hdr_parsed
usipy_sip_hdr_via_parse(struct usipy_msg_heap *mhp,
  const struct usipy_str *hvp)
{
    struct usipy_str s4;
    struct usipy_str sent_by, sent_by_port;
    union usipy_sip_hdr_parsed usp = {.via = NULL};
    struct usipy_sip_hdr_via *vp;
    struct usipy_msg_heap_cnt cnt;

    usp.via = usipy_msg_heap_alloc_cnt(mhp, VH_SIZEOF(0), &cnt);
    if (usp.via == NULL) {
        return (usp);
    }
    vp = usp.via;
    *vp = (struct usipy_sip_hdr_via){0};

    if (usipy_str_split3(hvp, '/', &vp->sent_protocol.name, &vp->sent_protocol.version, &s4) != 0) {
        goto rollback;
    }
    usipy_str_ltrm_b(&s4); /* UDP */
    if (usipy_str_splitlws(&s4, &vp->sent_protocol.transport, &s4) != 0) {
        goto rollback;
    }
    usipy_str_ltrm_e(&vp->sent_protocol.name); /* SIP */
    usipy_str_ltrm_b(&vp->sent_protocol.version); /* 2.0 */
    usipy_str_ltrm_e(&vp->sent_protocol.version);
    usipy_str_ltrm_b(&s4);
    struct usipy_str paramspace;
    if (usipy_str_split(&s4, ';', &sent_by, &paramspace) != 0) {
        sent_by = s4;
    } else {
        usipy_str_ltrm_e(&sent_by);
        usipy_str_ltrm_b(&paramspace);
    }
    if (sent_by.l == 0) {
        goto rollback;
    }
    if (usipy_str_split(&sent_by, ':', &vp->sent_by.host, &sent_by_port) == 0) {
        usipy_str_ltrm_e(&vp->sent_by.host);
        usipy_str_ltrm_b(&sent_by_port);
        if (usipy_str_atoui_range(&sent_by_port, &vp->sent_by.port, 1, 65535) != 0) {
            goto rollback;
        }
    } else {
        vp->sent_by.host = sent_by;
    }
    while (paramspace.l != 0) {
        struct usipy_str thisparam;
        if (usipy_str_split_elem(&paramspace, ';', &thisparam) != 0) {
            thisparam = paramspace;
            paramspace.l = 0;
        }
        struct usipy_str param_token, param_value;
        if (usipy_str_split(&thisparam, '=', &param_token, &param_value) == 0) {
            usipy_str_ltrm_e(&param_token);
            usipy_str_ltrm_b(&param_value);
        } else {
            param_token = thisparam;
            param_value = USIPY_STR_NULL;
        }
        if (usipy_msg_heap_aextend(mhp, VH_SIZEOF(vp->nparams + 1), &cnt) != 0) {
            goto rollback;
        }
        vp->params[vp->nparams].token = param_token;
        vp->params[vp->nparams].value = param_value;
        vp->nparams++;
    }

    return (usp);
rollback:
    usipy_msg_heap_cnt_rollback(mhp, &cnt);
    usp.via = NULL;
    return (usp);
}

int
usipy_sip_hdr_via_build(const union usipy_sip_hdr_parsed *up, char *buf, size_t len)
{
    const struct usipy_sip_hdr_via *vp = up->via;
    size_t off = 0;

    USIPY_DASSERT(vp != NULL);
#define APPEND_STR(sp) do { \
        if (off + (sp)->l > len) return (-1); \
        memcpy(buf + off, (sp)->s.ro, (sp)->l); \
        off += (sp)->l; \
    } while (0)
#define APPEND_CH(ch) do { \
        if (off + 1 > len) return (-1); \
        buf[off++] = (ch); \
    } while (0)
    APPEND_STR(&vp->sent_protocol.name);
    APPEND_CH('/');
    APPEND_STR(&vp->sent_protocol.version);
    APPEND_CH('/');
    APPEND_STR(&vp->sent_protocol.transport);
    APPEND_CH(' ');
    APPEND_STR(&vp->sent_by.host);
    if (vp->sent_by.port != 0) {
        int rval;

        APPEND_CH(':');
        rval = snprintf(buf + off, len - off, "%u", vp->sent_by.port);
        if (rval < 0 || (size_t)rval >= len - off) {
            return (-1);
        }
        off += rval;
    }
    for (int i = 0; i < vp->nparams; i++) {
        const struct usipy_tvpair *pp = &vp->params[i];

        APPEND_CH(';');
        APPEND_STR(&pp->token);
        if (pp->value.l == 0) {
            continue;
        }
        APPEND_CH('=');
        APPEND_STR(&pp->value);
    }
#undef APPEND_CH
#undef APPEND_STR
    return ((int)off);
}

void
usipy_sip_hdr_via_dump(const union usipy_sip_hdr_parsed *up, const char *log_tag,
  const char *log_pref, const char *canname)
{
    const struct usipy_sip_hdr_via *vp = up->via;

    DUMP_STR(&vp, sent_protocol.name, canname);
    DUMP_STR(&vp, sent_protocol.version, canname);
    DUMP_STR(&vp, sent_protocol.transport, canname);
    DUMP_STR(&vp, sent_by.host, canname);
    if (vp->sent_by.port > 0)
        DUMP_UINT(vp, sent_by.port, canname);
    for (int i = 0; i < vp->nparams; i++) {
        DUMP_PARAM(vp, params, i, canname);
    }
}
