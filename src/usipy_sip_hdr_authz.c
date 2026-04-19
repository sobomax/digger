#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/time.h>
#include <strings.h>

#include "usipy_port/log.h"

#include "usipy_debug.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_str.h"
#include "external/mackron_md5/md5.h"
#include "usipy_misc.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_sip_hdr_authz.h"

static int
usipy_sip_hdr_authz_ci_equal(const struct usipy_str *sp, const char *cp)
{
    const size_t clen = strlen(cp);

    return (sp->l == clen && strncasecmp(sp->s.ro, cp, sp->l) == 0);
}

static int
usipy_sip_hdr_authz_bufcopy(struct usipy_str *dstp, char **dstpp,
  const struct usipy_str *srcp)
{
    char *dstp_s;

    USIPY_DASSERT(dstp != NULL);
    USIPY_DASSERT(dstpp != NULL);
    USIPY_DASSERT(srcp != NULL);
    if (srcp->l == 0) {
        *dstp = USIPY_STR_NULL;
        return (0);
    }
    dstp_s = *dstpp;
    memcpy(dstp_s, srcp->s.ro, srcp->l);
    dstp->s.ro = dstp_s;
    dstp->l = srcp->l;
    *dstpp += srcp->l;
    return (0);
}

static void
usipy_sip_hdr_authz_md5_hex(char out[MD5_SIZE_FORMATTED], size_t nparts,
  const struct usipy_str *partsp)
{
    static const struct usipy_str colon = USIPY_2STR(":");
    unsigned char hash[MD5_SIZE];
    md5_context ctx;

    USIPY_DASSERT(out != NULL);
    USIPY_DASSERT(nparts == 0 || partsp != NULL);
    md5_init(&ctx);
    for (size_t i = 0; i < nparts; i++) {
        if (i != 0) {
            md5_update(&ctx, colon.s.ro, colon.l);
        }
        if (partsp[i].l != 0) {
            md5_update(&ctx, partsp[i].s.ro, partsp[i].l);
        }
    }
    md5_finalize(&ctx, hash);
    md5_format(out, MD5_SIZE_FORMATTED, hash);
}

static int
usipy_sip_hdr_authz_supported_qop(const struct usipy_str *qoplistp,
  const struct usipy_str *qopp)
{
    struct usipy_str work, token;

    USIPY_DASSERT(qoplistp != NULL);
    USIPY_DASSERT(qopp != NULL);
    work = *qoplistp;
    while (work.l != 0) {
        if (usipy_str_split_elem(&work, ',', &token) != 0) {
            token = work;
            work = USIPY_STR_NULL;
        }
        usipy_str_trm_b(&token);
        usipy_str_trm_e(&token);
        if (token.l == qopp->l && strncasecmp(token.s.ro, qopp->s.ro, token.l) == 0) {
            return (1);
        }
    }
    return (0);
}

static int
usipy_sip_hdr_authz_gen_cnonce(char buf[9], struct usipy_str *dstp)
{
    struct timeval tv;
    uint32_t usec_now;

    USIPY_DASSERT(buf != NULL);
    USIPY_DASSERT(dstp != NULL);
    if (gettimeofday(&tv, NULL) != 0) {
        return (-1);
    }
    usec_now = (uint32_t)((uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec);
    if (snprintf(buf, 9, "%08x", usec_now) != 8) {
        return (-1);
    }
    dstp->s.ro = buf;
    dstp->l = 8;
    return (0);
}

static int
usipy_sip_hdr_authz_append_value(char *buf, size_t len, size_t *offp,
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
    } else {
        rval = snprintf(buf + *offp, len - *offp, "%.*s=%.*s",
          USIPY_SFMT(namep), USIPY_SFMT(valuep));
    }
    if (rval < 0 || (size_t)rval >= len - *offp) {
        return (-1);
    }
    *offp += (size_t)rval;
    return (0);
}

struct usipy_sip_hdr_authz *
gen_auth_hf(struct usipy_msg_heap *mhp, const struct usipy_sip_hdr_auth *challengep,
  const struct usipy_str *usernamep, const struct usipy_str *passwordp,
  const struct usipy_str *methodp, const struct usipy_str *urip,
  const struct usipy_str *bodyp, const struct usipy_str *qopp)
{
    static const struct usipy_str digest = USIPY_2STR("Digest");
    static const struct usipy_str nc = USIPY_2STR("00000001");
    struct usipy_sip_hdr_authz *azp;
    struct usipy_str parts[6], body = USIPY_STR_NULL, qop = USIPY_STR_NULL;
    struct usipy_str cnonce = USIPY_STR_NULL;
    char ha1[MD5_SIZE_FORMATTED], ha2[MD5_SIZE_FORMATTED];
    char response[MD5_SIZE_FORMATTED], cnonce_buf[9];
    char *bp;
    struct usipy_str ha1s, ha2s;
    size_t slen;

    USIPY_DASSERT(mhp != NULL);
    USIPY_DASSERT(challengep != NULL);
    USIPY_DASSERT(usernamep != NULL);
    USIPY_DASSERT(passwordp != NULL);
    USIPY_DASSERT(methodp != NULL);
    USIPY_DASSERT(urip != NULL);
    if (!usipy_sip_hdr_authz_ci_equal(&challengep->scheme, "Digest")) {
        return (NULL);
    }
    if (challengep->realm.l == 0 || challengep->nonce.l == 0) {
        return (NULL);
    }
    if (challengep->algorithm.l != 0 &&
      !usipy_sip_hdr_authz_ci_equal(&challengep->algorithm, "MD5") &&
      !usipy_sip_hdr_authz_ci_equal(&challengep->algorithm, "MD5-sess")) {
        return (NULL);
    }
    if (challengep->qop.l != 0) {
        if (qopp == NULL || qopp->l == 0 ||
          !usipy_sip_hdr_authz_supported_qop(&challengep->qop, qopp)) {
            return (NULL);
        }
        qop = *qopp;
    } else if (qopp != NULL && qopp->l != 0) {
        return (NULL);
    }
    if (bodyp != NULL) {
        body = *bodyp;
    }
    parts[0] = *usernamep;
    parts[1] = challengep->realm;
    parts[2] = *passwordp;
    usipy_sip_hdr_authz_md5_hex(ha1, 3, parts);
    if (qop.l != 0 ||
      (challengep->algorithm.l != 0 &&
      usipy_sip_hdr_authz_ci_equal(&challengep->algorithm, "MD5-sess"))) {
        if (usipy_sip_hdr_authz_gen_cnonce(cnonce_buf, &cnonce) != 0) {
            return (NULL);
        }
    }
    if (challengep->algorithm.l != 0 &&
      usipy_sip_hdr_authz_ci_equal(&challengep->algorithm, "MD5-sess")) {
        ha1s = (struct usipy_str){.s.ro = ha1, .l = MD5_SIZE_FORMATTED - 1};
        parts[0] = ha1s;
        parts[1] = challengep->nonce;
        parts[2] = cnonce;
        usipy_sip_hdr_authz_md5_hex(ha1, 3, parts);
    }
    if (qop.l != 0 && usipy_sip_hdr_authz_ci_equal(&qop, "auth-int")) {
        char bodyhash[MD5_SIZE_FORMATTED];
        struct usipy_str bodyhashs;

        parts[0] = body;
        usipy_sip_hdr_authz_md5_hex(bodyhash, 1, parts);
        bodyhashs = (struct usipy_str){.s.ro = bodyhash, .l = MD5_SIZE_FORMATTED - 1};
        parts[0] = *methodp;
        parts[1] = *urip;
        parts[2] = bodyhashs;
        usipy_sip_hdr_authz_md5_hex(ha2, 3, parts);
    } else {
        parts[0] = *methodp;
        parts[1] = *urip;
        usipy_sip_hdr_authz_md5_hex(ha2, 2, parts);
    }
    ha1s = (struct usipy_str){.s.ro = ha1, .l = MD5_SIZE_FORMATTED - 1};
    ha2s = (struct usipy_str){.s.ro = ha2, .l = MD5_SIZE_FORMATTED - 1};
    parts[0] = ha1s;
    parts[1] = challengep->nonce;
    if (qop.l != 0) {
        parts[2] = nc;
        parts[3] = cnonce;
        parts[4] = qop;
        parts[5] = ha2s;
        usipy_sip_hdr_authz_md5_hex(response, 6, parts);
    } else {
        parts[2] = ha2s;
        usipy_sip_hdr_authz_md5_hex(response, 3, parts);
    }
    slen = usernamep->l + challengep->realm.l + challengep->nonce.l + urip->l +
      MD5_SIZE_FORMATTED - 1;
    if (challengep->algorithm.l != 0) {
        slen += challengep->algorithm.l;
    }
    if (challengep->opaque.l != 0) {
        slen += challengep->opaque.l;
    }
    if (qop.l != 0) {
        slen += qop.l + nc.l + cnonce.l;
    }
    azp = usipy_msg_heap_alloc(mhp, sizeof(*azp) + slen);
    if (azp == NULL) {
        return (NULL);
    }
    memset(azp, '\0', sizeof(*azp));
    azp->scheme = digest;
    bp = (char *)(azp + 1);
    (void)usipy_sip_hdr_authz_bufcopy(&azp->username, &bp, usernamep);
    (void)usipy_sip_hdr_authz_bufcopy(&azp->realm, &bp, &challengep->realm);
    (void)usipy_sip_hdr_authz_bufcopy(&azp->nonce, &bp, &challengep->nonce);
    (void)usipy_sip_hdr_authz_bufcopy(&azp->uri, &bp, urip);
    parts[0] = (struct usipy_str){.s.ro = response, .l = MD5_SIZE_FORMATTED - 1};
    (void)usipy_sip_hdr_authz_bufcopy(&azp->response, &bp, &parts[0]);
    if (challengep->algorithm.l != 0) {
        (void)usipy_sip_hdr_authz_bufcopy(&azp->algorithm, &bp,
          &challengep->algorithm);
    }
    if (challengep->opaque.l != 0) {
        (void)usipy_sip_hdr_authz_bufcopy(&azp->opaque, &bp, &challengep->opaque);
    }
    if (qop.l != 0) {
        (void)usipy_sip_hdr_authz_bufcopy(&azp->qop, &bp, &qop);
        (void)usipy_sip_hdr_authz_bufcopy(&azp->nc, &bp, &nc);
        (void)usipy_sip_hdr_authz_bufcopy(&azp->cnonce, &bp, &cnonce);
    }
    return (azp);
}

int
usipy_sip_hdr_authz_build(const union usipy_sip_hdr_parsed *up, char *buf, size_t len)
{
    static const struct usipy_str username = USIPY_2STR("username");
    static const struct usipy_str realm = USIPY_2STR("realm");
    static const struct usipy_str nonce = USIPY_2STR("nonce");
    static const struct usipy_str uri = USIPY_2STR("uri");
    static const struct usipy_str response = USIPY_2STR("response");
    static const struct usipy_str algorithm = USIPY_2STR("algorithm");
    static const struct usipy_str qop = USIPY_2STR("qop");
    static const struct usipy_str nc = USIPY_2STR("nc");
    static const struct usipy_str cnonce = USIPY_2STR("cnonce");
    static const struct usipy_str opaque = USIPY_2STR("opaque");
    static const struct usipy_str sp = USIPY_2STR(" ");
    const struct usipy_sip_hdr_authz *azp = up->authz;
    size_t off = 0, poff = 0;

    USIPY_DASSERT(azp != NULL);
    if (usipy_strbuf_append_pair(&azp->scheme, &sp, buf, len, &off) != 0) {
        return (-1);
    }
    if (usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &username, &azp->username, 1) != 0 ||
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &realm, &azp->realm, 1) != 0 ||
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &nonce, &azp->nonce, 1) != 0 ||
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &uri, &azp->uri, 1) != 0 ||
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &response, &azp->response, 1) != 0) {
        return (-1);
    }
    if (azp->algorithm.l != 0 &&
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &algorithm, &azp->algorithm, 0) != 0) {
        return (-1);
    }
    if (azp->qop.l != 0 &&
      (usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &qop, &azp->qop, 0) != 0 ||
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &nc, &azp->nc, 0) != 0 ||
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &cnonce, &azp->cnonce, 1) != 0)) {
        return (-1);
    }
    if (azp->opaque.l != 0 &&
      usipy_sip_hdr_authz_append_value(buf + off, len - off, &poff,
      &opaque, &azp->opaque, 1) != 0) {
        return (-1);
    }
    off += poff;
    return ((int)off);
}

void
usipy_sip_hdr_authz_dump(const union usipy_sip_hdr_parsed *up, const char *log_tag,
  const char *log_pref, const char *canname)
{
    const struct usipy_sip_hdr_authz *azp = up->authz;

    DUMP_STR(&azp, scheme, canname);
    DUMP_STR(&azp, username, canname);
    DUMP_STR(&azp, realm, canname);
    DUMP_STR(&azp, nonce, canname);
    DUMP_STR(&azp, uri, canname);
    DUMP_STR(&azp, response, canname);
    DUMP_STR(&azp, qop, canname);
    DUMP_STR(&azp, nc, canname);
    DUMP_STR(&azp, cnonce, canname);
    DUMP_STR(&azp, algorithm, canname);
    DUMP_STR(&azp, opaque, canname);
}
