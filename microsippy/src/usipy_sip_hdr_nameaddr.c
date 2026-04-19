#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "usipy_port/log.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "usipy_msg_heap_rb.h"
#include "usipy_msg_heap_inl.h"
#include "public/usipy_str.h"
#include "usipy_misc.h"
#include "usipy_sip_hdr.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr_nameaddr.h"

#define NA_SIZEOF(nparams) ( \
  sizeof(struct usipy_sip_hdr_nameaddr) + (sizeof(struct usipy_tvpair) * (nparams)) \
)

union usipy_sip_hdr_parsed
usipy_sip_hdr_nameaddr_parse(struct usipy_msg_heap *mhp,
  const struct usipy_str *hvp)
{
    struct usipy_str iup = *hvp;
    struct usipy_str paramspace = USIPY_STR_NULL;
    union usipy_sip_hdr_parsed usp = {.contact = NULL};
    struct usipy_sip_hdr_nameaddr *nap;
    struct usipy_msg_heap_cnt cnt;

    usp.contact = usipy_msg_heap_alloc_cnt(mhp, NA_SIZEOF(0), &cnt);
    if (usp.contact == NULL) {
        return (usp);
    }
    nap = usp.contact;
    *nap = (struct usipy_sip_hdr_nameaddr){0};

    if (usipy_str_split_elem(&iup, '<', &nap->display_name) != 0) {
        if (usipy_str_split_elem(&iup, ';', &nap->addr_spec) == 0) {
            paramspace = iup;
        } else {
            nap->addr_spec = iup;
        }
    } else {
         if (usipy_str_split_elem(&iup, '>', &nap->addr_spec) != 0) {
            /* No closing '>' */
            goto rollback;
         }
         if (iup.l > 0) {
            if (iup.l == 1 || iup.s.ro[0] != ';') {
                /* Some junk after closing '>' */
                goto rollback;
            }
            paramspace.l = iup.l - 1;
            paramspace.s.ro = iup.s.ro + 1;
         }
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
        if (usipy_msg_heap_aextend(mhp, NA_SIZEOF(nap->nparams + 1), &cnt) != 0) {
            goto rollback;
        }
        nap->params[nap->nparams].token = param_token;
        nap->params[nap->nparams].value = param_value;
        nap->nparams++;
    }

    return (usp);
rollback:
    usipy_msg_heap_cnt_rollback(mhp, &cnt);
    usp.contact = NULL;
    return (usp);
}

int
usipy_sip_hdr_nameaddr_build(const union usipy_sip_hdr_parsed *up, char *buf, size_t len)
{
    static const struct usipy_str lt = USIPY_2STR("<");
    static const struct usipy_str sp = USIPY_2STR(" ");
    static const struct usipy_str semi = USIPY_2STR(";");
    static const struct usipy_str eq = USIPY_2STR("=");
    const struct usipy_sip_hdr_nameaddr *nap = up->contact;
    size_t off = 0;

    USIPY_DASSERT(nap != NULL);
    if (nap->display_name.l != 0) {
        if (usipy_strbuf_append_pair(&nap->display_name, &sp, buf, len,
          &off) != 0) {
            return (-1);
        }
    }
    if (usipy_strbuf_append_pair(&lt, &nap->addr_spec, buf, len, &off) != 0 ||
      off + 1 > len) {
        return (-1);
    }
    buf[off++] = '>';
    for (int i = 0; i < nap->nparams; i++) {
        const struct usipy_tvpair *pp = &nap->params[i];

        if (usipy_strbuf_append_pair(&semi, &pp->token, buf, len,
          &off) != 0) {
            return (-1);
        }
        if (pp->value.l == 0) {
            continue;
        }
        if (usipy_strbuf_append_pair(&eq, &pp->value, buf, len,
          &off) != 0) {
            return (-1);
        }
    }
    return ((int)off);
}

void
usipy_sip_hdr_nameaddr_dump(const union usipy_sip_hdr_parsed *up, const char *log_tag,
  const char *log_pref, const char *canname)
{
    const struct usipy_sip_hdr_nameaddr *nap = up->contact;

    DUMP_STR(&nap, display_name, canname);
    DUMP_STR(&nap, addr_spec, canname);
    for (int i = 0; i < nap->nparams; i++) {
        DUMP_PARAM(nap, params, i, canname);
    }
}

const struct usipy_str *
usipy_sip_hdr_nameaddr_get_param(const struct usipy_sip_hdr_nameaddr *nap,
  const char *name)
{
    const size_t nlen = strlen(name);

    USIPY_DASSERT(nap != NULL);
    USIPY_DASSERT(name != NULL);

    for (int i = 0; i < nap->nparams; i++) {
        const struct usipy_tvpair *pp = &nap->params[i];

        if (pp->token.l != nlen || memcmp(pp->token.s.ro, name, nlen) != 0) {
            continue;
        }
        return (&pp->value);
    }
    return (NULL);
}
