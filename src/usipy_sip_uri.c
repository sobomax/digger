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
#include "usipy_misc.h"
#include "usipy_tvpair.h"
#include "usipy_sip_uri.h"

#define URI_SIZEOF(nparams) ( \
  sizeof(struct usipy_sip_uri) + (sizeof(struct usipy_tvpair) * (nparams)) \
)

struct usipy_sip_uri *
usipy_sip_uri_parse(struct usipy_msg_heap *mhp, const struct usipy_str *surip)
{
    struct usipy_str iup, pspace, hspace, pnum, userpass;
    struct usipy_sip_uri *up;
    struct usipy_msg_heap_cnt cnt;

    up = usipy_msg_heap_alloc_cnt(mhp, URI_SIZEOF(0), &cnt);
    if (up == NULL) {
        return (NULL);
    }
    *up = (struct usipy_sip_uri){0};
    iup = *surip;
    if (usipy_str_split_elem_nlws(&iup, ':', &up->proto) != 0)
        goto rollback;
    if (usipy_str_split_elem_nlws(&iup, '@', &userpass) == 0) {
        if (usipy_str_split(&userpass, ':', &up->user, &up->password) != 0) {
            up->user = userpass;
        }
    }
    if (usipy_str_split_elem_nlws(&iup, ';', &pnum) != 0) {
        pspace = USIPY_STR_NULL;
        if (usipy_str_split_elem_nlws(&iup, '?', &pnum) != 0) {
            pnum = iup;
            iup = USIPY_STR_NULL;
            hspace = USIPY_STR_NULL;
        } else {
            hspace = iup;
        }
    } else {
        if (usipy_str_split_elem_nlws(&iup, '?', &pspace) != 0) {
            pspace = iup;
            hspace = USIPY_STR_NULL;
        } else {
            hspace = iup;
        }
    }
    if (pnum.l == 0) {
        goto rollback;
    }
    if (usipy_str_split_elem_nlws(&pnum, ':', &up->host) == 0) {
        if (usipy_str_atoui_range(&pnum, &up->port, 1, 65535) != 0) {
            goto rollback;
        }
    } else {
        up->host = pnum;
    }

    while (pspace.l != 0) {
        if (up->nparams == 0)
            up->parameters = &up->_tvpstorage[0];
        struct usipy_str thisparam;
        if (usipy_str_split_elem_nlws(&pspace, ';', &thisparam) != 0) {
            thisparam = pspace;
            pspace.l = 0;
        }
        struct usipy_str param_token, param_value;
        if (usipy_str_split(&thisparam, '=', &param_token, &param_value) != 0) {
            param_token = thisparam;
            param_value = USIPY_STR_NULL;
        }
        if (usipy_msg_heap_aextend(mhp, URI_SIZEOF(up->nparams + 1), &cnt) != 0) {
            goto rollback;
        }
        up->parameters[up->nparams].token = param_token;
        up->parameters[up->nparams].value = param_value;
        up->nparams++;
    }
    while (hspace.l != 0) {
        if (up->nhdrs == 0)
            up->headers = &up->_tvpstorage[up->nparams];
        struct usipy_str thishdr;
        if (usipy_str_split_elem_nlws(&hspace, '&', &thishdr) != 0) {
            thishdr = hspace;
            hspace.l = 0;
        }
        struct usipy_str hdr_token, hdr_value;
        if (usipy_str_split(&thishdr, '=', &hdr_token, &hdr_value) != 0) {
            hdr_token = thishdr;
            hdr_value = USIPY_STR_NULL;
        }
        if (usipy_msg_heap_aextend(mhp, URI_SIZEOF(up->nhdrs + up->nparams + 1), &cnt) != 0) {
            goto rollback;
        }
        up->headers[up->nhdrs].token = hdr_token;
        up->headers[up->nhdrs].value = hdr_value;
        up->nhdrs++;
    }

    return (up);
rollback:
    usipy_msg_heap_cnt_rollback(mhp, &cnt);
    return (NULL);
}

int
usipy_sip_uri_build(const struct usipy_sip_uri *up, char *buf, size_t len)
{
    static const struct usipy_str semi = USIPY_2STR(";");
    static const struct usipy_str eq = USIPY_2STR("=");
    static const struct usipy_str qmark = USIPY_2STR("?");
    static const struct usipy_str amp = USIPY_2STR("&");
    static const struct usipy_str colon = USIPY_2STR(":");
    size_t off = 0;
    int rval;

    USIPY_DASSERT(up != NULL);
    USIPY_DASSERT(buf != NULL);

    if (usipy_strbuf_append_pair(&up->proto, &colon, buf, len, &off) != 0) {
        return (-1);
    }
    if (up->user.l != 0) {
        if (usipy_strbuf_append(&up->user, buf, len, &off) != 0) {
            return (-1);
        }
        if (up->password.l != 0) {
            if (usipy_strbuf_append_pair(&colon, &up->password, buf, len,
              &off) != 0) {
                return (-1);
            }
        }
        if (len - off < 1) {
            return (-1);
        }
        buf[off++] = '@';
    }
    if (usipy_strbuf_append(&up->host, buf, len, &off) != 0) {
        return (-1);
    }
    if (up->port != 0) {
        rval = snprintf(buf + off, len - off, ":%u", up->port);
        if (rval < 0 || (size_t)rval >= len - off) {
            return (-1);
        }
        off += (size_t)rval;
    }
    for (int i = 0; i < up->nparams; i++) {
        if (usipy_strbuf_append_pair(&semi, &up->parameters[i].token, buf,
          len, &off) != 0) {
            return (-1);
        }
        if (up->parameters[i].value.l != 0) {
            if (usipy_strbuf_append_pair(&eq, &up->parameters[i].value,
              buf, len, &off) != 0) {
                return (-1);
            }
        }
    }
    for (int i = 0; i < up->nhdrs; i++) {
        if (usipy_strbuf_append_pair(i == 0 ? &qmark : &amp,
          &up->headers[i].token, buf, len, &off) != 0) {
            return (-1);
        }
        if (up->headers[i].value.l != 0) {
            if (usipy_strbuf_append_pair(&eq, &up->headers[i].value,
              buf, len, &off) != 0) {
                return (-1);
            }
        }
    }
    return ((int)off);
}

void
usipy_sip_uri_dump(const struct usipy_sip_uri *up, const char *log_tag,
  const char *log_pref)
{

    DUMP_STR(&up, proto, "");
    DUMP_STR(&up, user, "");
    DUMP_STR(&up, password, "");
    DUMP_STR(&up, host, "");
    if (up->port > 0)
        DUMP_UINT(up, port, "");
    for (int i = 0; i < up->nparams; i++) {
        DUMP_PARAM(up, parameters, i, "");
    }
    for (int i = 0; i < up->nhdrs; i++) {
        DUMP_PARAM(up, headers, i, "");
    }
}
