#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "usipy_debug.h"
#include "usipy_types.h"
#include "public/usipy_platform.h"
#include "public/usipy_str.h"
#include "public/usipy_sip_sline.h"
#include "public/usipy_msg_heap.h"
#include "usipy_msg_heap_rb.h"
#include "usipy_msg_heap_inl.h"
#include "usipy_append_priv.h"
#include "public/usipy_sip_msg.h"
#include "usipy_sip_hdr.h"
#include "public/usipy_sip_hdr_types.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_msg_priv.h"
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

struct usipy_sip_res_build_arg {
    struct usipy_msg *rp;
    const struct usipy_msg *reqp;
    const struct usipy_sip_status *slp;
    const struct usipy_str *tagp;
    struct usipy_sip_hdr *hdrsp;
    size_t hdr_cap;
};

static const struct {
    uint64_t copyfirst;
    uint64_t copyall;
} res_tmpl = {
    .copyfirst = USIPY_HFT_MASK(USIPY_HF_FROM) | USIPY_HFT_MASK(USIPY_HF_CALLID) | \
      USIPY_HFT_MASK(USIPY_HF_TO) | USIPY_HFT_MASK(USIPY_HF_CSEQ),
    .copyall = USIPY_HFT_MASK(USIPY_HF_VIA) | USIPY_HFT_MASK(USIPY_HF_RECORDROUTE),
};

#define USIPY_SIP_RES_NAPPEND_HDRS_MAX (2)

static size_t
usipy_sip_res_init_append_hdrs(struct append_hdr *hdrsp)
{
    const struct usipy_str *serverp = USIPY_PLATFORM.get_server();
    size_t nhdrs = 0;

    if (serverp->l != 0) {
        hdrsp[nhdrs++] = (struct append_hdr){
          .type = USIPY_HF_SERVER,
          .value = *serverp,
        };
    }
    hdrsp[nhdrs++] = (struct append_hdr){
      .type = USIPY_HF_CONTENTLENGTH,
      .value = USIPY_2STR("0"),
    };
    return (nhdrs);
}

static size_t
usipy_sip_res_to_tag_extra(const struct usipy_str *tagp)
{
    return (tagp != NULL && tagp->l != 0 ? sizeof(";tag=") - 1 + tagp->l : 0);
}

static size_t
usipy_sip_res_append_raw_size(void)
{
    struct append_hdr append_hdrs[2];
    const size_t nhdrs = usipy_sip_res_init_append_hdrs(append_hdrs);
    size_t raw_len;

    raw_len = USIPY_CRLF_LEN;
    for (size_t i = 0; i < nhdrs; i++) {
        const struct append_hdr *ahp = &append_hdrs[i];
        const struct usipy_hdr_db_entr *hfp = usipy_hdr_db_byid(ahp->type);

        USIPY_DASSERT(hfp != NULL);
        raw_len += hfp->name.l + sizeof(": ") - 1 + ahp->value.l + USIPY_CRLF_LEN;
    }
    return (raw_len);
}

static size_t
usipy_sip_res_hdr_count(const struct usipy_msg *reqp)
{
    size_t nhdrs;

    USIPY_DASSERT(reqp != NULL);

    nhdrs = (size_t)reqp->nhdrs + USIPY_SIP_RES_NAPPEND_HDRS_MAX;
    if (nhdrs > USIPY_SIP_MSG_NHDRS_HINT) {
        nhdrs = USIPY_SIP_MSG_NHDRS_HINT;
    }
    return (nhdrs);
}

static size_t
usipy_sip_res_alloc_size_build(const struct usipy_msg *reqp,
  const struct usipy_str *tagp)
{
    size_t raw_len;
    size_t hdr_space;
    size_t heap_extra;

    USIPY_DASSERT(reqp != NULL);

    raw_len = reqp->onwire.l + usipy_sip_res_append_raw_size() +
      usipy_sip_res_to_tag_extra(tagp);
    heap_extra = usipy_sip_msg_extra_heap_size(raw_len);
    hdr_space = sizeof(struct usipy_sip_hdr) * usipy_sip_res_hdr_count(reqp);
    return (sizeof(struct usipy_msg) + USIPY_ALIGNED_SIZE(raw_len) +
      USIPY_ALIGNED_SIZE(hdr_space) + heap_extra);
}

static struct usipy_sip_hdr *
usipy_sip_res_get_next_ohp(struct usipy_sip_hdr *hdrsp, unsigned int *nhdrsp,
  size_t hdr_cap)
{
    USIPY_DASSERT(hdrsp != NULL);
    USIPY_DASSERT(nhdrsp != NULL);

    if ((size_t)(*nhdrsp) >= hdr_cap) {
        return (NULL);
    }
    memset(&hdrsp[*nhdrsp], '\0', sizeof(hdrsp[0]));
    return (&hdrsp[(*nhdrsp)++]);
}

static int
usipy_sip_res_append_hdr(char *buf, size_t len, size_t *offp,
  struct usipy_sip_hdr *ohp, const struct usipy_hdr_db_entr *hfp,
  const struct usipy_str *valuep, const char *extra_prefix,
  size_t extra_prefix_len, const struct usipy_str *extrap)
{
    size_t off = *offp;

#define APPEND_MEM(bp, blen) USIPY_APPEND_MEM_OR_GOTO(e0, buf, off, len, bp, blen)
#define APPEND_STR(sp) USIPY_APPEND_STR_OR_GOTO(e0, buf, off, len, sp)

    USIPY_DASSERT(buf != NULL);
    USIPY_DASSERT(offp != NULL);
    USIPY_DASSERT(ohp != NULL);
    USIPY_DASSERT(hfp != NULL);
    USIPY_DASSERT(valuep != NULL);

    ohp->hf_type = ohp->onwire.hf_type = hfp;
    ohp->onwire.name.s.ro = ohp->onwire.full.s.ro = buf + off;
    APPEND_STR(&hfp->name);
    ohp->onwire.name.l = hfp->name.l;
    APPEND_MEM(": ", 2);
    ohp->onwire.value.s.ro = buf + off;
    APPEND_STR(valuep);
    ohp->onwire.value.l = valuep->l;
    if (extra_prefix_len != 0) {
        APPEND_MEM(extra_prefix, extra_prefix_len);
        ohp->onwire.value.l += extra_prefix_len;
    }
    if (extrap != NULL && extrap->l != 0) {
        APPEND_STR(extrap);
        ohp->onwire.value.l += extrap->l;
    }
    ohp->onwire.full.l = off - (size_t)(ohp->onwire.full.s.ro - buf);
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
    *offp = off;
#undef APPEND_MEM
#undef APPEND_STR
    return (0);
e0:
#undef APPEND_MEM
#undef APPEND_STR
    return (-1);
}

static int
usipy_sip_res_append_next_hdr(const struct usipy_sip_res_build_arg *barg,
  char *buf, size_t len, size_t *offp, const struct usipy_hdr_db_entr *hfp,
  const struct usipy_str *valuep, const char *extra_prefix,
  size_t extra_prefix_len, const struct usipy_str *extrap)
{
    struct usipy_sip_hdr *ohp;

    USIPY_DASSERT(barg != NULL);
    USIPY_DASSERT(barg->rp != NULL);
    USIPY_DASSERT(offp != NULL);
    USIPY_DASSERT(hfp != NULL);
    ohp = usipy_sip_res_get_next_ohp(barg->hdrsp, &barg->rp->nhdrs,
      barg->hdr_cap);
    if (ohp == NULL) {
        return (-1);
    }
    return (usipy_sip_res_append_hdr(buf, len, offp, ohp, hfp, valuep,
      extra_prefix, extra_prefix_len, extrap));
}

static int
usipy_sip_res_build_fromreq_tagged_sz(void *arg, char *buf, size_t len)
{
    const struct usipy_sip_res_build_arg *barg = arg;
    const struct usipy_msg *reqp = barg->reqp;
    const struct usipy_sip_status *slp = barg->slp;
    const struct usipy_str *tagp = barg->tagp;
    struct usipy_msg *rp = barg->rp;
    const struct usipy_sip_request_line *rlin = &(reqp->sline.parsed.rl);
    uint64_t copyfirst = res_tmpl.copyfirst;
    uint64_t copyall = res_tmpl.copyall;
    struct usipy_sip_status_line *slout = &(rp->sline.parsed.sl);
    const struct usipy_str *vp;
    size_t off = 0;

#define APPEND_MEM(bp, blen) USIPY_APPEND_MEM_OR_RETURN(-1, buf, off, len, bp, blen)
#define APPEND_STR(sp) USIPY_APPEND_STR_OR_RETURN(-1, buf, off, len, sp)

    rp->nhdrs = 0;
    vp = &rlin->version;
    if (vp->l == 0) {
        vp = &rlin->onwire.version;
    }
    slout->version.s.ro = buf + off;
    APPEND_STR(vp);
    slout->version.l = vp->l;
    APPEND_MEM(" ", 1);
    slout->status.code = slp->code;
    scode2str(slp->code, buf + off);
    off += 4;
    slout->status.reason_phrase.s.ro = buf + off;
    APPEND_STR(&slp->reason_phrase);
    slout->status.reason_phrase.l = slp->reason_phrase.l;
    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);

    for (int i = 0; i < reqp->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = &reqp->hdrs[i];

        if (USIPY_HF_ISMSET(copyfirst, shp->hf_type->cantype)) {
            if (usipy_sip_res_append_next_hdr(barg, buf, len, &off,
              shp->hf_type, &shp->onwire.value,
              shp->hf_type->cantype == USIPY_HF_TO && tagp != NULL &&
              tagp->l != 0 ? ";tag=" : NULL,
              shp->hf_type->cantype == USIPY_HF_TO && tagp != NULL &&
              tagp->l != 0 ? sizeof(";tag=") - 1 : 0,
              shp->hf_type->cantype == USIPY_HF_TO ? tagp : NULL) != 0) {
                return (-1);
            }
            copyfirst &= ~USIPY_HFT_MASK(shp->hf_type->cantype);
            continue;
        }
        if (USIPY_HF_ISMSET(copyall, shp->hf_type->cantype)) {
            if (usipy_sip_res_append_next_hdr(barg, buf, len, &off,
              shp->hf_type, &shp->onwire.value, NULL, 0, NULL) != 0) {
                return (-1);
            }
        }
    }
    if (copyfirst != 0) {
        return (-1);
    }
    {
        struct append_hdr ahdrs[2];
        const size_t nahdrs = usipy_sip_res_init_append_hdrs(ahdrs);

        for (size_t i = 0; i < nahdrs; i++) {
            const struct append_hdr *ahp = &ahdrs[i];
            const struct usipy_hdr_db_entr *hfp = usipy_hdr_db_byid(ahp->type);

            if (hfp == NULL) {
                return (-1);
            }
            if (usipy_sip_res_append_next_hdr(barg, buf, len, &off, hfp,
              &ahp->value, NULL, 0, NULL) != 0) {
                return (-1);
            }
        }
    }

    APPEND_MEM(USIPY_CRLF, USIPY_CRLF_LEN);
#undef APPEND_MEM
#undef APPEND_STR
    return ((int)off);
}

static int
usipy_sip_res_init_fromreq(struct usipy_msg *rp, size_t alloc_len,
  const struct usipy_msg *reqp, const struct usipy_sip_status *slp,
  const struct usipy_str *tagp)
{
    struct usipy_sip_res_build_arg barg = {
      .rp = rp,
      .reqp = reqp,
      .slp = slp,
      .tagp = tagp,
    };
    const size_t heap_size = alloc_len - sizeof(*rp);
    const size_t hdr_cap = usipy_sip_res_hdr_count(reqp);
    struct usipy_sip_hdr ohdrs[hdr_cap != 0 ? hdr_cap : 1];

    USIPY_DASSERT(rp != NULL);
    USIPY_DASSERT(reqp != NULL);
    USIPY_DASSERT(slp != NULL);

    memset(rp, '\0', alloc_len);
    rp->kind = USIPY_SIP_MSG_RES;
    usipy_msg_heap_init(&rp->heap, rp->_storage, heap_size, NULL, 0);
    barg.hdrsp = ohdrs;
    barg.hdr_cap = hdr_cap;
    if (usipy_msg_heap_build(&rp->heap, &rp->onwire, &barg,
      usipy_sip_res_build_fromreq_tagged_sz) != 0) {
        return (-1);
    }
    rp->hdrs = usipy_msg_heap_alloc(&rp->heap, sizeof(rp->hdrs[0]) * rp->nhdrs);
    if (rp->hdrs == NULL) {
        return (-1);
    }
    memcpy(rp->hdrs, ohdrs, sizeof(rp->hdrs[0]) * rp->nhdrs);
    rp->sline.onwire.s.ro = rp->onwire.s.ro;
    rp->sline.onwire.l = rp->sline.parsed.sl.version.l + 1 + 4 +
      rp->sline.parsed.sl.status.reason_phrase.l;
    return (0);
}

struct usipy_msg *
usipy_sip_res_ctor_fromreq(const struct usipy_msg *reqp,
  const struct usipy_sip_status *slp)
{
    struct usipy_msg *rp;
    const size_t tlen = usipy_sip_res_alloc_size_build(reqp, NULL);

    rp = malloc(tlen);
    if (rp == NULL) {
        return (NULL);
    }
    if (usipy_sip_res_init_fromreq(rp, tlen, reqp, slp, NULL) != 0) {
        free(rp);
        return (NULL);
    }
    return (rp);
}

struct usipy_msg *
usipy_sip_res_build_fromreq_tagged(struct usipy_msg_heap *hp,
  const struct usipy_msg *reqp, const struct usipy_sip_status *slp,
  const struct usipy_str *tagp)
{
    struct usipy_msg_heap_cnt cnt;
    struct usipy_msg *rp;
    const size_t tlen = usipy_sip_res_alloc_size_build(reqp, tagp);

    USIPY_DASSERT(hp != NULL);
    memset(&cnt, '\0', sizeof(cnt));
    rp = usipy_msg_heap_alloc_cnt(hp, tlen, &cnt);
    if (rp == NULL) {
        return (NULL);
    }
    if (usipy_sip_res_init_fromreq(rp, tlen, reqp, slp, tagp) != 0) {
        usipy_msg_heap_cnt_rollback(hp, &cnt);
        return (NULL);
    }
    return (rp);
}
