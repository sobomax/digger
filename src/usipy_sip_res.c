#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "usipy_debug.h"
#include "usipy_types.h"
#include "public/usipy_str.h"
#include "public/usipy_sip_sline.h"
#include "public/usipy_msg_heap.h"
#include "usipy_msg_heap_rb.h"
#include "usipy_msg_heap_inl.h"
#include "public/usipy_sip_msg.h"
#include "usipy_sip_hdr.h"
#include "public/usipy_sip_hdr_types.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_res.h"

const struct usipy_sip_status usipy_sip_res_trying = {
  .code = 100,
  .reason_phrase = USIPY_2STR("Trying"),
};

const struct usipy_sip_status usipy_sip_res_ringing = {
  .code = 180,
  .reason_phrase = USIPY_2STR("Ringing"),
};

const struct usipy_sip_status usipy_sip_res_ok = {
  .code = 200,
  .reason_phrase = USIPY_2STR("OK"),
};

const struct usipy_sip_status usipy_sip_res_not_impl = {
  .code = 501,
  .reason_phrase = USIPY_2STR("Not Implemented"),
};

const struct usipy_sip_status usipy_sip_res_unauth = {
  .code = 401,
  .reason_phrase = USIPY_2STR("Unauthorized"),
};

const struct usipy_sip_status usipy_sip_res_busy_here = {
  .code = 486,
  .reason_phrase = USIPY_2STR("Busy Here"),
};

const struct usipy_sip_status usipy_sip_res_req_term = {
  .code = 487,
  .reason_phrase = USIPY_2STR("Request Terminated"),
};

static void
scode2str(unsigned int scode, char *res)
{

    res[0] = '0' + (scode / 100);
    res[1] = '0' + (scode % 100) / 10;
    res[2] = '0' + (scode % 10);
    res[3] = ' ';
}

struct append_hdr {
    unsigned char type;
    struct usipy_str value;
};

static const struct append_hdr append_hdrs[] = {
    {
        .type = USIPY_HF_SERVER,
        .value = USIPY_2STR("uSippy")
    },
    {
        .type = USIPY_HF_CONTENTLENGTH,
        .value = USIPY_2STR("0")
    },
    {
        .value = USIPY_STR_NULL
    }
};

static const struct {
    uint64_t copyfirst;
    uint64_t copyall;
    const struct append_hdr *append_hdrs;
} res_tmpl = {
    .copyfirst = USIPY_HFT_MASK(USIPY_HF_FROM) | USIPY_HFT_MASK(USIPY_HF_CALLID) | \
      USIPY_HFT_MASK(USIPY_HF_TO) | USIPY_HFT_MASK(USIPY_HF_CSEQ),
    .copyall = USIPY_HFT_MASK(USIPY_HF_VIA) | USIPY_HFT_MASK(USIPY_HF_RECORDROUTE),
    .append_hdrs = append_hdrs
};

static size_t
usipy_sip_res_to_tag_extra(const struct usipy_str *tagp)
{
    return (tagp != NULL && tagp->l != 0 ? sizeof(";tag=") - 1 + tagp->l : 0);
}

static size_t
usipy_sip_res_append_raw_size(void)
{
    size_t raw_len;

    raw_len = USIPY_CRLF_LEN;
    for (int i = 0; append_hdrs[i].value.l != 0; i++) {
        const struct append_hdr *ahp = &append_hdrs[i];
        const struct usipy_hdr_db_entr *hfp = usipy_hdr_db_byid(ahp->type);

        USIPY_DASSERT(hfp != NULL);
        raw_len += hfp->name.l + sizeof(": ") - 1 + ahp->value.l + USIPY_CRLF_LEN;
    }
    return (raw_len);
}

static int
usipy_sip_res_alloc_extra_hdr_space(size_t *spacep)
{
    size_t extra_hdr_space;

    USIPY_DASSERT(spacep != NULL);

    extra_hdr_space = 0;
    for (int i = 0; append_hdrs[i].value.l != 0; i++) {
        extra_hdr_space += sizeof(struct usipy_sip_hdr);
    }
    *spacep = USIPY_ALIGNED_SIZE(extra_hdr_space);
    return (0);
}

static int
usipy_sip_res_alloc_size_build(const struct usipy_msg *reqp, const struct usipy_str *tagp,
  size_t *raw_spacep, size_t *heap_spacep)
{
    size_t raw_len, extra_hdr_space;

    USIPY_DASSERT(reqp != NULL);
    USIPY_DASSERT(raw_spacep != NULL);
    USIPY_DASSERT(heap_spacep != NULL);

    raw_len = reqp->onwire.l + usipy_sip_res_append_raw_size() +
      usipy_sip_res_to_tag_extra(tagp);
    usipy_sip_res_alloc_extra_hdr_space(&extra_hdr_space);
    *raw_spacep = USIPY_ALIGNED_SIZE(raw_len);
    *heap_spacep = USIPY_ALIGNED_SIZE(sizeof(struct usipy_sip_hdr) * reqp->nhdrs) +
      extra_hdr_space;
    return (sizeof(struct usipy_msg) + *raw_spacep + *heap_spacep);
}

static int
usipy_sip_res_alloc_size_ctor(const struct usipy_msg *reqp, const struct usipy_str *tagp,
  size_t *raw_spacep, size_t *heap_spacep)
{
    size_t extra_hdr_space;

    USIPY_DASSERT(reqp != NULL);
    USIPY_DASSERT(raw_spacep != NULL);
    USIPY_DASSERT(heap_spacep != NULL);

    usipy_sip_res_alloc_size_build(reqp, tagp, raw_spacep, heap_spacep);
    usipy_sip_res_alloc_extra_hdr_space(&extra_hdr_space);
    *heap_spacep = USIPY_ALIGNED_SIZE(reqp->heap.tsize + extra_hdr_space + *raw_spacep);
    return (sizeof(struct usipy_msg) + *raw_spacep + *heap_spacep);
}

static struct usipy_sip_hdr *
get_next_ohp(struct usipy_msg *rp, struct usipy_msg_heap_cnt *cnp)
{
    if (rp->hdrs == NULL) {
        memset(cnp, '\0', sizeof(*cnp));
        rp->hdrs = usipy_msg_heap_alloc_cnt(&rp->heap, sizeof(rp->hdrs[0]),
          cnp);
        if (rp->hdrs == NULL)
            return (NULL);
        rp->nhdrs = 1;
        return (&(rp->hdrs[0]));
    }
    if (usipy_msg_heap_aextend(&rp->heap, (rp->nhdrs + 1) * sizeof(rp->hdrs[0]),
      cnp) != 0)
        return (NULL);
    rp->nhdrs += 1;
    return (&(rp->hdrs[rp->nhdrs - 1]));
}

static void
usipy_sip_res_compact(struct usipy_msg *rp, struct usipy_msg_heap_cnt *mcnt,
  struct usipy_msg_heap *hp, size_t raw_space)
{
    char *new_heapstart;
    ptrdiff_t doff;

    USIPY_DASSERT(rp != NULL);
    USIPY_DASSERT(mcnt != NULL);
    USIPY_DASSERT(hp != NULL);
    USIPY_DASSERT(raw_space >= USIPY_ALIGNED_SIZE(rp->onwire.l));

    new_heapstart = rp->_storage + USIPY_ALIGNED_SIZE(rp->onwire.l);
    doff = (char *)rp->heap.first - new_heapstart;
    if (doff > 0 && rp->heap.alen != 0) {
        memmove(new_heapstart, rp->heap.first, rp->heap.alen);
        if (rp->hdrs != NULL) {
            rp->hdrs = (struct usipy_sip_hdr *)((char *)rp->hdrs - doff);
        }
    }
    rp->heap.first = new_heapstart;
    usipy_msg_heap_cnt_reclaim(hp, mcnt, offsetof(struct usipy_msg, _storage) +
      USIPY_ALIGNED_SIZE(rp->onwire.l) + rp->heap.tsize);
}

struct usipy_msg *
usipy_sip_res_build_fromreq_tagged_sz(struct usipy_msg_heap *hp,
  const struct usipy_msg *reqp, const struct usipy_sip_status *slp,
  const struct usipy_str *tagp, size_t raw_space, size_t heap_space, int compact)
{
    uint64_t copyfirst = res_tmpl.copyfirst;
    uint64_t copyall = res_tmpl.copyall;
    struct usipy_msg *rp;
    struct usipy_msg_heap_cnt mcnt;
    const struct usipy_sip_request_line *rlin = &(reqp->sline.parsed.rl);
    const size_t total_size = sizeof(struct usipy_msg) + raw_space + heap_space;

    USIPY_DASSERT(hp != NULL);
    rp = usipy_msg_heap_alloc_cnt(hp, total_size, &mcnt);
    if (rp == NULL) {
        return (NULL);
    }
    memset(rp, '\0', sizeof(*rp));

    rp->kind = USIPY_SIP_MSG_RES;
    char *cp;
    struct usipy_sip_status_line *slout = &(rp->sline.parsed.sl);
    const struct usipy_str *vp;
    cp = rp->onwire.s.rw = &(rp->_storage[0]);

    void *heapstart = rp->_storage + raw_space;
    size_t heapsize = heap_space;
    usipy_msg_heap_init(&rp->heap, heapstart, heapsize, NULL, 0);

    vp = &rlin->version;
    if (vp->l == 0) {
        vp = &rlin->onwire.version;
    }
    slout->version.s.rw = cp;
    memcpy(cp, vp->s.ro, vp->l);
    slout->version.l = vp->l;
    cp += vp->l;
    cp[0] = ' ';
    cp += 1;
    scode2str(slp->code, cp);
    slout->status.code = slp->code;
    cp += 4;
    slout->status.reason_phrase.s.rw = cp;
    memcpy(cp, slp->reason_phrase.s.ro, slp->reason_phrase.l);
    slout->status.reason_phrase.l = slp->reason_phrase.l;
    cp += slp->reason_phrase.l;
    memcpy(cp, USIPY_CRLF, USIPY_CRLF_LEN);
    cp += USIPY_CRLF_LEN;

    struct usipy_msg_heap_cnt cnt;
    for (int i = 0; i < reqp->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = &reqp->hdrs[i];

        if (USIPY_HF_ISMSET(copyfirst, shp->hf_type->cantype)) {
            struct usipy_sip_hdr *ohp = get_next_ohp(rp, &cnt);
            if (ohp == NULL)
                goto e0;
            ohp->hf_type = ohp->onwire.hf_type = shp->hf_type;
            memcpy(cp, shp->hf_type->name.s.ro, shp->hf_type->name.l);
            ohp->onwire.name.s.ro = ohp->onwire.full.s.ro = cp;
            ohp->onwire.name.l = shp->hf_type->name.l;
            cp += shp->hf_type->name.l;
            memcpy(cp, ": ", 2);
            cp += 2;
            memcpy(cp, shp->onwire.value.s.ro, shp->onwire.value.l);
            ohp->onwire.value.s.ro = cp;
            ohp->onwire.value.l = shp->onwire.value.l;
            cp += shp->onwire.value.l;
            if (shp->hf_type->cantype == USIPY_HF_TO && tagp != NULL && tagp->l != 0) {
                memcpy(cp, ";tag=", sizeof(";tag=") - 1);
                cp += sizeof(";tag=") - 1;
                memcpy(cp, tagp->s.ro, tagp->l);
                cp += tagp->l;
                ohp->onwire.value.l += sizeof(";tag=") - 1 + tagp->l;
            }
            ohp->onwire.full.l = cp - ohp->onwire.full.s.ro;
            memcpy(cp, USIPY_CRLF, USIPY_CRLF_LEN);
            cp += USIPY_CRLF_LEN;
            copyfirst &= ~USIPY_HFT_MASK(shp->hf_type->cantype);
            continue;
        }
        if (USIPY_HF_ISMSET(copyall, shp->hf_type->cantype)) {
            struct usipy_sip_hdr *ohp = get_next_ohp(rp, &cnt);
            if (ohp == NULL)
                goto e0;
            ohp->hf_type = ohp->onwire.hf_type = shp->hf_type;
            memcpy(cp, shp->hf_type->name.s.ro, shp->hf_type->name.l);
            ohp->onwire.name.s.ro = ohp->onwire.full.s.ro = cp;
            ohp->onwire.name.l = shp->hf_type->name.l;
            cp += shp->hf_type->name.l;
            memcpy(cp, ": ", 2);
            cp += 2;
            memcpy(cp, shp->onwire.value.s.ro, shp->onwire.value.l);
            ohp->onwire.value.s.ro = cp;
            ohp->onwire.value.l = shp->onwire.value.l;
            cp += shp->onwire.value.l;
            ohp->onwire.full.l = cp - ohp->onwire.full.s.ro;
            memcpy(cp, USIPY_CRLF, USIPY_CRLF_LEN);
            cp += USIPY_CRLF_LEN;
        }
    }
    if (copyfirst != 0) {
        goto e0;
    }
    const struct append_hdr *ahdrs = res_tmpl.append_hdrs;
    for (int i = 0; ahdrs[i].value.l != 0; i++) {
        const struct append_hdr *ahp = &ahdrs[i];
        struct usipy_sip_hdr *ohp = get_next_ohp(rp, &cnt);
        if (ohp == NULL)
            goto e0;

        ohp->hf_type = ohp->onwire.hf_type = usipy_hdr_db_byid(ahp->type);
        memcpy(cp, ohp->hf_type->name.s.ro, ohp->hf_type->name.l);
        ohp->onwire.name.s.ro = ohp->onwire.full.s.ro = cp;
        ohp->onwire.name.l = ohp->hf_type->name.l;
        cp += ohp->hf_type->name.l;
        memcpy(cp, ": ", 2);
        cp += 2;
        memcpy(cp, ahp->value.s.ro, ahp->value.l);
        ohp->onwire.value.s.ro = cp;
        ohp->onwire.value.l = ahp->value.l;
        cp += ahp->value.l;
        ohp->onwire.full.l = cp - ohp->onwire.full.s.ro;
        memcpy(cp, USIPY_CRLF, USIPY_CRLF_LEN);
        cp += USIPY_CRLF_LEN;
    }

    memcpy(cp, USIPY_CRLF, USIPY_CRLF_LEN);
    cp += USIPY_CRLF_LEN;
    rp->onwire.l = cp - rp->onwire.s.rw;
    if (compact) {
        usipy_sip_res_compact(rp, &mcnt, hp, raw_space);
    }

    return (rp);
e0:
    usipy_msg_heap_cnt_rollback(hp, &mcnt);
    return (NULL);
}

struct usipy_msg *
usipy_sip_res_ctor_fromreq(const struct usipy_msg *reqp,
  const struct usipy_sip_status *slp)
{
    struct usipy_msg_heap hp;
    struct usipy_msg *rp;
    void *bp;
    size_t raw_space, heap_space;
    const size_t tlen = usipy_sip_res_alloc_size_ctor(reqp, NULL, &raw_space,
      &heap_space);

    bp = malloc(tlen);
    if (bp == NULL) {
        return (NULL);
    }
    usipy_msg_heap_init(&hp, bp, tlen, NULL, 0);
    rp = usipy_sip_res_build_fromreq_tagged_sz(&hp, reqp, slp, NULL, raw_space,
      heap_space, 0);
    if (rp == NULL) {
        free(bp);
    }
    return (rp);
}

struct usipy_msg *
usipy_sip_res_build_fromreq_tagged(struct usipy_msg_heap *hp,
  const struct usipy_msg *reqp, const struct usipy_sip_status *slp,
  const struct usipy_str *tagp)
{
    size_t raw_space, heap_space;

    USIPY_DASSERT(hp != NULL);
    usipy_sip_res_alloc_size_build(reqp, tagp, &raw_space, &heap_space);
    return (usipy_sip_res_build_fromreq_tagged_sz(hp, reqp, slp, tagp, raw_space,
      heap_space, 1));
}
