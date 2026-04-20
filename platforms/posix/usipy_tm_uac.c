#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "public/usipy_platform.h"
#include "public/usipy_str.h"
#include "external/mackron_md5/md5.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_uri.h"
#include "usipy_tvpair.h"

#include "usipy_tm_uac.h"

static int
usipy_tm_uac_uint_from_str(const struct usipy_str *sp, unsigned int *outp)
{
    if (sp == NULL || outp == NULL) {
        return (-1);
    }
    return (usipy_str_atoui_range(sp, outp, 0, UINT32_MAX));
}

uint64_t
usipy_tm_uac_mono_ms(void)
{

    return (usipy_platform_mono_ms());
}

void
usipy_tm_uac_sleep_until_ms(uint64_t when_ms)
{

    usipy_platform_sleep_until_ms(when_ms);
}

int
usipy_tm_uac_register_reply_auth(struct usipy_sip_tm *tm, size_t tx_index,
  const struct usipy_msg *msg, const struct usipy_str *usernamep,
  const struct usipy_str *passwordp, const struct usipy_str *qopp,
  const struct usipy_sip_tm_extra_header *base_hdrsp, size_t nbase_hdrs)
{
    struct usipy_sip_tm_extra_header *hdrsp;
    const struct usipy_str *effective_qopp;
    const struct usipy_sip_hdr_auth *challengep = NULL;
    uint64_t parse_mask;
    uint8_t auth_hf_type;
    struct usipy_msg *cmsg = (struct usipy_msg *)msg;
    char auth_storage[1024];
    size_t auth_cpts[4];
    struct usipy_msg_heap auth_heap;
    int rval = USIPY_SIP_TM_ERR_INVAL;

    if (tm == NULL || msg == NULL || usernamep == NULL || passwordp == NULL ||
      (nbase_hdrs != 0 && base_hdrsp == NULL)) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    struct usipy_sip_hdr_match *hdr_matches = __builtin_alloca(
      USIPY_SIP_HDR_MATCH_SIZE(cmsg->nhdrs));

    hdr_matches->hdrslen = cmsg->nhdrs;

    switch (msg->sline.parsed.sl.status.code) {
    case 401:
        parse_mask = USIPY_HFT_MASK(USIPY_HF_WWWAUTHENTICATE);
        auth_hf_type = USIPY_HF_AUTHORIZATION;
        break;

    case 407:
        parse_mask = USIPY_HFT_MASK(USIPY_HF_PROXYAUTHENTICATE);
        auth_hf_type = USIPY_HF_PROXYAUTHORIZATION;
        break;

    default:
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (usipy_sip_msg_parse_hdrs_get(cmsg, parse_mask, 0, hdr_matches) != 0) {
        return (USIPY_SIP_TM_ERR_PARSE);
    }
    if (hdr_matches->nhdrs != 0) {
        challengep = hdr_matches->hdrsp[0]->parsed.auth;
    }
    if (challengep == NULL) {
        return (USIPY_SIP_TM_ERR_NOT_FOUND);
    }
    effective_qopp = (challengep->qop.l != 0 ? qopp : NULL);
    hdrsp = __builtin_alloca(sizeof(*hdrsp) * (nbase_hdrs + 1));
    if (nbase_hdrs != 0) {
        memcpy(hdrsp, base_hdrsp, sizeof(*hdrsp) * nbase_hdrs);
    }
    usipy_msg_heap_init(&auth_heap, auth_storage, sizeof(auth_storage),
      auth_cpts, sizeof(auth_cpts) / sizeof(auth_cpts[0]));
    rval = usipy_sip_tm_gen_authz_hf(tm, tx_index, auth_hf_type, &auth_heap,
      challengep, usernamep, passwordp, NULL, effective_qopp, &hdrsp[nbase_hdrs]);
    if (rval == USIPY_SIP_TM_OK) {
        rval = usipy_sip_tm_next_transaction(tm, tx_index, NULL, hdrsp, nbase_hdrs + 1);
    }
    return (rval);
}

int
usipy_tm_uac_extract_register_expires(const struct usipy_msg *msg,
  const struct usipy_str *usernamep, unsigned int *expiresp)
{
    struct usipy_msg *cmsg = (struct usipy_msg *)msg;
    unsigned int fallback = 0;
    int have_fallback = 0;

    if (msg == NULL || expiresp == NULL) {
        return (-1);
    }
    struct usipy_sip_hdr_match *contact_hdrs = __builtin_alloca(
      USIPY_SIP_HDR_MATCH_SIZE(cmsg->nhdrs));
    struct usipy_sip_hdr_match *expires_hdrs = __builtin_alloca(
      USIPY_SIP_HDR_MATCH_SIZE(1));

    *contact_hdrs = (struct usipy_sip_hdr_match){.hdrslen = cmsg->nhdrs};
    *expires_hdrs = (struct usipy_sip_hdr_match){.hdrslen = 1};

    if (usipy_sip_msg_parse_hdrs_get(cmsg, USIPY_HFT_MASK(USIPY_HF_CONTACT), 0,
      contact_hdrs) != 0) {
        return (-1);
    }
    if (USIPY_MSG_HDR_PRESENT(cmsg, USIPY_HF_EXPIRES) &&
      usipy_sip_msg_parse_hdrs_get(cmsg, USIPY_HFT_MASK(USIPY_HF_EXPIRES), 1,
        expires_hdrs) != 0) {
        return (-1);
    }
    if (expires_hdrs->nhdrs != 0 &&
      usipy_tm_uac_uint_from_str(expires_hdrs->hdrsp[0]->parsed.generic, &fallback) == 0) {
        have_fallback = 1;
    }
    for (size_t i = 0; i < contact_hdrs->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = contact_hdrs->hdrsp[i];
        const struct usipy_sip_hdr_nameaddr *nap = shp->parsed.contact;
        int user_match = (usernamep == NULL || usernamep->l == 0);
        struct usipy_sip_uri *urip = NULL;

        if (nap == NULL) {
            continue;
        }
        if (!user_match) {
            urip = usipy_sip_uri_parse(&cmsg->heap, &nap->addr_spec);
            if (urip != NULL && urip->user.l == usernamep->l &&
              memcmp(urip->user.s.ro, usernamep->s.ro, usernamep->l) == 0) {
                user_match = 1;
            }
        }
        for (int pi = 0; pi < nap->nparams; pi++) {
            unsigned int expires;
            const struct usipy_tvpair *pp = &nap->params[pi];

            if (pp->token.l != 7 || memcmp(pp->token.s.ro, "expires", 7) != 0) {
                continue;
            }
            if (usipy_tm_uac_uint_from_str(&pp->value, &expires) != 0) {
                continue;
            }
            if (user_match) {
                *expiresp = expires;
                return (0);
            }
            if (!have_fallback) {
                fallback = expires;
                have_fallback = 1;
            }
        }
    }
    if (!have_fallback) {
        return (-1);
    }
    *expiresp = fallback;
    return (0);
}

int
usipy_tm_uac_production_ids_init(struct usipy_tm_uac_production_ids *idsp)
{
    md5_context ctx;
    unsigned char seed[MD5_SIZE];
    unsigned char hash[MD5_SIZE];
    char hash_hex[MD5_SIZE_FORMATTED];

    if (idsp == NULL) {
        return (-1);
    }
    if (usipy_platform_random_fill(seed, sizeof(seed)) != 0) {
        return (-1);
    }
    md5_init(&ctx);
    md5_update(&ctx, seed, sizeof(seed));
    md5_finalize(&ctx, hash);
    md5_format(hash_hex, sizeof(hash_hex), hash);
    memcpy(idsp->branch_seed, hash_hex, USIPY_TM_UAC_ID_SEED_HEXLEN);
    idsp->branch_seed[USIPY_TM_UAC_ID_SEED_HEXLEN] = '\0';
    memcpy(idsp->local_tag, hash_hex + USIPY_TM_UAC_ID_SEED_HEXLEN,
      USIPY_TM_UAC_ID_SEED_HEXLEN);
    idsp->local_tag[USIPY_TM_UAC_ID_SEED_HEXLEN] = '\0';
    memcpy(idsp->call_id, hash_hex, sizeof(hash_hex));
    idsp->call_id_s = (struct usipy_str){
      .s.ro = idsp->call_id,
      .l = sizeof(hash_hex) - 1,
    };
    return (0);
}

int
usipy_tm_uac_production_id_policy(void *arg, struct usipy_msg_heap *mhp,
  const struct usipy_sip_tm_id_policy_in *inp,
  struct usipy_sip_tm_id_policy_out *outp)
{
    const struct usipy_tm_uac_production_ids *idsp = arg;

    if (idsp == NULL || mhp == NULL || inp == NULL || outp == NULL) {
        return (-1);
    }
    if (usipy_msg_heap_sprintf(mhp, &outp->branch, "z9hG4bK-%.*s",
      USIPY_TM_UAC_ID_SEED_HEXLEN, idsp->branch_seed) != 0) {
        return (-1);
    }
    outp->call_id = idsp->call_id_s;
    if (usipy_msg_heap_append(mhp, &outp->local_tag, &(struct usipy_str){
      .s.ro = idsp->local_tag,
      .l = USIPY_TM_UAC_ID_SEED_HEXLEN,
    }) != 0) {
        return (-1);
    }
    return (0);
}
