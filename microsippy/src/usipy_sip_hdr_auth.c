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
#include "usipy_sip_hdr_auth.h"

#define AH_SIZEOF(nparams) ( \
  sizeof(struct usipy_sip_hdr_auth) + (sizeof(struct usipy_tvpair) * (nparams)) \
)

static void
usipy_sip_hdr_auth_assign_known(struct usipy_sip_hdr_auth *ap,
  const struct usipy_str *namep, const struct usipy_str *valuep)
{
    if (namep->l == 5 && memcmp(namep->s.ro, "realm", 5) == 0) {
        ap->realm = *valuep;
    } else if (namep->l == 5 && memcmp(namep->s.ro, "nonce", 5) == 0) {
        ap->nonce = *valuep;
    } else if (namep->l == 3 && memcmp(namep->s.ro, "qop", 3) == 0) {
        ap->qop = *valuep;
    } else if (namep->l == 9 && memcmp(namep->s.ro, "algorithm", 9) == 0) {
        ap->algorithm = *valuep;
    } else if (namep->l == 6 && memcmp(namep->s.ro, "opaque", 6) == 0) {
        ap->opaque = *valuep;
    } else {
        ap->params[ap->nparams].token = *namep;
        ap->params[ap->nparams].value = *valuep;
        ap->nparams++;
    }
}

static void
usipy_sip_hdr_auth_skip_delims(struct usipy_str *sp)
{

    USIPY_DASSERT(sp != NULL);
    while (sp->l != 0) {
        if (sp->s.ro[0] == ',') {
            sp->s.ro += 1;
            sp->l -= 1;
            continue;
        }
        if (USIPY_ISWS(sp->s.ro[0])) {
            usipy_str_trm_b(sp);
            continue;
        }
        break;
    }
}

static int
usipy_sip_hdr_auth_parse_value(struct usipy_str *paramspace,
  struct usipy_str *valuep)
{
    struct usipy_str vspace;

    USIPY_DASSERT(paramspace != NULL);
    USIPY_DASSERT(valuep != NULL);
    usipy_str_trm_b(paramspace);
    if (paramspace->l == 0) {
        return (-1);
    }
    if (paramspace->s.ro[0] == '"') {
        paramspace->s.ro += 1;
        paramspace->l -= 1;
        if (usipy_str_split_elem_nlws(paramspace, '"', valuep) != 0) {
            return (-1);
        }
        if (valuep->l == 0) {
            *valuep = USIPY_STR_NULL;
        }
    } else {
        vspace = *paramspace;
        if (usipy_str_split_elem(paramspace, ',', valuep) != 0) {
            *valuep = vspace;
            *paramspace = USIPY_STR_NULL;
        }
        usipy_str_trm_e(valuep);
    }
    usipy_sip_hdr_auth_skip_delims(paramspace);
    return (0);
}

union usipy_sip_hdr_parsed
usipy_sip_hdr_auth_parse(struct usipy_msg_heap *mhp, const struct usipy_str *hvp)
{
    struct usipy_str paramspace, pspace, pname, pvalue;
    union usipy_sip_hdr_parsed usp = {.auth = NULL};
    struct usipy_sip_hdr_auth *ap;
    struct usipy_msg_heap_cnt cnt;

    usp.auth = usipy_msg_heap_alloc_cnt(mhp, AH_SIZEOF(0), &cnt);
    if (usp.auth == NULL) {
        return (usp);
    }
    ap = usp.auth;
    *ap = (struct usipy_sip_hdr_auth){0};
    if (usipy_str_splitlws(hvp, &ap->scheme, &paramspace) != 0) {
        goto rollback;
    }
    usipy_str_trm_e(&ap->scheme);
    usipy_str_trm_b(&paramspace);
    if (ap->scheme.l == 0) {
        goto rollback;
    }
    while (paramspace.l != 0) {
        usipy_sip_hdr_auth_skip_delims(&paramspace);
        if (paramspace.l == 0) {
            break;
        }
        if (usipy_str_split(&paramspace, '=', &pname, &pspace) != 0) {
            goto rollback;
        }
        usipy_str_trm_e(&pname);
        if (pname.l == 0) {
            goto rollback;
        }
        paramspace = pspace;
        if (usipy_sip_hdr_auth_parse_value(&paramspace, &pvalue) != 0) {
            goto rollback;
        }
        if (usipy_msg_heap_aextend(mhp, AH_SIZEOF(ap->nparams + 1), &cnt) != 0) {
            goto rollback;
        }
        usipy_sip_hdr_auth_assign_known(ap, &pname, &pvalue);
    }
    return (usp);
rollback:
    usipy_msg_heap_cnt_rollback(mhp, &cnt);
    usp.auth = NULL;
    return (usp);
}

static int
usipy_sip_hdr_auth_append_value(char *buf, size_t len, size_t *offp,
  const struct usipy_str *namep, const struct usipy_str *valuep, int quote)
{
    int rval;

    if (*offp != 0) {
        rval = snprintf(buf + *offp, len - *offp, ",");
        if (rval < 0 || (size_t)rval >= len - *offp) {
            return (-1);
        }
        *offp += (size_t)rval;
    }
    if (quote) {
        rval = snprintf(buf + *offp, len - *offp, "%.*s=\"%.*s\"",
          USIPY_SFMT(namep), USIPY_SFMT(valuep));
    } else if (valuep->l != 0) {
        rval = snprintf(buf + *offp, len - *offp, "%.*s=%.*s",
          USIPY_SFMT(namep), USIPY_SFMT(valuep));
    } else {
        rval = snprintf(buf + *offp, len - *offp, "%.*s", USIPY_SFMT(namep));
    }
    if (rval < 0 || (size_t)rval >= len - *offp) {
        return (-1);
    }
    *offp += (size_t)rval;
    return (0);
}

int
usipy_sip_hdr_auth_build(const union usipy_sip_hdr_parsed *up, char *buf, size_t len)
{
    const struct usipy_sip_hdr_auth *ap = up->auth;
    static const struct usipy_str realm = USIPY_2STR("realm");
    static const struct usipy_str nonce = USIPY_2STR("nonce");
    static const struct usipy_str qop = USIPY_2STR("qop");
    static const struct usipy_str algorithm = USIPY_2STR("algorithm");
    static const struct usipy_str opaque = USIPY_2STR("opaque");
    size_t off = 0, poff = 0;
    int rval;

    USIPY_DASSERT(ap != NULL);
    rval = snprintf(buf + off, len - off, "%.*s", USIPY_SFMT(&ap->scheme));
    if (rval < 0 || (size_t)rval >= len - off) {
        return (-1);
    }
    off += (size_t)rval;
    if (ap->realm.l != 0 || ap->nonce.l != 0 || ap->qop.l != 0 || ap->algorithm.l != 0 ||
      ap->opaque.l != 0 || ap->nparams != 0) {
        if (off + 1 > len) {
            return (-1);
        }
        buf[off++] = ' ';
    }
    if (ap->realm.l != 0 &&
      usipy_sip_hdr_auth_append_value(buf + off, len - off, &poff, &realm, &ap->realm, 1) != 0) {
        return (-1);
    }
    if (ap->nonce.l != 0 &&
      usipy_sip_hdr_auth_append_value(buf + off, len - off, &poff, &nonce, &ap->nonce, 1) != 0) {
        return (-1);
    }
    if (ap->qop.l != 0 &&
      usipy_sip_hdr_auth_append_value(buf + off, len - off, &poff, &qop, &ap->qop,
      memchr(ap->qop.s.ro, ',', ap->qop.l) != NULL) != 0) {
        return (-1);
    }
    if (ap->algorithm.l != 0 &&
      usipy_sip_hdr_auth_append_value(buf + off, len - off, &poff, &algorithm, &ap->algorithm, 0) != 0) {
        return (-1);
    }
    if (ap->opaque.l != 0 &&
      usipy_sip_hdr_auth_append_value(buf + off, len - off, &poff, &opaque, &ap->opaque, 1) != 0) {
        return (-1);
    }
    for (int i = 0; i < ap->nparams; i++) {
        const struct usipy_tvpair *pp = &ap->params[i];
        const int quote = pp->value.l != 0 &&
          (memchr(pp->value.s.ro, ',', pp->value.l) != NULL ||
          memchr(pp->value.s.ro, ' ', pp->value.l) != NULL);

        if (usipy_sip_hdr_auth_append_value(buf + off, len - off, &poff, &pp->token, &pp->value,
          quote) != 0) {
            return (-1);
        }
    }
    off += poff;
    return ((int)off);
}

void
usipy_sip_hdr_auth_dump(const union usipy_sip_hdr_parsed *up, const char *log_tag,
  const char *log_pref, const char *canname)
{
    const struct usipy_sip_hdr_auth *ap = up->auth;

    DUMP_STR(&ap, scheme, canname);
    DUMP_STR(&ap, realm, canname);
    DUMP_STR(&ap, nonce, canname);
    DUMP_STR(&ap, qop, canname);
    DUMP_STR(&ap, algorithm, canname);
    DUMP_STR(&ap, opaque, canname);
    for (int i = 0; i < ap->nparams; i++) {
        DUMP_PARAM(ap, params, i, canname);
    }
}
