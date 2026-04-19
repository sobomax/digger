#pragma once

#include <stddef.h>
#include <string.h>

#include "usipy_debug.h"
#include "public/usipy_str.h"

extern const struct usipy_str usippy_sip_version;

static inline int
usipy_strbuf_append(const struct usipy_str *sp, char *buf, size_t len,
  size_t *offp)
{

    USIPY_DASSERT(sp != NULL);
    USIPY_DASSERT(buf != NULL);
    USIPY_DASSERT(offp != NULL);
    if (*offp + sp->l > len) {
        return (-1);
    }
    memcpy(buf + *offp, sp->s.ro, sp->l);
    *offp += sp->l;
    return (0);
}

static inline int
usipy_strbuf_append_pair(const struct usipy_str *sp1,
  const struct usipy_str *sp2, char *buf, size_t len, size_t *offp)
{

    USIPY_DASSERT(sp1 != NULL);
    USIPY_DASSERT(sp2 != NULL);
    USIPY_DASSERT(buf != NULL);
    USIPY_DASSERT(offp != NULL);
    if (usipy_strbuf_append(sp1, buf, len, offp) != 0) {
        return (-1);
    }
    return (usipy_strbuf_append(sp2, buf, len, offp));
}

int usipy_verify_sip_version(const struct usipy_str *);
