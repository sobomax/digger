#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>

#include "usipy_port/log.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_str.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_cseq.h"
#include "usipy_sip_method_db.h"

#define USIPY_SIP_SEQ_MIN 0
#define USIPY_SIP_SEQ_MAX 0xffffffff

union usipy_sip_hdr_parsed
usipy_sip_hdr_cseq_parse(struct usipy_msg_heap *mhp,
  const struct usipy_str *hvp)
{
    struct usipy_str s1, s2;
    uint32_t r;
    union usipy_sip_hdr_parsed usp = {.cseq = NULL};

    if (usipy_str_splitlws(hvp, &s1, &s2) != 0) {
        return (usp);
    }
    if (usipy_str_atoui_range(&s1, &r, USIPY_SIP_SEQ_MIN, USIPY_SIP_SEQ_MAX) != 0) {
        return (usp);
    }
    usp.cseq = usipy_msg_heap_alloc(mhp, sizeof(struct usipy_sip_hdr_cseq));
    if (usp.cseq == NULL) {
        return (usp);
    }
    usp.cseq->val = r;
    usp.cseq->method = usipy_method_db_lookup(&s2);
    usp.cseq->onwire.method = s2;
    return (usp);
}

int
usipy_sip_hdr_cseq_build(const union usipy_sip_hdr_parsed *up, char *buf, size_t len)
{
    int rval;

    USIPY_DASSERT(up->cseq != NULL);
    USIPY_DASSERT(up->cseq->method != NULL);
    USIPY_DASSERT(up->cseq->method->cantype != USIPY_SIP_METHOD_generic);
    USIPY_DASSERT(up->cseq->method->name.l != 0);
    rval = snprintf(buf, len, "%u %.*s", up->cseq->val,
      USIPY_SFMT(&up->cseq->method->name));
    if (rval < 0 || (size_t)rval >= len) {
        return (-1);
    }
    return (rval);
}

#define DUMP_METHOD(sname) \
    USIPY_LOGI(log_tag, "%s%s." #sname " = \"%.*s\" (%d)", log_pref, canname, \
      USIPY_SFMT(&csp->sname->name), csp->sname->cantype)

void
usipy_sip_hdr_cseq_dump1(const struct usipy_sip_hdr_cseq *csp, const char *log_tag,
  const char *log_pref, const char *canname)
{
    DUMP_UINT(csp, val, canname);
    DUMP_METHOD(method);
}

void
usipy_sip_hdr_cseq_dump(const union usipy_sip_hdr_parsed *up, const char *log_tag,
  const char *log_pref, const char *canname)
{
    usipy_sip_hdr_cseq_dump1(up->cseq, log_tag, log_pref, canname);
}
