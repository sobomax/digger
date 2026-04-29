#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "public/microsippy.h"
#include "external/mackron_md5/md5.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_auth.h"
#include "usipy_sip_hdr_authz.h"
#include "usipy_sip_hdr_cseq.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_hdr_via.h"
#include "usipy_sip_method_db.h"
#include "usipy_sip_res.h"
#include "usipy_sip_tid.h"
#include "usipy_tvpair.h"
#include "usipy_tm_uac.h"

struct test_cbarg {
    size_t nsent;
    size_t nresponses;
    size_t ntimeouts;
    uint16_t last_status_code;
    uint64_t start_ms;
    struct usipy_sip_tm *tm;
    struct usipy_str username;
    struct usipy_str password;
    struct usipy_str qop;
    struct usipy_str extra_hdr_values[2];
    struct usipy_sip_tm_extra_header extra_hdrs[2];
    int auth_retry_started;
    int stop;
};

struct invite_cbarg {
    size_t nresponses;
    size_t ntimeouts;
    uint64_t now_ms;
    uint16_t status_codes[8];
    const char *scenario;
    enum usipy_sip_tm_uac_timeout_id timeout_ids[4];
};

struct invite_send_arg {
    size_t nsent;
    uint64_t now_ms;
    size_t tx_indexes[8];
    const char *scenario;
};

struct uas_cbarg {
    struct usipy_sip_tm *tm;
    size_t nrequests;
    size_t ncancels;
    size_t nnoacks;
    size_t tx_index;
    const char *scenario;
    struct usipy_sip_status response_status;
    struct usipy_sip_status cancel_response_status;
    struct usipy_sip_tm_uas_callbacks response_callbacks;
    struct usipy_sip_tm_uas_callbacks cancel_response_callbacks;
    struct usipy_sip_tm_uas_response_params response;
    struct usipy_sip_tm_uas_response_params cancel_response;
};

static const struct usipy_str *find_nameaddr_param(
  const struct usipy_sip_hdr_nameaddr *nap, const char *name);
static const struct usipy_sip_hdr *find_header(const struct usipy_msg *msg,
  uint8_t hf_type);
static void assert_tx_target(const struct usipy_sip_tm_tx *txp, const char *host,
  unsigned int port);

static void
dump_onwire(const char *scenario, const char *tag, uint64_t now_ms, size_t tx_index,
  const struct usipy_str *rawp)
{
    assert(tag != NULL);
    assert(rawp != NULL);

    if (fprintf(stdout, "== %s ==\n[%s][%llu ms][tx %zu]\n",
      scenario != NULL ? scenario : "UAC",
      tag, (unsigned long long)now_ms, tx_index) < 0) {
        assert(0);
    }
    assert(fwrite(rawp->s.ro, 1, rawp->l, stdout) == rawp->l);
}

static int
stdout_send_to(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_sip_tm_outbound *outp)
{
    struct test_cbarg *carg = arg;
    const uint64_t now_ms = usipy_tm_uac_mono_ms();

    (void)arg;
    (void)tx_index;
    (void)txp;
    carg->nsent += 1;
    if (fprintf(stdout, "[%llu ms]\n",
      (unsigned long long)(now_ms - carg->start_ms)) < 0) {
        return (-1);
    }
    return (fwrite(outp->raw.s.ro, 1, outp->raw.l, stdout) == outp->raw.l ? 0 : -1);
}

static void
uac_response(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
    struct test_cbarg *carg = arg;
    const struct usipy_msg *cmsg = msg;
    const struct usipy_sip_hdr_auth *ap = NULL;

    (void)tx_index;
    (void)txp;
    assert(msg != NULL);
    assert(msg->kind == USIPY_SIP_MSG_RES);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)cmsg,
      USIPY_HFT_MASK(USIPY_HF_WWWAUTHENTICATE), 0) == 0);
    for (unsigned int i = 0; i < cmsg->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = &cmsg->hdrs[i];

        if (shp->hf_type->cantype != USIPY_HF_WWWAUTHENTICATE) {
            continue;
        }
        ap = shp->parsed.auth;
        break;
    }
    assert(ap != NULL);
    assert(ap->scheme.l == 6 && memcmp(ap->scheme.s.ro, "Digest", 6) == 0);
    assert(ap->realm.l == 12 && memcmp(ap->realm.s.ro, "example.test", 12) == 0);
    assert(ap->nonce.l == 6 && memcmp(ap->nonce.s.ro, "abcdef", 6) == 0);
    assert(ap->algorithm.l == 3 && memcmp(ap->algorithm.s.ro, "MD5", 3) == 0);
    assert(ap->qop.l == 4 && memcmp(ap->qop.s.ro, "auth", 4) == 0);
    carg->nresponses += 1;
    carg->last_status_code = msg->sline.parsed.sl.status.code;
    if (msg->sline.parsed.sl.status.code == 401 && txp->common.id.cseq == 2 &&
      !carg->auth_retry_started) {
        assert(usipy_tm_uac_register_reply_auth(carg->tm, tx_index, msg,
          &carg->username, &carg->password, &carg->qop, &carg->extra_hdrs[0],
          1) == USIPY_SIP_TM_OK);
        carg->auth_retry_started = 1;
    }
}

static void
md5_hex_join(char out[MD5_SIZE_FORMATTED], size_t nparts,
  const struct usipy_str *partsp)
{
    unsigned char hash[MD5_SIZE];
    md5_context ctx;

    md5_init(&ctx);
    for (size_t i = 0; i < nparts; i++) {
        if (i != 0) {
            md5_update(&ctx, ":", 1);
        }
        if (partsp[i].l != 0) {
            md5_update(&ctx, partsp[i].s.ro, partsp[i].l);
        }
    }
    md5_finalize(&ctx, hash);
    md5_format(out, MD5_SIZE_FORMATTED, hash);
}

static void
test_gen_auth_hf(void)
{
    char chall_storage[1024], auth_storage[1024], buf[1024], expbuf[1024];
    char ha1[MD5_SIZE_FORMATTED], ha2[MD5_SIZE_FORMATTED];
    char response[MD5_SIZE_FORMATTED];
    size_t chall_cpts[4], auth_cpts[4];
    struct usipy_msg_heap chall_heap, auth_heap;
    const struct usipy_str challenge_noqop = USIPY_2STR(
      "Digest realm=\"testrealm@host.com\",nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\",opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"");
    const struct usipy_str challenge_qop = USIPY_2STR(
      "Digest realm=\"testrealm@host.com\",nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\",qop=auth,algorithm=MD5,opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"");
    const struct usipy_str username = USIPY_2STR("Mufasa");
    const struct usipy_str password = USIPY_2STR("Circle Of Life");
    const struct usipy_str method = USIPY_2STR("GET");
    const struct usipy_str uri = USIPY_2STR("/dir/index.html");
    const struct usipy_str qop = USIPY_2STR("auth");
    union usipy_sip_hdr_parsed up;
    struct usipy_sip_hdr_auth *challengep;
    struct usipy_sip_hdr_authz *authzp;
    const struct usipy_hdr_db_entr *hfp;
    struct usipy_str parts[6], ha1s, ha2s;
    int blen;

    usipy_msg_heap_init(&chall_heap, chall_storage, sizeof(chall_storage),
      chall_cpts, sizeof(chall_cpts) / sizeof(chall_cpts[0]));
    usipy_msg_heap_init(&auth_heap, auth_storage, sizeof(auth_storage),
      auth_cpts, sizeof(auth_cpts) / sizeof(auth_cpts[0]));
    challengep = usipy_sip_hdr_auth_parse(&chall_heap, &challenge_noqop).auth;
    assert(challengep != NULL);
    authzp = gen_auth_hf(&auth_heap, challengep, &username, &password, &method,
      &uri, NULL, NULL);
    assert(authzp != NULL);
    hfp = usipy_hdr_db_byid(USIPY_HF_AUTHORIZATION);
    assert(hfp->build != NULL);
    up.authz = authzp;
    blen = hfp->build(&up, buf, sizeof(buf));
    assert(blen > 0);
    assert((size_t)blen == sizeof("Digest username=\"Mufasa\",realm=\"testrealm@host.com\",nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\",uri=\"/dir/index.html\",response=\"670fd8c2df070c60b045671b8b24ff02\",opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"") - 1);
    assert(memcmp(buf,
      "Digest username=\"Mufasa\",realm=\"testrealm@host.com\",nonce=\"dcd98b7102dd2f0e8b11d0f600bfb0c093\",uri=\"/dir/index.html\",response=\"670fd8c2df070c60b045671b8b24ff02\",opaque=\"5ccc069c403ebaf9f0171e9517f40e41\"",
      (size_t)blen) == 0);

    usipy_msg_heap_init(&auth_heap, auth_storage, sizeof(auth_storage),
      auth_cpts, sizeof(auth_cpts) / sizeof(auth_cpts[0]));
    challengep = usipy_sip_hdr_auth_parse(&chall_heap, &challenge_qop).auth;
    assert(challengep != NULL);
    authzp = gen_auth_hf(&auth_heap, challengep, &username, &password, &method,
      &uri, NULL, &qop);
    assert(authzp != NULL);
    assert(authzp->cnonce.l == 8);
    up.authz = authzp;
    blen = hfp->build(&up, buf, sizeof(buf));
    assert(blen > 0);
    parts[0] = username;
    parts[1] = challengep->realm;
    parts[2] = password;
    md5_hex_join(ha1, 3, parts);
    parts[0] = method;
    parts[1] = uri;
    md5_hex_join(ha2, 2, parts);
    ha1s = (struct usipy_str){.s.ro = ha1, .l = MD5_SIZE_FORMATTED - 1};
    ha2s = (struct usipy_str){.s.ro = ha2, .l = MD5_SIZE_FORMATTED - 1};
    parts[0] = ha1s;
    parts[1] = challengep->nonce;
    parts[2] = authzp->nc;
    parts[3] = authzp->cnonce;
    parts[4] = qop;
    parts[5] = ha2s;
    md5_hex_join(response, 6, parts);
    assert(snprintf(expbuf, sizeof(expbuf),
      "Digest username=\"%.*s\",realm=\"%.*s\",nonce=\"%.*s\",uri=\"%.*s\",response=\"%s\",algorithm=%.*s,qop=%.*s,nc=%.*s,cnonce=\"%.*s\",opaque=\"%.*s\"",
      USIPY_SFMT(&username), USIPY_SFMT(&challengep->realm),
      USIPY_SFMT(&challengep->nonce), USIPY_SFMT(&uri), response,
      USIPY_SFMT(&challengep->algorithm), USIPY_SFMT(&qop),
      USIPY_SFMT(&authzp->nc), USIPY_SFMT(&authzp->cnonce),
      USIPY_SFMT(&challengep->opaque)) == blen);
    assert(memcmp(buf, expbuf, (size_t)blen) == 0);
}

static void
test_register_expires_helpers(void)
{
    static const char msgbuf[] =
      "SIP/2.0 200 OK\r\n"
      "Via: SIP/2.0/UDP 127.0.0.1;branch=z9hG4bK-a\r\n"
      "From: <sip:alice@example.test>;tag=f1\r\n"
      "To: <sip:alice@example.test>;tag=t1\r\n"
      "Call-ID: reg-1@example.test\r\n"
      "CSeq: 1 REGISTER\r\n"
      "Contact: <sip:alice@example.test>;expires=120\r\n"
      "Contact: <sip:bob@example.test>\r\n"
      "Expires: 300\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_sip_hdr_match *contact_hdrs;
    struct usipy_msg *msg;
    unsigned int expires = 0;
    const struct usipy_str alice = USIPY_2STR("alice");
    const struct usipy_str bob = USIPY_2STR("bob");
    const struct usipy_str carol = USIPY_2STR("carol");

    msg = usipy_sip_msg_ctor_fromwire(msgbuf, sizeof(msgbuf) - 1, &perr);
    assert(msg != NULL);
    contact_hdrs = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(8));
    contact_hdrs->hdrslen = 8;
    assert(usipy_sip_msg_parse_hdrs_get(msg, USIPY_HFT_MASK(USIPY_HF_CONTACT), 0,
      contact_hdrs) == 0);
    assert(contact_hdrs->nhdrs == 2);

    assert(usipy_tm_uac_extract_register_expires(msg, &alice, &expires, NULL) == 0);
    assert(expires == 120);

    assert(usipy_tm_uac_extract_register_expires(msg, &bob, &expires, NULL) == 0);
    assert(expires == 300);

    assert(usipy_tm_uac_extract_register_expires(msg, &carol, &expires, NULL) == 0);
    assert(expires == 300);

    usipy_sip_msg_dtor(msg);
}

static void
uac_timeout(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  enum usipy_sip_tm_uac_timeout_id timeout_id)
{
    struct test_cbarg *carg = arg;

    (void)tx_index;
    (void)timeout_id;
    carg->ntimeouts += 1;
    if (carg->auth_retry_started && txp->common.id.cseq == 3) {
        carg->stop = 1;
    }
}

static int
bind_loopback_udp(void)
{
    struct sockaddr_in sin;
    int sock;

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock >= 0);
    memset(&sin, '\0', sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(0);
    assert(inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr) == 1);
    if (bind(sock, (const struct sockaddr *)&sin, sizeof(sin)) != 0) {
        perror("bind_loopback_udp bind");
        assert(0);
    }
    return (sock);
}

static struct usipy_sip_hdr *
append_parsed_header(struct usipy_msg *mp, const struct usipy_hdr_db_entr *hfp)
{
    struct usipy_sip_hdr *nhdrs;
    const size_t nsize = sizeof(mp->hdrs[0]) * (mp->nhdrs + 1);

    assert(mp != NULL);
    assert(hfp != NULL);

    nhdrs = usipy_msg_heap_alloc(&mp->heap, nsize);
    assert(nhdrs != NULL);
    memcpy(nhdrs, mp->hdrs, sizeof(mp->hdrs[0]) * mp->nhdrs);
    mp->hdrs = nhdrs;
    memset(&mp->hdrs[mp->nhdrs], '\0', sizeof(mp->hdrs[0]));
    mp->hdrs[mp->nhdrs].hf_type = hfp;
    mp->hdr_masks.present |= USIPY_HFT_MASK(hfp->cantype);
    mp->nhdrs += 1;
    return (&mp->hdrs[mp->nhdrs - 1]);
}

static void
prepare_msg_for_build(struct usipy_msg *mp)
{
    uint64_t present_mask = 0;
    uint64_t parsed_mask = 0;

    assert(mp != NULL);
    for (unsigned int i = 0; i < mp->nhdrs; i++) {
        struct usipy_sip_hdr *shp = &mp->hdrs[i];

        present_mask |= USIPY_HFT_MASK(shp->hf_type->cantype);
        if (shp->hf_type->parse != NULL) {
            shp->parsed = shp->hf_type->parse(&mp->heap, &shp->onwire.value);
            assert(shp->parsed.generic != NULL);
            parsed_mask |= USIPY_HFT_MASK(shp->hf_type->cantype);
            continue;
        }
        shp->parsed.generic = &shp->onwire.value;
    }
    mp->hdr_masks.present |= present_mask;
    mp->hdr_masks.parsed |= parsed_mask;
}

static void
set_header_tag(struct usipy_msg *mp, int htype, const struct usipy_str *tagp)
{
    struct usipy_str hvalue;

    assert(mp != NULL);
    assert(tagp != NULL);
    for (unsigned int i = 0; i < mp->nhdrs; i++) {
        struct usipy_sip_hdr *shp = &mp->hdrs[i];

        if (shp->hf_type->cantype != htype) {
            continue;
        }
        if (shp->parsed.to == NULL) {
            shp->parsed.to = usipy_sip_hdr_nameaddr_parse(&mp->heap,
              &shp->onwire.value).to;
            assert(shp->parsed.to != NULL);
        }
        assert(usipy_msg_heap_sprintf(&mp->heap, &hvalue, "%.*s%.*s",
          USIPY_SFMT(&shp->onwire.value), USIPY_SFMT(tagp)) == 0);
        shp->parsed.to = usipy_sip_hdr_nameaddr_parse(&mp->heap, &hvalue).to;
        assert(shp->parsed.to != NULL);
        mp->hdr_masks.parsed |= USIPY_HFT_MASK(htype);
        return;
    }
    assert(0);
}

static void
append_www_authenticate(struct usipy_msg *mp, const struct usipy_str *valuep)
{
    struct usipy_sip_hdr *shp;

    assert(mp != NULL);
    assert(valuep != NULL);
    shp = append_parsed_header(mp, usipy_hdr_db_byid(USIPY_HF_WWWAUTHENTICATE));
    shp->parsed.auth = usipy_sip_hdr_auth_parse(&mp->heap, valuep).auth;
    assert(shp->parsed.auth != NULL);
    mp->hdr_masks.parsed |= USIPY_HFT_MASK(USIPY_HF_WWWAUTHENTICATE);
}

static struct usipy_msg *
build_unauth_response(const struct usipy_msg *reqp, const struct usipy_sip_status *slp,
  const struct usipy_str *to_tagp, const struct usipy_str *www_authp)
{
    struct usipy_msg *rp;
    struct usipy_str onwire;

    assert(reqp != NULL);
    assert(slp != NULL);
    assert(to_tagp != NULL);
    assert(www_authp != NULL);
    rp = usipy_sip_res_ctor_fromreq(reqp, slp);
    assert(rp != NULL);
    prepare_msg_for_build(rp);
    set_header_tag(rp, USIPY_HF_TO, to_tagp);
    append_www_authenticate(rp, www_authp);
    assert(usipy_sip_msg_build(&rp->heap, rp, &onwire) == 0);
    return (rp);
}

static struct usipy_msg *
build_basic_response(const struct usipy_msg *reqp, const struct usipy_sip_status *slp,
  const struct usipy_str *to_tagp)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_str *via = NULL, *from = NULL, *to = NULL, *callid = NULL, *cseq = NULL;
    char rawbuf[1024];
    int blen;

    assert(reqp != NULL);
    assert(slp != NULL);
    assert(to_tagp != NULL);
    for (unsigned int i = 0; i < reqp->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = &reqp->hdrs[i];

        switch (shp->hf_type->cantype) {
        case USIPY_HF_VIA:
            if (via == NULL) {
                via = &shp->onwire.value;
            }
            break;
        case USIPY_HF_FROM:
            if (from == NULL) {
                from = &shp->onwire.value;
            }
            break;
        case USIPY_HF_TO:
            if (to == NULL) {
                to = &shp->onwire.value;
            }
            break;
        case USIPY_HF_CALLID:
            if (callid == NULL) {
                callid = &shp->onwire.value;
            }
            break;
        case USIPY_HF_CSEQ:
            if (cseq == NULL) {
                cseq = &shp->onwire.value;
            }
            break;
        default:
            break;
        }
    }
    assert(via != NULL && from != NULL && to != NULL && callid != NULL && cseq != NULL);
    blen = snprintf(rawbuf, sizeof(rawbuf),
      "SIP/2.0 %u %.*s\r\n"
      "Via: %.*s\r\n"
      "From: %.*s\r\n"
      "To: %.*s%.*s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %.*s\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      slp->code, USIPY_SFMT(&slp->reason_phrase), USIPY_SFMT(via), USIPY_SFMT(from),
      USIPY_SFMT(to), USIPY_SFMT(to_tagp), USIPY_SFMT(callid), USIPY_SFMT(cseq));
    assert(blen > 0 && (size_t)blen < sizeof(rawbuf));
    return (usipy_sip_msg_ctor_fromwire(rawbuf, (size_t)blen, &perr));
}

static struct usipy_msg *
build_trying_response(const struct usipy_msg *reqp)
{
    const struct usipy_str no_tag = USIPY_STR_NULL;

    return (build_basic_response(reqp, &usipy_sip_res_trying, &no_tag));
}

static int
build_response_with_contact_raw(const struct usipy_msg *reqp,
  const struct usipy_sip_status *slp, const struct usipy_str *to_tagp,
  const struct usipy_str *contactp, char *rawbuf, size_t rawbuf_len)
{
    const struct usipy_str *via = NULL, *from = NULL, *to = NULL, *callid = NULL, *cseq = NULL;
    int blen;

    assert(reqp != NULL);
    assert(slp != NULL);
    assert(to_tagp != NULL);
    assert(contactp != NULL);
    assert(rawbuf != NULL);
    for (unsigned int i = 0; i < reqp->nhdrs; i++) {
        const struct usipy_sip_hdr *shp = &reqp->hdrs[i];

        switch (shp->hf_type->cantype) {
        case USIPY_HF_VIA:
            if (via == NULL) {
                via = &shp->onwire.value;
            }
            break;
        case USIPY_HF_FROM:
            if (from == NULL) {
                from = &shp->onwire.value;
            }
            break;
        case USIPY_HF_TO:
            if (to == NULL) {
                to = &shp->onwire.value;
            }
            break;
        case USIPY_HF_CALLID:
            if (callid == NULL) {
                callid = &shp->onwire.value;
            }
            break;
        case USIPY_HF_CSEQ:
            if (cseq == NULL) {
                cseq = &shp->onwire.value;
            }
            break;
        default:
            break;
        }
    }
    assert(via != NULL && from != NULL && to != NULL && callid != NULL && cseq != NULL);
    blen = snprintf(rawbuf, rawbuf_len,
      "SIP/2.0 %u %.*s\r\n"
      "Via: %.*s\r\n"
      "From: %.*s\r\n"
      "To: %.*s%.*s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %.*s\r\n"
      "Contact: <%.*s>\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      slp->code, USIPY_SFMT(&slp->reason_phrase), USIPY_SFMT(via), USIPY_SFMT(from),
      USIPY_SFMT(to), USIPY_SFMT(to_tagp), USIPY_SFMT(callid), USIPY_SFMT(cseq),
      USIPY_SFMT(contactp));
    assert(blen > 0 && (size_t)blen < rawbuf_len);
    return (blen);
}

static struct usipy_msg *
build_response_with_contact(const struct usipy_msg *reqp,
  const struct usipy_sip_status *slp, const struct usipy_str *to_tagp,
  const struct usipy_str *contactp)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    char rawbuf[1024];
    int blen;

    blen = build_response_with_contact_raw(reqp, slp, to_tagp, contactp, rawbuf,
      sizeof(rawbuf));
    return (usipy_sip_msg_ctor_fromwire(rawbuf, (size_t)blen, &perr));
}

static int
build_response_with_contact_routes_raw(const struct usipy_msg *reqp,
  const struct usipy_sip_status *slp, const struct usipy_str *to_tagp,
  const struct usipy_str *contactp, const struct usipy_str *routep, size_t nroutes,
  char *rawbuf, size_t rawbuf_len)
{
    int blen;

    blen = build_response_with_contact_raw(reqp, slp, to_tagp, contactp, rawbuf,
      rawbuf_len);
    assert(blen > 0);
    blen -= (int)USIPY_CRLF_LEN;
    for (size_t i = 0; i < nroutes; i++) {
        const int hlen = snprintf(rawbuf + blen, rawbuf_len - (size_t)blen,
          "Record-Route: <%.*s>\r\n", USIPY_SFMT(&routep[i]));

        assert(hlen > 0);
        assert((size_t)hlen < rawbuf_len - (size_t)blen);
        blen += hlen;
    }
    assert((size_t)blen + USIPY_CRLF_LEN < rawbuf_len);
    memcpy(rawbuf + blen, "\r\n", USIPY_CRLF_LEN + 1);
    blen += (int)USIPY_CRLF_LEN;
    return (blen);
}

static struct usipy_msg *
build_response_with_contact_routes(const struct usipy_msg *reqp,
  const struct usipy_sip_status *slp, const struct usipy_str *to_tagp,
  const struct usipy_str *contactp, const struct usipy_str *routep, size_t nroutes)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    char rawbuf[1024];
    int blen;

    blen = build_response_with_contact_routes_raw(reqp, slp, to_tagp, contactp,
      routep, nroutes, rawbuf, sizeof(rawbuf));
    return (usipy_sip_msg_ctor_fromwire(rawbuf, (size_t)blen, &perr));
}

static int
invite_send_to(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_sip_tm_outbound *outp)
{
    struct invite_send_arg *sarg = arg;

    (void)txp;
    assert(sarg->nsent < sizeof(sarg->tx_indexes) / sizeof(sarg->tx_indexes[0]));
    sarg->tx_indexes[sarg->nsent++] = tx_index;
    dump_onwire(sarg->scenario, "send", sarg->now_ms, tx_index, &outp->raw);
    return (0);
}

static void
invite_response(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
    struct invite_cbarg *carg = arg;

    (void)tx_index;
    (void)txp;
    assert(msg != NULL);
    assert(msg->kind == USIPY_SIP_MSG_RES);
    assert(carg->nresponses < sizeof(carg->status_codes) / sizeof(carg->status_codes[0]));
    dump_onwire(carg->scenario, "response-cb", carg->now_ms, tx_index, &msg->onwire);
    carg->status_codes[carg->nresponses++] = msg->sline.parsed.sl.status.code;
}

static void
invite_timeout(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  enum usipy_sip_tm_uac_timeout_id timeout_id)
{
    struct invite_cbarg *carg = arg;

    (void)tx_index;
    (void)txp;
    assert(carg->ntimeouts < sizeof(carg->timeout_ids) / sizeof(carg->timeout_ids[0]));
    assert(fprintf(stdout, "== %s ==\n[timeout][%llu ms][tx %zu] id=%u\n",
      carg->scenario != NULL ? carg->scenario : "UAC",
      (unsigned long long)carg->now_ms, tx_index, (unsigned int)timeout_id) > 0);
    carg->timeout_ids[carg->ntimeouts++] = timeout_id;
}

static void
invite_print_banner(const char *scenario)
{
    assert(scenario != NULL);
    assert(fprintf(stdout, "\n-- %s --\n", scenario) > 0);
}

static void
invite_run_step(struct invite_send_arg *sarg, struct usipy_sip_tm_run_in *rin,
  struct usipy_sip_tm_run_out *rout, uint64_t now_ms)
{
    assert(sarg != NULL);
    assert(rin != NULL);

    sarg->now_ms = now_ms;
    rin->now_ms = now_ms;
    assert(usipy_sip_tm_run(rin, rout) == USIPY_SIP_TM_OK);
}

static void
invite_handle_step(struct invite_cbarg *carg,
  struct usipy_sip_tm_handle_incoming_in *hin, struct usipy_sip_tm_handle_incoming_out *hout,
  const struct usipy_msg *msg, uint64_t now_ms)
{
    assert(carg != NULL);
    assert(hin != NULL);
    assert(hout != NULL);
    assert(msg != NULL);

    dump_onwire(carg->scenario, "recv", now_ms, 0, &msg->onwire);
    carg->now_ms = now_ms;
    hin->now_ms = now_ms;
    hin->buf = msg->onwire.s.ro;
    hin->len = msg->onwire.l;
    assert(usipy_sip_tm_handle_incoming(hin, hout) == USIPY_SIP_TM_OK);
}

static const struct usipy_str *
find_tvpair_param(const struct usipy_tvpair *params, int nparams, const char *name)
{
    const size_t nlen = strlen(name);

    assert(params != NULL);
    assert(name != NULL);
    for (int i = 0; i < nparams; i++) {
        if (params[i].token.l == nlen &&
          memcmp(params[i].token.s.ro, name, nlen) == 0) {
            return (&params[i].value);
        }
    }
    return (NULL);
}

static const struct usipy_str *
find_nameaddr_param(const struct usipy_sip_hdr_nameaddr *nap, const char *name)
{
    assert(nap != NULL);
    return (find_tvpair_param(nap->params, nap->nparams, name));
}

static void
assert_expires_header(const struct usipy_msg *msg, const char *value)
{
    struct usipy_sip_hdr_match *matchp;

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
    matchp->hdrslen = 1;
    assert(usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)msg,
      USIPY_HFT_MASK(USIPY_HF_EXPIRES), 1, matchp) == 0);
    assert(matchp->nhdrs == 1);
    assert(matchp->hdrsp[0]->onwire.value.l == strlen(value));
    assert(memcmp(matchp->hdrsp[0]->onwire.value.s.ro, value,
      matchp->hdrsp[0]->onwire.value.l) == 0);
}

static void
assert_invite_ack_request(const struct usipy_msg *invite_reqp,
  const struct usipy_msg *ack_reqp, const char *to_tag, int same_branch,
  const char *remote_target, const char **routev, size_t nroutes)
{
    struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *top;
    const struct usipy_sip_hdr_cseq *cseqp;
    const struct usipy_sip_hdr_via *viap, *invite_viap;
    const struct usipy_str *tagvp, *branchvp, *invite_branchvp;
    const uint64_t ack_mask = USIPY_HFT_MASK(USIPY_HF_TO) |
      USIPY_HFT_MASK(USIPY_HF_CSEQ) | USIPY_HFT_MASK(USIPY_HF_VIA) |
      (nroutes != 0 ? USIPY_HFT_MASK(USIPY_HF_ROUTE) : 0);
    size_t rindex = 0;

    assert(invite_reqp != NULL);
    assert(ack_reqp != NULL);
    assert(to_tag != NULL);
    assert(nroutes == 0 || routev != NULL);

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
    matchp->hdrslen = 1;
    assert(usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)invite_reqp,
      USIPY_HFT_MASK(USIPY_HF_VIA), 1, matchp) == 0);
    assert(matchp->nhdrs == 1);
    invite_viap = matchp->hdrsp[0]->parsed.via;
    assert(invite_viap != NULL);
    invite_branchvp = find_tvpair_param(invite_viap->params, invite_viap->nparams, "branch");
    assert(invite_branchvp != NULL);

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(3 + nroutes));
    matchp->hdrslen = 3 + nroutes;
    assert(usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)ack_reqp, ack_mask,
      0, matchp) == 0);
    top = NULL;
    cseqp = NULL;
    viap = NULL;
    for (size_t i = 0; i < matchp->nhdrs; i++) {
        if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_TO) {
            top = matchp->hdrsp[i]->parsed.to;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_CSEQ) {
            cseqp = matchp->hdrsp[i]->parsed.cseq;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_VIA) {
            viap = matchp->hdrsp[i]->parsed.via;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_ROUTE) {
            assert(rindex < nroutes);
            assert(matchp->hdrsp[i]->onwire.value.l == strlen(routev[rindex]));
            assert(memcmp(matchp->hdrsp[i]->onwire.value.s.ro, routev[rindex],
              matchp->hdrsp[i]->onwire.value.l) == 0);
            rindex += 1;
        }
    }
    assert(top != NULL);
    tagvp = find_nameaddr_param(top, "tag");
    assert(tagvp != NULL);
    assert(tagvp->l == strlen(to_tag));
    assert(memcmp(tagvp->s.ro, to_tag, tagvp->l) == 0);
    assert(cseqp != NULL);
    assert(cseqp->val == 1);
    assert(cseqp->method->cantype == USIPY_SIP_METHOD_ACK);
    if (remote_target != NULL) {
        assert(ack_reqp->sline.parsed.rl.onwire.ruri.l == strlen(remote_target));
        assert(memcmp(ack_reqp->sline.parsed.rl.onwire.ruri.s.ro, remote_target,
          ack_reqp->sline.parsed.rl.onwire.ruri.l) == 0);
    }
    assert(rindex == nroutes);
    assert(viap != NULL);
    branchvp = find_tvpair_param(viap->params, viap->nparams, "branch");
    assert(branchvp != NULL);
    if (same_branch) {
        assert(branchvp->l == invite_branchvp->l);
        assert(memcmp(branchvp->s.ro, invite_branchvp->s.ro, branchvp->l) == 0);
    } else {
        assert(branchvp->l != invite_branchvp->l ||
          memcmp(branchvp->s.ro, invite_branchvp->s.ro, branchvp->l) != 0);
    }
}

static void
init_invite_tx(struct usipy_sip_tm *tm, struct invite_cbarg *carg, size_t *tx_indexp)
{
    struct usipy_sip_tm_new_uac_tr_params tp = {
      .request_id = &(struct usipy_sip_tm_request_id){
        .call_id = &(struct usipy_str)USIPY_2STR("inv-1@example.test"),
        .cseq = 1,
        .method_type = USIPY_SIP_METHOD_INVITE,
      },
      .request_target = &(struct usipy_sip_tm_request_target){
        .request_uri = &(struct usipy_str)USIPY_2STR("sip:bob@example.test"),
        .target = &(struct usipy_sip_tm_addr){
          .af = AF_INET,
          .port = 5060,
          .transport = USIPY_SIP_TM_TRANSPORT_UDP,
          .host = (struct usipy_str)USIPY_2STR("127.0.0.1"),
        },
      },
      .parties_by_username = &(struct usipy_sip_tm_request_parties){
        .from = &(struct usipy_str)USIPY_2STR("alice"),
        .to = &(struct usipy_str)USIPY_2STR("bob"),
        .contact = &(struct usipy_str)USIPY_2STR("alice"),
      },
      .invite_expires = 1,
      .callbacks = &(struct usipy_sip_tm_uac_callbacks){
        .arg = carg,
        .response = invite_response,
        .timeout = invite_timeout,
      },
    };
    struct usipy_sip_tm_tx *txp;

    assert(usipy_sip_tm_new_uac_tr(tm, &tp, tx_indexp) == USIPY_SIP_TM_OK);
    txp = (struct usipy_sip_tm_tx *)usipy_sip_tm_get_transaction(tm, *tx_indexp);
    assert(txp != NULL);
    txp->common.timers.t1_ms = 10;
    txp->common.timers.t4_ms = 400;
    txp->common.timers.timer_d_ms = 400;
}

static struct usipy_sip_tm *
invite_tm_ctor(int *sockp)
{
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm *tm;

    *sockp = bind_loopback_udp();
    tm_ctorp.sock = *sockp;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 4;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    return (tm);
}

static struct usipy_msg *
build_options_request(void)
{
    static const char raw[] =
      "OPTIONS sip:service@example.test SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 198.51.100.10:5060;branch=z9hG4bK-uas-1;rport\r\n"
      "From: <sip:alice@example.test>;tag=caller1\r\n"
      "To: <sip:service@example.test>\r\n"
      "Call-ID: uas-1@example.test\r\n"
      "CSeq: 1 OPTIONS\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;

    return (usipy_sip_msg_ctor_fromwire(raw, sizeof(raw) - 1, &perr));
}

static struct usipy_msg *
build_uas_invite_request(void)
{
    static const char raw[] =
      "INVITE sip:bob@example.test SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 198.51.100.10:5060;branch=z9hG4bK-uas-inv-1;rport\r\n"
      "From: <sip:alice@example.test>;tag=caller1\r\n"
      "To: <sip:bob@example.test>\r\n"
      "Call-ID: uas-invite-1@example.test\r\n"
      "CSeq: 1 INVITE\r\n"
      "Contact: <sip:alice@198.51.100.10:5070>\r\n"
      "Record-Route: <sip:edge1.example.test;lr>\r\n"
      "Record-Route: <sip:edge2.example.test;lr>\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;

    return (usipy_sip_msg_ctor_fromwire(raw, sizeof(raw) - 1, &perr));
}

static struct usipy_msg *
build_uas_invite_ack(const struct usipy_msg *invite_reqp, const struct usipy_msg *respp)
{
    char raw[1024];
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_hdr *viah, *fromh, *toh, *callidh;
    const struct usipy_sip_hdr_cseq *cseqp;
    int blen;

    assert(invite_reqp != NULL);
    assert(respp != NULL);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)invite_reqp,
      USIPY_HFT_MASK(USIPY_HF_CSEQ), 1) == 0);
    viah = find_header(invite_reqp, USIPY_HF_VIA);
    fromh = find_header(invite_reqp, USIPY_HF_FROM);
    toh = find_header(respp, USIPY_HF_TO);
    callidh = find_header(invite_reqp, USIPY_HF_CALLID);
    cseqp = find_header(invite_reqp, USIPY_HF_CSEQ)->parsed.cseq;
    assert(cseqp != NULL);
    blen = snprintf(raw, sizeof(raw),
      "ACK %.*s SIP/2.0\r\n"
      "Via: %.*s\r\n"
      "From: %.*s\r\n"
      "To: %.*s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %u ACK\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      USIPY_SFMT(&invite_reqp->sline.parsed.rl.onwire.ruri),
      USIPY_SFMT(&viah->onwire.value),
      USIPY_SFMT(&fromh->onwire.value),
      USIPY_SFMT(&toh->onwire.value),
      USIPY_SFMT(&callidh->onwire.value),
      cseqp->val);
    assert(blen > 0 && (size_t)blen < sizeof(raw));
    return (usipy_sip_msg_ctor_fromwire(raw, (size_t)blen, &perr));
}

static struct usipy_msg *
build_uas_invite_2xx_ack(const struct usipy_msg *invite_reqp, const struct usipy_msg *respp)
{
    char raw[1024];
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_hdr *fromh, *toh, *callidh, *contacth;
    const struct usipy_sip_hdr_nameaddr *contactp;
    const struct usipy_sip_hdr_cseq *cseqp;
    int blen;

    assert(invite_reqp != NULL);
    assert(respp != NULL);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)invite_reqp,
      USIPY_HFT_MASK(USIPY_HF_CSEQ), 1) == 0);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)respp,
      USIPY_HFT_MASK(USIPY_HF_CONTACT), 1) == 0);
    fromh = find_header(invite_reqp, USIPY_HF_FROM);
    toh = find_header(respp, USIPY_HF_TO);
    callidh = find_header(invite_reqp, USIPY_HF_CALLID);
    contacth = find_header(respp, USIPY_HF_CONTACT);
    contactp = contacth->parsed.contact;
    cseqp = find_header(invite_reqp, USIPY_HF_CSEQ)->parsed.cseq;
    assert(contactp != NULL);
    assert(cseqp != NULL);
    blen = snprintf(raw, sizeof(raw),
      "ACK %.*s SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 198.51.100.10:5060;branch=z9hG4bK-ack-2xx-1;rport\r\n"
      "From: %.*s\r\n"
      "To: %.*s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %u ACK\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      USIPY_SFMT(&contactp->addr_spec),
      USIPY_SFMT(&fromh->onwire.value),
      USIPY_SFMT(&toh->onwire.value),
      USIPY_SFMT(&callidh->onwire.value),
      cseqp->val);
    assert(blen > 0 && (size_t)blen < sizeof(raw));
    return (usipy_sip_msg_ctor_fromwire(raw, (size_t)blen, &perr));
}

static struct usipy_msg *
build_uas_invite_cancel(const struct usipy_msg *invite_reqp)
{
    char raw[1024];
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_hdr *viah, *fromh, *toh, *callidh;
    const struct usipy_sip_hdr_cseq *cseqp;
    int blen;

    assert(invite_reqp != NULL);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)invite_reqp,
      USIPY_HFT_MASK(USIPY_HF_CSEQ), 1) == 0);
    viah = find_header(invite_reqp, USIPY_HF_VIA);
    fromh = find_header(invite_reqp, USIPY_HF_FROM);
    toh = find_header(invite_reqp, USIPY_HF_TO);
    callidh = find_header(invite_reqp, USIPY_HF_CALLID);
    cseqp = find_header(invite_reqp, USIPY_HF_CSEQ)->parsed.cseq;
    assert(cseqp != NULL);
    blen = snprintf(raw, sizeof(raw),
      "CANCEL %.*s SIP/2.0\r\n"
      "Via: %.*s\r\n"
      "From: %.*s\r\n"
      "To: %.*s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %u CANCEL\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      USIPY_SFMT(&invite_reqp->sline.parsed.rl.onwire.ruri),
      USIPY_SFMT(&viah->onwire.value),
      USIPY_SFMT(&fromh->onwire.value),
      USIPY_SFMT(&toh->onwire.value),
      USIPY_SFMT(&callidh->onwire.value),
      cseqp->val);
    assert(blen > 0 && (size_t)blen < sizeof(raw));
    return (usipy_sip_msg_ctor_fromwire(raw, (size_t)blen, &perr));
}

static void
uas_no_ack(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp)
{
    struct uas_cbarg *carg = arg;

    assert(carg != NULL);
    assert(txp != NULL);
    assert(tx_index == carg->tx_index);
    carg->nnoacks += 1;
}

static void
uas_cancel(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
    struct uas_cbarg *carg = arg;
    const struct usipy_sip_tm_uas_response_params *rpp;

    assert(carg != NULL);
    assert(txp != NULL);
    assert(msg != NULL);
    assert(tx_index == carg->tx_index);
    assert(msg->kind == USIPY_SIP_MSG_REQ);
    assert(msg->sline.parsed.rl.method->cantype == USIPY_SIP_METHOD_CANCEL);
    carg->ncancels += 1;
    rpp = (carg->cancel_response.status != NULL &&
      carg->cancel_response.status->code != 0) ? &carg->cancel_response : NULL;
    assert(usipy_sip_tm_uas_tr_cancelled(carg->tm, msg, tx_index, rpp) ==
      USIPY_SIP_TM_OK);
}

static void
assert_uas_response(const struct usipy_msg *reqp, const struct usipy_msg *resp,
  unsigned int code)
{
    struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *top;
    const struct usipy_sip_hdr *req_callidp, *resp_callidp;
    const struct usipy_sip_hdr_cseq *cseqp;
    const struct usipy_sip_hdr_via *viap;
    const struct usipy_str *tagvp;

    assert(reqp != NULL);
    assert(resp != NULL);
    assert(resp->kind == USIPY_SIP_MSG_RES);
    assert(resp->sline.parsed.sl.status.code == code);

    req_callidp = find_header(reqp, USIPY_HF_CALLID);
    resp_callidp = find_header(resp, USIPY_HF_CALLID);
    assert(resp_callidp->onwire.value.l == req_callidp->onwire.value.l);
    assert(memcmp(resp_callidp->onwire.value.s.ro, req_callidp->onwire.value.s.ro,
      resp_callidp->onwire.value.l) == 0);

    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(3));
    matchp->hdrslen = 3;
    assert(usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)resp,
      USIPY_HFT_MASK(USIPY_HF_TO) | USIPY_HFT_MASK(USIPY_HF_CSEQ) |
      USIPY_HFT_MASK(USIPY_HF_VIA), 1, matchp) == 0);
    assert(matchp->nhdrs == 3);
    top = NULL;
    cseqp = NULL;
    viap = NULL;
    for (size_t i = 0; i < matchp->nhdrs; i++) {
        if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_TO) {
            top = matchp->hdrsp[i]->parsed.to;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_CSEQ) {
            cseqp = matchp->hdrsp[i]->parsed.cseq;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_VIA) {
            viap = matchp->hdrsp[i]->parsed.via;
        }
    }
    assert(top != NULL);
    tagvp = find_nameaddr_param(top, "tag");
    assert(tagvp != NULL && tagvp->l != 0);
    assert(cseqp != NULL);
    assert(cseqp->val == 1);
    assert(cseqp->method == reqp->sline.parsed.rl.method);
    assert(viap != NULL);
}

static void
assert_contact_uri(const struct usipy_msg *msg, const char *urip)
{
    const struct usipy_sip_hdr *contacth;
    const struct usipy_sip_hdr_nameaddr *contactp;

    assert(msg != NULL);
    assert(urip != NULL);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)msg,
      USIPY_HFT_MASK(USIPY_HF_CONTACT), 1) == 0);
    contacth = find_header(msg, USIPY_HF_CONTACT);
    assert(contacth != NULL);
    contactp = contacth->parsed.contact;
    assert(contactp != NULL);
    assert(contactp->addr_spec.l == strlen(urip));
    assert(memcmp(contactp->addr_spec.s.ro, urip, contactp->addr_spec.l) == 0);
}

static void
assert_same_contact_uri(const struct usipy_msg *msg, const struct usipy_msg *ref)
{
    const struct usipy_sip_hdr_nameaddr *contactp, *ref_contactp;

    assert(msg != NULL);
    assert(ref != NULL);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)msg,
      USIPY_HFT_MASK(USIPY_HF_CONTACT), 1) == 0);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)ref,
      USIPY_HFT_MASK(USIPY_HF_CONTACT), 1) == 0);
    contactp = find_header(msg, USIPY_HF_CONTACT)->parsed.contact;
    ref_contactp = find_header(ref, USIPY_HF_CONTACT)->parsed.contact;
    assert(contactp != NULL);
    assert(ref_contactp != NULL);
    assert(contactp->addr_spec.l == ref_contactp->addr_spec.l);
    assert(memcmp(contactp->addr_spec.s.ro, ref_contactp->addr_spec.s.ro,
      contactp->addr_spec.l) == 0);
}

static void
uas_incoming_request(void *arg, const struct usipy_sip_tm_handle_incoming_in *hin,
  const struct usipy_msg *msg)
{
    struct uas_cbarg *carg = arg;
    struct usipy_sip_tm_new_uas_tr_params tp = {
      .request = msg,
      .timers = hin->timers,
      .peer = hin->peer,
      .local = hin->local,
      .callbacks = &(struct usipy_sip_tm_uas_callbacks){
        .arg = carg,
        .cancel = uas_cancel,
      },
    };
    int rval;

    assert(carg != NULL);
    assert(hin != NULL);
    assert(msg != NULL);
    dump_onwire(carg->scenario, "request-cb", hin->now_ms, 0, &msg->onwire);
    carg->nrequests += 1;
    rval = usipy_sip_tm_new_uas_tr(carg->tm, &tp, &carg->tx_index);
    assert(rval == USIPY_SIP_TM_OK);
    rval = usipy_sip_tm_send_uas_response(carg->tm, carg->tx_index, &carg->response);
    assert(rval == USIPY_SIP_TM_OK);
}

static void
test_uas_options_retransmit(void)
{
    static const char scenario[] =
      "UAS OPTIONS -> request-cb -> 200 -> retransmit -> 200";
    struct uas_cbarg carg = {
      .tx_index = USIPY_SIP_TM_TX_INDEX_NONE,
      .scenario = scenario,
      .response_status = usipy_sip_res_ok,
      .response = {
        .status = &carg.response_status,
      },
    };
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_callbacks tm_callbacks = {
      .arg = &carg,
      .incoming_request = uas_incoming_request,
    };
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *reqp, *respp;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 2;
    tm_ctorp.callbacks = &tm_callbacks;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    carg.tm = tm;

    reqp = build_options_request();
    assert(reqp != NULL);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    dump_onwire(scenario, "recv", 100, 0, &reqp->onwire);
    hin.tm = tm;
    hin.now_ms = 100;
    hin.buf = reqp->onwire.s.ro;
    hin.len = reqp->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.error == USIPY_SIP_TM_OK);
    assert(hout.consumed != 0);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_NEW);
    assert(hout.event == USIPY_SIP_TM_EVENT_REQUEST_RX);
    assert(hout.transaction_index == carg.tx_index);
    assert(carg.nrequests == 1);

    invite_run_step(&sarg, &rin, &rout, 100);
    assert(sarg.nsent == 1);
    assert(sarg.tx_indexes[0] == carg.tx_index);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->role == USIPY_SIP_TM_ROLE_UAS);
    assert(txp->role_data.uas.last_status_code == 200);
    assert_tx_target(txp, "198.51.100.10", 5060);
    respp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(respp != NULL);
    assert_uas_response(reqp, respp, 200);
    usipy_sip_msg_dtor(respp);

    dump_onwire(scenario, "recv", 200, 0, &reqp->onwire);
    hin.now_ms = 200;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.error == USIPY_SIP_TM_OK);
    assert(hout.consumed != 0);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_EXISTING);
    assert(hout.event == USIPY_SIP_TM_EVENT_REQUEST_RETRANSMIT);
    assert(hout.transaction_index == carg.tx_index);

    invite_run_step(&sarg, &rin, &rout, 200);
    assert(sarg.nsent == 2);
    assert(sarg.tx_indexes[1] == carg.tx_index);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->role_data.uas.request_retransmits == 1);

    invite_run_step(&sarg, &rin, &rout, 3401);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);

    usipy_sip_msg_dtor(reqp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_uas_invite_error_ack(void)
{
    static const char scenario[] =
      "UAS INVITE -> 486 -> retransmit -> ACK -> confirmed -> idle";
    struct uas_cbarg carg = {
      .tx_index = USIPY_SIP_TM_TX_INDEX_NONE,
      .scenario = scenario,
    };
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_callbacks tm_callbacks = {
      .arg = &carg,
      .incoming_request = uas_incoming_request,
    };
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invitep, *respp, *ackp;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 2;
    tm_ctorp.callbacks = &tm_callbacks;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    carg.tm = tm;
    carg.response_status = usipy_sip_res_busy_here;
    carg.response.status = &carg.response_status;
    carg.response_callbacks.arg = &carg;
    carg.response_callbacks.no_ack = uas_no_ack;
    carg.response.callbacks = &carg.response_callbacks;
    invitep = build_uas_invite_request();
    assert(invitep != NULL);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    dump_onwire(scenario, "recv", 100, 0, &invitep->onwire);
    hin.tm = tm;
    hin.now_ms = 100;
    hin.buf = invitep->onwire.s.ro;
    hin.len = invitep->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_NEW);
    assert(carg.nrequests == 1);

    invite_run_step(&sarg, &rin, &rout, 100);
    assert(sarg.nsent == 1);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_COMPLETED);
    assert(txp->common.timer.type == USIPY_SIP_TM_TIMER_H);
    assert(txp->common.timer.due_at_ms == 3300);
    assert(txp->common.outbound.next_send_at_ms == 150);
    respp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(respp != NULL);
    assert_uas_response(invitep, respp, 486);

    invite_run_step(&sarg, &rin, &rout, 150);
    assert(sarg.nsent == 2);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->common.outbound.next_send_at_ms == 250);

    ackp = build_uas_invite_ack(invitep, respp);
    assert(ackp != NULL);
    dump_onwire(scenario, "recv", 200, 0, &ackp->onwire);
    hin.now_ms = 200;
    hin.buf = ackp->onwire.s.ro;
    hin.len = ackp->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_EXISTING);
    assert(hout.event == USIPY_SIP_TM_EVENT_ACK_RX);
    assert(hout.transaction_index == carg.tx_index);

    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_CONFIRMED);
    assert(txp->common.timer.type == USIPY_SIP_TM_TIMER_I);
    assert(txp->common.timer.due_at_ms == 600);
    assert(txp->common.outbound.next_send_at_ms == USIPY_SIP_TM_TIME_NONE);

    invite_run_step(&sarg, &rin, &rout, 300);
    assert(sarg.nsent == 2);
    invite_run_step(&sarg, &rin, &rout, 601);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.nnoacks == 0);

    usipy_sip_msg_dtor(ackp);
    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(invitep);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_uas_invite_error_no_ack_timeout(void)
{
    static const char scenario[] =
      "UAS INVITE -> 486 -> retransmits -> no ACK timeout";
    struct uas_cbarg carg = {
      .tx_index = USIPY_SIP_TM_TX_INDEX_NONE,
      .scenario = scenario,
    };
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_callbacks tm_callbacks = {
      .arg = &carg,
      .incoming_request = uas_incoming_request,
    };
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    struct usipy_sip_tm_handle_incoming_out hout;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invitep;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 2;
    tm_ctorp.callbacks = &tm_callbacks;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    carg.tm = tm;
    carg.response_status = usipy_sip_res_busy_here;
    carg.response.status = &carg.response_status;
    carg.response_callbacks.arg = &carg;
    carg.response_callbacks.no_ack = uas_no_ack;
    carg.response.callbacks = &carg.response_callbacks;
    invitep = build_uas_invite_request();
    assert(invitep != NULL);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    dump_onwire(scenario, "recv", 100, 0, &invitep->onwire);
    hin.tm = tm;
    hin.now_ms = 100;
    hin.buf = invitep->onwire.s.ro;
    hin.len = invitep->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_NEW);

    invite_run_step(&sarg, &rin, &rout, 100);
    invite_run_step(&sarg, &rin, &rout, 150);
    invite_run_step(&sarg, &rin, &rout, 250);
    assert(sarg.nsent == 3);

    invite_run_step(&sarg, &rin, &rout, 3301);
    assert(rout.ntimeouts == 1);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.nnoacks == 1);

    usipy_sip_msg_dtor(invitep);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_uas_invite_cancel_default(void)
{
    static const char scenario[] = "UAS INVITE -> CANCEL -> 200 CANCEL + default 487";
    struct uas_cbarg carg = {
      .tx_index = USIPY_SIP_TM_TX_INDEX_NONE,
      .scenario = scenario,
    };
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_callbacks tm_callbacks = {
      .arg = &carg,
      .incoming_request = uas_incoming_request,
    };
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    struct usipy_sip_tm_handle_incoming_out hout;
    const struct usipy_sip_tm_tx *txp;
    const struct usipy_sip_tm_tx *cancel_txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invitep, *cancelp;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 2;
    tm_ctorp.callbacks = &tm_callbacks;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    carg.tm = tm;
    carg.response_status = usipy_sip_res_ringing;
    carg.response.status = &carg.response_status;
    invitep = build_uas_invite_request();
    assert(invitep != NULL);
    cancelp = build_uas_invite_cancel(invitep);
    assert(cancelp != NULL);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    dump_onwire(scenario, "recv", 100, 0, &invitep->onwire);
    hin.tm = tm;
    hin.now_ms = 100;
    hin.buf = invitep->onwire.s.ro;
    hin.len = invitep->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_NEW);
    assert(carg.nrequests == 1);
    assert(carg.ncancels == 0);

    dump_onwire(scenario, "recv", 150, 0, &cancelp->onwire);
    hin.now_ms = 150;
    hin.buf = cancelp->onwire.s.ro;
    hin.len = cancelp->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_EXISTING);
    assert(hout.transaction_index == carg.tx_index);
    assert(carg.ncancels == 1);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_COMPLETED);
    assert(txp->role_data.uas.last_status_code == 487);
    cancel_txp = usipy_sip_tm_get_transaction(tm, 1);
    assert(cancel_txp != NULL);
    assert(cancel_txp->common.id.method_type == USIPY_SIP_METHOD_CANCEL);
    assert(cancel_txp->state == USIPY_SIP_TM_STATE_COMPLETED);
    assert(cancel_txp->role_data.uas.last_status_code == 200);
    assert(usipy_sip_tm_nactive(tm) == 2);

    invite_run_step(&sarg, &rin, &rout, 150);
    assert(rout.error == USIPY_SIP_TM_OK);
    assert(rout.nsent == 2);

    usipy_sip_msg_dtor(cancelp);
    usipy_sip_msg_dtor(invitep);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_uas_invite_cancel_callback(void)
{
    static const char scenario[] = "UAS INVITE -> CANCEL -> 200 CANCEL + 487";
    struct uas_cbarg carg = {
      .tx_index = USIPY_SIP_TM_TX_INDEX_NONE,
      .scenario = scenario,
    };
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_callbacks tm_callbacks = {
      .arg = &carg,
      .incoming_request = uas_incoming_request,
    };
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    struct usipy_sip_tm_handle_incoming_out hout;
    const struct usipy_sip_tm_tx *txp;
    const struct usipy_sip_tm_tx *cancel_txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invitep, *cancelp;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 2;
    tm_ctorp.callbacks = &tm_callbacks;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    carg.tm = tm;
    carg.response_status = usipy_sip_res_ringing;
    carg.response.status = &carg.response_status;
    carg.cancel_response_status = usipy_sip_res_req_term;
    carg.cancel_response.status = &carg.cancel_response_status;
    invitep = build_uas_invite_request();
    assert(invitep != NULL);
    cancelp = build_uas_invite_cancel(invitep);
    assert(cancelp != NULL);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    dump_onwire(scenario, "recv", 100, 0, &invitep->onwire);
    hin.tm = tm;
    hin.now_ms = 100;
    hin.buf = invitep->onwire.s.ro;
    hin.len = invitep->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_NEW);
    assert(carg.nrequests == 1);
    assert(carg.ncancels == 0);

    dump_onwire(scenario, "recv", 150, 0, &cancelp->onwire);
    hin.now_ms = 150;
    hin.buf = cancelp->onwire.s.ro;
    hin.len = cancelp->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_EXISTING);
    assert(hout.transaction_index == carg.tx_index);
    assert(carg.ncancels == 1);
    txp = usipy_sip_tm_get_transaction(tm, carg.tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_COMPLETED);
    assert(txp->role_data.uas.last_status_code == 487);
    cancel_txp = usipy_sip_tm_get_transaction(tm, 1);
    assert(cancel_txp != NULL);
    assert(cancel_txp->common.id.method_type == USIPY_SIP_METHOD_CANCEL);
    assert(cancel_txp->state == USIPY_SIP_TM_STATE_COMPLETED);
    assert(cancel_txp->role_data.uas.last_status_code == 200);
    assert(usipy_sip_tm_nactive(tm) == 2);

    invite_run_step(&sarg, &rin, &rout, 150);
    assert(rout.error == USIPY_SIP_TM_OK);
    assert(rout.nsent == 2);

    usipy_sip_msg_dtor(cancelp);
    usipy_sip_msg_dtor(invitep);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static size_t
find_method_tx(const struct usipy_sip_tm *tm, size_t invite_index, uint8_t method_type)
{
    for (size_t i = 0; i < 4; i++) {
        const struct usipy_sip_tm_tx *cand = usipy_sip_tm_get_transaction(tm, i);

        if (cand == NULL || i == invite_index) {
            continue;
        }
        if (cand->common.id.method_type == method_type) {
            return (i);
        }
    }
    return (USIPY_SIP_TM_TX_INDEX_NONE);
}

static const struct usipy_sip_hdr *
find_header(const struct usipy_msg *msg, uint8_t hf_type)
{
    assert(msg != NULL);

    for (unsigned int i = 0; i < msg->nhdrs; i++) {
        if (msg->hdrs[i].hf_type->cantype == hf_type) {
            return (&msg->hdrs[i]);
        }
    }
    assert(0);
}

static void
assert_msg_contains_n(const struct usipy_msg *msg, const char *needle, size_t nlen)
{
    assert(msg != NULL);
    assert(needle != NULL);
    assert(msg->onwire.l >= nlen);
    for (size_t i = 0; i + nlen <= msg->onwire.l; i++) {
        if (memcmp(msg->onwire.s.ro + i, needle, nlen) == 0) {
            return;
        }
    }
    assert(0);
}

static void
assert_msg_contains(const struct usipy_msg *msg, const char *needle)
{
    assert_msg_contains_n(msg, needle, strlen(needle));
}

static void
assert_tx_target(const struct usipy_sip_tm_tx *txp, const char *host, unsigned int port)
{
    assert(txp != NULL);
    assert(host != NULL);
    assert(txp->common.outbound.target.host.l == strlen(host));
    assert(memcmp(txp->common.outbound.target.host.s.ro, host,
      txp->common.outbound.target.host.l) == 0);
    assert(txp->common.outbound.target.port == port);
}

static void
assert_cancel_request(const struct usipy_msg *invite_reqp, const struct usipy_msg *cancel_reqp)
{
    const struct usipy_sip_hdr *viah;
    const char *bp = NULL;
    const char *ep;
    struct usipy_str branch;
    static const char branch_prefix[] = "branch=";

    assert(invite_reqp != NULL);
    assert(cancel_reqp != NULL);
    viah = find_header(invite_reqp, USIPY_HF_VIA);
    for (size_t i = 0; i + sizeof(branch_prefix) - 1 <= viah->onwire.value.l; i++) {
        if (memcmp(viah->onwire.value.s.ro + i, branch_prefix,
          sizeof(branch_prefix) - 1) == 0) {
            bp = viah->onwire.value.s.ro + i;
            break;
        }
    }
    assert(bp != NULL);
    ep = bp;
    while ((size_t)(ep - viah->onwire.value.s.ro) < viah->onwire.value.l &&
      *ep != ';' && *ep != '\r') {
        ep++;
    }
    branch.s.ro = bp;
    branch.l = (size_t)(ep - bp);
    assert_msg_contains_n(cancel_reqp, branch.s.ro, branch.l);
    assert_msg_contains(cancel_reqp, "CSeq: 1 CANCEL");
}

static void
assert_bye_request(const struct usipy_msg *invite_reqp, const struct usipy_msg *bye_reqp,
  const char *remote_tag, const char *remote_target, const char **routev,
  size_t nroutes)
{
    struct usipy_sip_hdr_match *matchp;
    const struct usipy_sip_hdr_nameaddr *fromp, *top;
    const struct usipy_sip_hdr_cseq *cseqp;
    const struct usipy_str *tagvp;

    assert(invite_reqp != NULL);
    assert(bye_reqp != NULL);
    assert(remote_tag != NULL);
    assert(remote_target != NULL);
    assert(nroutes == 0 || routev != NULL);

    assert(bye_reqp->kind == USIPY_SIP_MSG_REQ);
    assert(bye_reqp->sline.parsed.rl.method->cantype == USIPY_SIP_METHOD_BYE);
    assert(bye_reqp->sline.parsed.rl.onwire.ruri.l == strlen(remote_target));
    assert(memcmp(bye_reqp->sline.parsed.rl.onwire.ruri.s.ro, remote_target,
      bye_reqp->sline.parsed.rl.onwire.ruri.l) == 0);
    matchp = __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(4 + nroutes));
    *matchp = (struct usipy_sip_hdr_match){.hdrslen = 4 + nroutes};
    assert(usipy_sip_msg_parse_hdrs_get((struct usipy_msg *)bye_reqp,
      USIPY_HFT_MASK(USIPY_HF_FROM) | USIPY_HFT_MASK(USIPY_HF_TO) |
      USIPY_HFT_MASK(USIPY_HF_CSEQ) | USIPY_HFT_MASK(USIPY_HF_CALLID) |
      USIPY_HFT_MASK(USIPY_HF_ROUTE), 0, matchp) == 0);
    fromp = NULL;
    top = NULL;
    cseqp = NULL;
    size_t rindex = 0;
    for (size_t i = 0; i < matchp->nhdrs; i++) {
        if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_FROM) {
            fromp = matchp->hdrsp[i]->parsed.from;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_TO) {
            top = matchp->hdrsp[i]->parsed.to;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_CSEQ) {
            cseqp = matchp->hdrsp[i]->parsed.cseq;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_ROUTE) {
            assert(rindex < nroutes);
            assert(matchp->hdrsp[i]->onwire.value.l == strlen(routev[rindex]));
            assert(memcmp(matchp->hdrsp[i]->onwire.value.s.ro, routev[rindex],
              matchp->hdrsp[i]->onwire.value.l) == 0);
            rindex += 1;
        } else if (matchp->hdrsp[i]->hf_type->cantype == USIPY_HF_CALLID) {
            const struct usipy_sip_hdr *invite_callidp;

            invite_callidp = find_header(invite_reqp, USIPY_HF_CALLID);
            assert(invite_callidp->onwire.value.l == matchp->hdrsp[i]->onwire.value.l);
            assert(memcmp(invite_callidp->onwire.value.s.ro,
              matchp->hdrsp[i]->onwire.value.s.ro,
              matchp->hdrsp[i]->onwire.value.l) == 0);
        }
    }
    assert(fromp != NULL);
    tagvp = find_nameaddr_param(fromp, "tag");
    assert(tagvp != NULL);
    assert(tagvp->l != 0);
    assert(top != NULL);
    tagvp = find_nameaddr_param(top, "tag");
    assert(tagvp != NULL);
    assert(tagvp->l == strlen(remote_tag));
    assert(memcmp(tagvp->s.ro, remote_tag, tagvp->l) == 0);
    assert(cseqp != NULL);
    assert(cseqp->val == 2);
    assert(cseqp->method->cantype == USIPY_SIP_METHOD_BYE);
    assert(rindex == nroutes);
}

static void
assert_request_expires(const struct usipy_sip_tm_tx *txp, const char *value)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_msg *reqp;

    assert(txp != NULL);
    reqp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(reqp != NULL);
    assert_expires_header(reqp, value);
    usipy_sip_msg_dtor(reqp);
}

static struct usipy_msg *
dup_tx_request(const struct usipy_sip_tm_tx *txp)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_msg *reqp;

    assert(txp != NULL);
    reqp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(reqp != NULL);
    return (reqp);
}

static void
test_invite_pr_timeout(void)
{
    static const char scenario[] = "INVITE -> PR timeout";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    size_t tx_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    assert(sarg.nsent == 1);
    assert_request_expires(txp, "1");

    carg.now_ms = 1000;
    invite_run_step(&sarg, &rin, &rout, 1000);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.nresponses == 0);
    assert(carg.ntimeouts == 1);
    assert(carg.timeout_ids[0] == USIPY_SIP_TM_TIMEOUT_PR);

    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_fr_timeout_single_100(void)
{
    static const char scenario[] = "INVITE -> 100 -> FR timeout";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *reqp, *resp;
    size_t tx_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    resp = build_trying_response(reqp);
    usipy_sip_msg_dtor(reqp);

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;
    invite_handle_step(&carg, &hin, &hout, resp, 100);
    assert(carg.nresponses == 1);
    assert(carg.status_codes[0] == 100);

    carg.now_ms = 1000;
    invite_run_step(&sarg, &rin, &rout, 1000);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.ntimeouts == 1);
    assert(carg.timeout_ids[0] == USIPY_SIP_TM_TIMEOUT_FR);

    usipy_sip_msg_dtor(resp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_fr_timeout_repeated_100(void)
{
    static const char scenario[] = "INVITE -> 100 -> 100 -> FR timeout";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *reqp, *resp;
    size_t tx_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    resp = build_trying_response(reqp);
    usipy_sip_msg_dtor(reqp);

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;
    invite_handle_step(&carg, &hin, &hout, resp, 100);
    invite_handle_step(&carg, &hin, &hout, resp, 700);
    assert(carg.nresponses == 2);
    assert(carg.status_codes[0] == 100);
    assert(carg.status_codes[1] == 100);

    carg.now_ms = 1000;
    invite_run_step(&sarg, &rin, &rout, 1000);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.ntimeouts == 1);
    assert(carg.timeout_ids[0] == USIPY_SIP_TM_TIMEOUT_FR);

    usipy_sip_msg_dtor(resp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_ack_support(void)
{
    static const char scenario[] = "INVITE -> 100 -> 200 -> ACK -> 200 -> ACK";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    const struct usipy_str ok_tag = USIPY_2STR(";tag=uas200");
    const struct usipy_str remote_contact = USIPY_2STR("sip:bob@127.0.0.1:5070");
    const struct usipy_str record_routes[] = {
      USIPY_2STR("sip:edge1.example.test;lr"),
      USIPY_2STR("sip:edge2.example.test;lr"),
    };
    const char *routev[] = {
      "sip:edge2.example.test;lr",
      "sip:edge1.example.test;lr",
    };
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp, *ack_txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invite_reqp, *ack_reqp, *resp100, *resp200;
    size_t tx_index, ack_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    invite_reqp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(invite_reqp != NULL);
    assert_expires_header(invite_reqp, "1");
    resp100 = build_trying_response(invite_reqp);
    resp200 = build_response_with_contact_routes(invite_reqp, &usipy_sip_res_ok, &ok_tag,
      &remote_contact, record_routes, sizeof(record_routes) / sizeof(record_routes[0]));

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;

    invite_handle_step(&carg, &hin, &hout, resp100, 100);
    assert(carg.nresponses == 1);
    assert(carg.status_codes[0] == 100);

    invite_handle_step(&carg, &hin, &hout, resp200, 200);
    assert(carg.nresponses == 2);
    assert(carg.status_codes[1] == 200);

    ack_index = find_method_tx(tm, tx_index, USIPY_SIP_METHOD_ACK);
    assert(ack_index != USIPY_SIP_TM_TX_INDEX_NONE);

    invite_run_step(&sarg, &rin, &rout, 200);
    assert(sarg.nsent == 2);
    assert(sarg.tx_indexes[1] == ack_index);

    ack_txp = usipy_sip_tm_get_transaction(tm, ack_index);
    assert(ack_txp != NULL);
    assert_tx_target(ack_txp, "edge2.example.test", 5060);
    ack_reqp = usipy_sip_msg_ctor_fromwire(ack_txp->common.outbound.raw.s.ro,
      ack_txp->common.outbound.raw.l, &perr);
    assert(ack_reqp != NULL);
    assert_invite_ack_request(invite_reqp, ack_reqp, "uas200", 0,
      "sip:bob@127.0.0.1:5070", routev, sizeof(routev) / sizeof(routev[0]));
    usipy_sip_msg_dtor(ack_reqp);

    invite_handle_step(&carg, &hin, &hout, resp200, 300);
    assert(carg.nresponses == 2);

    invite_run_step(&sarg, &rin, &rout, 300);
    assert(sarg.nsent == 3);
    assert(sarg.tx_indexes[2] == ack_index);

    invite_run_step(&sarg, &rin, &rout, 700);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    ack_txp = usipy_sip_tm_get_transaction(tm, ack_index);
    assert(txp != NULL && txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(ack_txp != NULL && ack_txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.ntimeouts == 0);

    usipy_sip_msg_dtor(resp100);
    usipy_sip_msg_dtor(resp200);
    usipy_sip_msg_dtor(invite_reqp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_error_ack_support(void)
{
    static const char scenario[] = "INVITE -> 100 -> 486 -> ACK -> 486 -> ACK";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    const struct usipy_str busy_tag = USIPY_2STR(";tag=uas486");
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp, *ack_txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invite_reqp, *ack_reqp, *resp100, *resp486;
    size_t tx_index, ack_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    invite_reqp = dup_tx_request(txp);
    resp100 = build_trying_response(invite_reqp);
    resp486 = build_basic_response(invite_reqp, &usipy_sip_res_busy_here, &busy_tag);

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;

    invite_handle_step(&carg, &hin, &hout, resp100, 100);
    assert(carg.nresponses == 1);
    assert(carg.status_codes[0] == 100);

    invite_handle_step(&carg, &hin, &hout, resp486, 200);
    assert(carg.nresponses == 2);
    assert(carg.status_codes[1] == 486);

    ack_index = find_method_tx(tm, tx_index, USIPY_SIP_METHOD_ACK);
    assert(ack_index != USIPY_SIP_TM_TX_INDEX_NONE);

    invite_run_step(&sarg, &rin, &rout, 200);
    assert(sarg.nsent == 2);
    assert(sarg.tx_indexes[1] == ack_index);

    ack_txp = usipy_sip_tm_get_transaction(tm, ack_index);
    assert(ack_txp != NULL);
    ack_reqp = usipy_sip_msg_ctor_fromwire(ack_txp->common.outbound.raw.s.ro,
      ack_txp->common.outbound.raw.l, &perr);
    assert(ack_reqp != NULL);
    assert_invite_ack_request(invite_reqp, ack_reqp, "uas486", 1, NULL, NULL, 0);
    usipy_sip_msg_dtor(ack_reqp);

    invite_handle_step(&carg, &hin, &hout, resp486, 300);
    assert(carg.nresponses == 2);

    invite_run_step(&sarg, &rin, &rout, 300);
    assert(sarg.nsent == 3);
    assert(sarg.tx_indexes[2] == ack_index);

    invite_run_step(&sarg, &rin, &rout, 700);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    ack_txp = usipy_sip_tm_get_transaction(tm, ack_index);
    assert(txp != NULL && txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(ack_txp != NULL && ack_txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(carg.ntimeouts == 0);

    usipy_sip_msg_dtor(resp486);
    usipy_sip_msg_dtor(resp100);
    usipy_sip_msg_dtor(invite_reqp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_dialog_end_bye(void)
{
    static const char scenario[] = "INVITE -> 200 -> dialog -> BYE";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    const struct usipy_str ok_tag = USIPY_2STR(";tag=uas200");
    const struct usipy_str remote_contact = USIPY_2STR("sip:bob@127.0.0.1:5070");
    const struct usipy_str record_routes[] = {
      USIPY_2STR("sip:edge1.example.test;lr"),
      USIPY_2STR("sip:edge2.example.test"),
    };
    const char *routev[] = {
      "sip:edge1.example.test;lr",
      "sip:bob@127.0.0.1:5070",
    };
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp, *bye_txp;
    struct usipy_sip_dialog *dialogp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invite_reqp, *bye_reqp, *resp200;
    size_t tx_index, bye_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    invite_reqp = dup_tx_request(txp);
    resp200 = build_response_with_contact_routes(invite_reqp, &usipy_sip_res_ok, &ok_tag,
      &remote_contact, record_routes, sizeof(record_routes) / sizeof(record_routes[0]));
    assert(resp200 != NULL);

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;
    invite_handle_step(&carg, &hin, &hout, resp200, 200);
    assert(carg.nresponses == 1);
    assert(carg.status_codes[0] == 200);

    invite_run_step(&sarg, &rin, &rout, 200);
    dialogp = usipy_sip_dialog_uac_ctor(tm, tx_index, resp200);
    assert(dialogp != NULL);
    assert(usipy_sip_tm_drop_transaction(tm, tx_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_tm_get_transaction(tm, tx_index) == NULL);
    assert(usipy_sip_dialog_end(dialogp, NULL, &bye_index) == USIPY_SIP_TM_OK);

    invite_run_step(&sarg, &rin, &rout, 210);
    assert(sarg.nsent == 3);
    assert(sarg.tx_indexes[2] == bye_index);

    bye_txp = usipy_sip_tm_get_transaction(tm, bye_index);
    assert(bye_txp != NULL);
    assert_tx_target(bye_txp, "edge2.example.test", 5060);
    bye_reqp = usipy_sip_msg_ctor_fromwire(bye_txp->common.outbound.raw.s.ro,
      bye_txp->common.outbound.raw.l, &perr);
    assert(bye_reqp != NULL);
    assert_bye_request(invite_reqp, bye_reqp, "uas200", "sip:edge2.example.test",
      routev, sizeof(routev) / sizeof(routev[0]));
    usipy_sip_msg_dtor(bye_reqp);

    usipy_sip_dialog_dtor(dialogp);

    usipy_sip_msg_dtor(resp200);
    usipy_sip_msg_dtor(invite_reqp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_cancel_pending(void)
{
    static const char scenario[] = "INVITE -> cancel pending -> 100 -> CANCEL";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    const struct usipy_str trying_tag = USIPY_2STR(";tag=uas100");
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp, *cancel_txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invite_reqp, *cancel_reqp, *resp100, *cancel_resp;
    size_t tx_index, cancel_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    invite_reqp = dup_tx_request(txp);
    resp100 = build_trying_response(invite_reqp);

    assert(usipy_sip_tm_cancel(tm, tx_index) == USIPY_SIP_TM_OK);

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;
    invite_handle_step(&carg, &hin, &hout, resp100, 100);
    assert(carg.nresponses == 1);
    assert(carg.status_codes[0] == 100);

    invite_run_step(&sarg, &rin, &rout, 100);
    cancel_index = find_method_tx(tm, tx_index, USIPY_SIP_METHOD_CANCEL);
    assert(cancel_index != USIPY_SIP_TM_TX_INDEX_NONE);
    assert(sarg.nsent == 2);
    assert(sarg.tx_indexes[1] == cancel_index);

    cancel_txp = usipy_sip_tm_get_transaction(tm, cancel_index);
    assert(cancel_txp != NULL);
    cancel_reqp = usipy_sip_msg_ctor_fromwire(cancel_txp->common.outbound.raw.s.ro,
      cancel_txp->common.outbound.raw.l, &perr);
    assert(cancel_reqp != NULL);
    assert_cancel_request(invite_reqp, cancel_reqp);
    cancel_resp = build_basic_response(cancel_reqp, &usipy_sip_res_ok, &trying_tag);

    hin.buf = cancel_resp->onwire.s.ro;
    hin.len = cancel_resp->onwire.l;
    hin.now_ms = 110;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(carg.nresponses == 1);
    assert(carg.ntimeouts == 0);

    usipy_sip_msg_dtor(cancel_resp);
    usipy_sip_msg_dtor(cancel_reqp);
    usipy_sip_msg_dtor(resp100);
    usipy_sip_msg_dtor(invite_reqp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_uas_dialog_end_bye(void)
{
    static const char scenario[] = "UAS INVITE -> 200 dialog -> BYE";
    static const char *routev[] = {
      "<sip:edge1.example.test;lr>",
      "<sip:edge2.example.test;lr>",
    };
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_new_uas_tr_params tpp;
    struct usipy_sip_tm_uas_response_params rpp = {
      .status = &usipy_sip_res_ok,
    };
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp, *bye_txp;
    struct usipy_sip_dialog *dialogp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invitep, *resp200, *bye_reqp;
    size_t tx_index, bye_index;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 4;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    invitep = build_uas_invite_request();
    assert(invitep != NULL);
    tpp = (struct usipy_sip_tm_new_uas_tr_params){
      .request = invitep,
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    assert(usipy_sip_tm_new_uas_tr(tm, &tpp, &tx_index) == USIPY_SIP_TM_OK);
    dialogp = usipy_sip_dialog_uas_ctor(tm, tx_index, &rpp);
    assert(dialogp != NULL);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 100);
    assert(sarg.nsent == 1);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    resp200 = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(resp200 != NULL);
    assert_uas_response(invitep, resp200, 200);
    assert(find_header(resp200, USIPY_HF_CONTACT) != NULL);

    assert(usipy_sip_tm_drop_transaction(tm, tx_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_dialog_end(dialogp, NULL, &bye_index) == USIPY_SIP_TM_OK);

    invite_run_step(&sarg, &rin, &rout, 110);
    assert(sarg.nsent == 2);
    assert(sarg.tx_indexes[1] == bye_index);
    bye_txp = usipy_sip_tm_get_transaction(tm, bye_index);
    assert(bye_txp != NULL);
    assert_tx_target(bye_txp, "edge1.example.test", 5060);
    bye_reqp = usipy_sip_msg_ctor_fromwire(bye_txp->common.outbound.raw.s.ro,
      bye_txp->common.outbound.raw.l, &perr);
    assert(bye_reqp != NULL);
    assert_bye_request(invitep, bye_reqp, "caller1", "sip:alice@198.51.100.10:5070",
      routev, sizeof(routev) / sizeof(routev[0]));
    assert_same_contact_uri(resp200, bye_reqp);

    usipy_sip_msg_dtor(bye_reqp);
    usipy_sip_msg_dtor(resp200);
    usipy_sip_msg_dtor(invitep);
    usipy_sip_dialog_dtor(dialogp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_uas_invite_2xx_ack(void)
{
    static const char scenario[] = "UAS INVITE -> 200 -> 2xx ACK";
    struct invite_send_arg sarg = {.scenario = scenario};
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_new_uas_tr_params tpp;
    struct usipy_sip_tm_uas_response_params rpp = {
      .status = &usipy_sip_res_ok,
    };
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invitep, *respp, *ackp;
    size_t tx_index;
    int sock;

    invite_print_banner(scenario);
    sock = bind_loopback_udp();
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 4;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    invitep = build_uas_invite_request();
    assert(invitep != NULL);
    tpp = (struct usipy_sip_tm_new_uas_tr_params){
      .request = invitep,
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = (struct usipy_str)USIPY_2STR("192.0.2.55"),
      },
    };
    assert(usipy_sip_tm_new_uas_tr(tm, &tpp, &tx_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_tm_send_uas_response(tm, tx_index, &rpp) == USIPY_SIP_TM_OK);

    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;
    invite_run_step(&sarg, &rin, &rout, 100);
    assert(sarg.nsent == 1);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_COMPLETED);
    respp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(respp != NULL);
    ackp = build_uas_invite_2xx_ack(invitep, respp);
    assert(ackp != NULL);

    dump_onwire(scenario, "recv", 150, 0, &ackp->onwire);
    hin.tm = tm;
    hin.now_ms = 150;
    hin.buf = ackp->onwire.s.ro;
    hin.len = ackp->onwire.l;
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
    assert(hout.match_kind == USIPY_SIP_TM_MATCH_EXISTING);
    assert(hout.event == USIPY_SIP_TM_EVENT_ACK_RX);
    assert(hout.transaction_index == tx_index);

    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_COMPLETED);

    usipy_sip_msg_dtor(ackp);
    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(invitep);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_invite_fr_timeout_auto_cancel(void)
{
    static const char scenario[] = "INVITE -> 100 -> FR timeout -> CANCEL";
    struct invite_cbarg carg = {0};
    struct invite_send_arg sarg = {0};
    struct usipy_sip_tm_run_in rin = {0};
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin = {0};
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_tm_tx *txp, *cancel_txp;
    struct usipy_sip_tm *tm;
    struct usipy_msg *invite_reqp, *cancel_reqp, *resp100;
    size_t tx_index, cancel_index;
    int sock;

    tm = invite_tm_ctor(&sock);
    carg.scenario = scenario;
    sarg.scenario = scenario;
    invite_print_banner(scenario);
    init_invite_tx(tm, &carg, &tx_index);
    rin.tm = tm;
    rin.send_to = invite_send_to;
    rin.send_to_arg = &sarg;

    invite_run_step(&sarg, &rin, &rout, 0);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    invite_reqp = dup_tx_request(txp);
    resp100 = build_trying_response(invite_reqp);

    hin.tm = tm;
    hin.peer = &txp->common.peer;
    hin.local = &txp->common.local;
    invite_handle_step(&carg, &hin, &hout, resp100, 100);
    assert(carg.nresponses == 1);
    assert(carg.status_codes[0] == 100);

    carg.now_ms = 1000;
    invite_run_step(&sarg, &rin, &rout, 1000);
    txp = usipy_sip_tm_get_transaction(tm, tx_index);
    cancel_index = find_method_tx(tm, tx_index, USIPY_SIP_METHOD_CANCEL);
    assert(txp != NULL);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(cancel_index != USIPY_SIP_TM_TX_INDEX_NONE);
    assert(carg.ntimeouts == 1);
    assert(carg.timeout_ids[0] == USIPY_SIP_TM_TIMEOUT_FR);
    assert(sarg.nsent == 2);
    assert(sarg.tx_indexes[1] == cancel_index);

    cancel_txp = usipy_sip_tm_get_transaction(tm, cancel_index);
    assert(cancel_txp != NULL);
    cancel_reqp = usipy_sip_msg_ctor_fromwire(cancel_txp->common.outbound.raw.s.ro,
      cancel_txp->common.outbound.raw.l, &perr);
    assert(cancel_reqp != NULL);
    assert_cancel_request(invite_reqp, cancel_reqp);
    assert(usipy_sip_tm_drop_transaction(tm, tx_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_tm_get_transaction(tm, tx_index) == NULL);
    assert(usipy_sip_tm_get_transaction(tm, cancel_index) != NULL);

    invite_run_step(&sarg, &rin, &rout, 1010);
    assert(sarg.nsent == 3);
    assert(sarg.tx_indexes[2] == cancel_index);

    usipy_sip_msg_dtor(cancel_reqp);
    usipy_sip_msg_dtor(resp100);
    usipy_sip_msg_dtor(invite_reqp);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

int
main(void)
{
    struct test_cbarg carg = {0};
    const struct usipy_str to_tag = USIPY_2STR(";tag=r");
    const struct usipy_str www_auth = USIPY_2STR(
      "Digest realm=\"example.test\",nonce=\"abcdef\",algorithm=MD5,qop=auth");
    struct usipy_sip_tm_new_uac_tr_params tp = {
      .request_id = &(struct usipy_sip_tm_request_id){
        .call_id = &(struct usipy_str)USIPY_2STR("reg-1@example.test"),
        .cseq = 1,
        .method_type = USIPY_SIP_METHOD_REGISTER,
      },
      .request_target = &(struct usipy_sip_tm_request_target){
        .request_uri = &(struct usipy_str)USIPY_2STR("sip:registrar.example.test"),
        .target = &(struct usipy_sip_tm_addr){
          .af = AF_INET,
          .port = 5060,
          .transport = USIPY_SIP_TM_TRANSPORT_UDP,
          .host = (struct usipy_str)USIPY_2STR("127.0.0.1"),
        },
      },
      .parties_by_username = &(struct usipy_sip_tm_request_parties){
        .contact = &(struct usipy_str)USIPY_2STR("alice"),
        .from = &(struct usipy_str)USIPY_2STR("alice"),
        .to = &(struct usipy_str)USIPY_2STR("alice"),
      },
      .callbacks = &(struct usipy_sip_tm_uac_callbacks){
        .arg = &carg,
        .response = uac_response,
        .timeout = uac_timeout,
      },
    };
    struct usipy_sip_tm_handle_incoming_in hin;
    struct usipy_sip_tm_handle_incoming_out hout;
    struct usipy_sip_tm_run_in rin;
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_ctor_params tm_ctorp;
    struct usipy_sip_tm_tx *txp;
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_msg *reqp, *resp;
    struct usipy_sip_tid tid;
    size_t total_sent = 0, total_timeouts = 0;
    int response_injected = 0;
    struct usipy_sip_tm *tm;
    size_t tx_index;
    int second_started = 0;
    int sock;

    test_gen_auth_hf();
    test_register_expires_helpers();
    test_invite_pr_timeout();
    test_invite_fr_timeout_single_100();
    test_invite_fr_timeout_repeated_100();
    test_invite_ack_support();
    test_invite_error_ack_support();
    test_invite_dialog_end_bye();
    test_uas_dialog_end_bye();
    test_uas_invite_2xx_ack();
    test_invite_cancel_pending();
    test_invite_fr_timeout_auto_cancel();
    test_uas_options_retransmit();
    test_uas_invite_error_ack();
    test_uas_invite_error_no_ack_timeout();
    test_uas_invite_cancel_default();
    test_uas_invite_cancel_callback();
    assert(fprintf(stdout, "\n-- REGISTER auth/retransmit --\n") > 0);
    carg.username = (struct usipy_str)USIPY_2STR("alice");
    carg.password = (struct usipy_str)USIPY_2STR("secret");
    carg.qop = (struct usipy_str)USIPY_2STR("auth");
    carg.extra_hdr_values[0] = (struct usipy_str)USIPY_2STR("60");
    carg.extra_hdrs[0].hf_type = USIPY_HF_EXPIRES;
    carg.extra_hdrs[0].value = &carg.extra_hdr_values[0];
    carg.start_ms = usipy_tm_uac_mono_ms();
    sock = bind_loopback_udp();
    memset(&tm_ctorp, '\0', sizeof(tm_ctorp));
    tm_ctorp.sock = sock;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 4;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    carg.tm = tm;
    assert(usipy_sip_tm_new_uac_tr(tm, &tp, &tx_index) == USIPY_SIP_TM_OK);
    txp = (struct usipy_sip_tm_tx *)usipy_sip_tm_get_transaction(tm, tx_index);
    assert(txp != NULL);
    txp->common.timers.t1_ms = 50;
    txp->common.timers.t2_ms = 200;
    txp->common.timers.timer_f_ms = 500;
    rin.tm = tm;
    rin.send_to = stdout_send_to;
    rin.send_to_arg = &carg;
    memset(&hin, '\0', sizeof(hin));
    hin.tm = tm;
    while (!carg.stop) {
        rin.now_ms = usipy_tm_uac_mono_ms();
        assert(usipy_sip_tm_run(&rin, &rout) == USIPY_SIP_TM_OK);
        total_sent += rout.nsent;
        total_timeouts += rout.ntimeouts;
        if (!second_started && carg.ntimeouts == 1) {
            assert(usipy_sip_tm_next_transaction(tm, tx_index, NULL,
              &carg.extra_hdrs[0], 1) ==
              USIPY_SIP_TM_OK);
            second_started = 1;
            txp = (struct usipy_sip_tm_tx *)usipy_sip_tm_get_transaction(tm, tx_index);
            assert(txp != NULL);
            txp->common.timers.t1_ms = 50;
            txp->common.timers.t2_ms = 200;
            txp->common.timers.timer_f_ms = 500;
            continue;
        }
        if (second_started && !response_injected && txp->common.id.cseq == 2 &&
          txp->common.retransmit_count == 1) {
            reqp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
              txp->common.outbound.raw.l, &perr);
            assert(reqp != NULL);
            assert(usipy_sip_msg_get_tid(reqp, &tid) == 0);
            assert(tid.cseq != NULL);
            assert(tid.cseq->val == 2);
            resp = build_unauth_response(reqp, &usipy_sip_res_unauth,
              &to_tag, &www_auth);
            assert(fwrite(resp->onwire.s.ro, 1, resp->onwire.l, stdout) == resp->onwire.l);
            hin.now_ms = usipy_tm_uac_mono_ms();
            hin.peer = tp.request_target->target;
            hin.local = &txp->common.local;
            hin.buf = resp->onwire.s.ro;
            hin.len = resp->onwire.l;
            assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
            assert(hout.error == USIPY_SIP_TM_OK);
            assert(hout.consumed != 0);
            assert(hout.match_kind == USIPY_SIP_TM_MATCH_EXISTING);
            assert(hout.event == USIPY_SIP_TM_EVENT_RESPONSE_FINAL);
            assert(hout.transaction_index == tx_index);
            assert(carg.nresponses == 1);
            assert(carg.last_status_code == 401);
            response_injected = 1;
            txp = (struct usipy_sip_tm_tx *)usipy_sip_tm_get_transaction(tm, tx_index);
            assert(txp != NULL);
            usipy_sip_msg_dtor(resp);
            usipy_sip_msg_dtor(reqp);
            continue;
        }
        if (!carg.stop) {
            assert(rout.next_run_at_ms != USIPY_SIP_TM_TIME_NONE);
            usipy_tm_uac_sleep_until_ms(rout.next_run_at_ms);
        }
    }
    assert(txp->common.outbound.raw.s.ro == NULL);
    assert(second_started != 0);
    assert(carg.auth_retry_started != 0);
    assert(response_injected != 0);
    assert(total_sent == 9);
    assert(carg.nsent == 9);
    assert(carg.nresponses == 1);
    assert(carg.last_status_code == 401);
    assert(total_timeouts == 2);
    assert(carg.ntimeouts == 2);
    assert(txp->common.id.cseq == 3);
    assert(txp->common.retransmit_count == 4);
    assert(txp->common.outbound.next_send_at_ms == USIPY_SIP_TM_TIME_NONE);
    assert(txp->state == USIPY_SIP_TM_STATE_TERMINATED);
    usipy_sip_tm_dtor(tm);
    close(sock);
    return (0);
}
