#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "public/microsippy.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_hdr_cseq.h"
#include "usipy_sip_res.h"

struct emit_log {
    struct usipy_sip_tm *tm;
    size_t count;
    struct usipy_sip_ua_emit emits[8];
};

static void run_tm_once(struct usipy_sip_tm *tm, uint64_t now_ms);

static int
noop_send_to(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_sip_tm_outbound *outp)
{
    (void)arg;
    (void)tx_index;
    (void)txp;
    (void)outp;
    return (0);
}

static int
bind_loopback_udp(void)
{
    int sock;
    struct sockaddr_in sin = {0};

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock >= 0);
    sin.sin_family = AF_INET;
    assert(inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr) == 1);
    sin.sin_port = 0;
    if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) != 0) {
        perror("bind_loopback_udp bind");
        assert(0);
    }
    return (sock);
}

static void
capture_emit(void *arg, const struct usipy_sip_ua_emit *emitp)
{
    struct emit_log *elog = arg;

    assert(elog != NULL);
    assert(emitp != NULL);
    assert(elog->count < sizeof(elog->emits) / sizeof(elog->emits[0]));
    elog->emits[elog->count++] = *emitp;
    if (elog->tm != NULL) {
        run_tm_once(elog->tm, 0);
    }
}

static struct usipy_sip_tm *
make_tm(int *sockp)
{
    struct usipy_sip_tm_ctor_params tm_ctorp = {0};
    struct usipy_sip_tm *tm;

    *sockp = bind_loopback_udp();
    tm_ctorp.sock = *sockp;
    tm_ctorp.transport = USIPY_SIP_TM_TRANSPORT_UDP;
    tm_ctorp.max_transactions = 8;
    tm = usipy_sip_tm_ctor(&tm_ctorp);
    assert(tm != NULL);
    return (tm);
}

static void
run_tm_once(struct usipy_sip_tm *tm, uint64_t now_ms)
{
    struct usipy_sip_tm_run_in rin = {
      .now_ms = now_ms,
      .tm = tm,
      .send_to = noop_send_to,
    };
    struct usipy_sip_tm_run_out rout;

    assert(tm != NULL);
    assert(usipy_sip_tm_run(&rin, &rout) == USIPY_SIP_TM_OK);
}

static void
handle_incoming_msg(struct usipy_sip_tm *tm, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg, uint64_t now_ms)
{
    static const struct usipy_sip_tm_timer_policy no_timers;
    struct usipy_sip_tm_handle_incoming_in hin = {
      .now_ms = now_ms,
      .tm = tm,
      .timers = &no_timers,
      .peer = &txp->common.peer,
      .local = &txp->common.local,
      .buf = msg->onwire.s.ro,
      .len = msg->onwire.l,
    };
    struct usipy_sip_tm_handle_incoming_out hout;

    assert(tm != NULL);
    assert(txp != NULL);
    assert(msg != NULL);
    assert(usipy_sip_tm_handle_incoming(&hin, &hout) == USIPY_SIP_TM_OK);
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

static struct usipy_msg *
dup_tx_request(const struct usipy_sip_tm_tx *txp)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_msg *msg;

    assert(txp != NULL);
    msg = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(msg != NULL);
    return (msg);
}

static struct usipy_msg *
build_response(const struct usipy_msg *reqp, const struct usipy_sip_status *statusp,
  const char *to_tag, const char *contact_uri)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_hdr *viah, *fromh, *toh, *callidh, *cseqh;
    char raw[1024];
    int blen;

    assert(reqp != NULL);
    assert(statusp != NULL);
    viah = find_header(reqp, USIPY_HF_VIA);
    fromh = find_header(reqp, USIPY_HF_FROM);
    toh = find_header(reqp, USIPY_HF_TO);
    callidh = find_header(reqp, USIPY_HF_CALLID);
    cseqh = find_header(reqp, USIPY_HF_CSEQ);
    blen = snprintf(raw, sizeof(raw),
      "SIP/2.0 %u %.*s\r\n"
      "Via: %.*s\r\n"
      "From: %.*s\r\n"
      "To: %.*s%s%s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %.*s\r\n"
      "%s%s%s%s"
      "Content-Length: 0\r\n"
      "\r\n",
      statusp->code, USIPY_SFMT(&statusp->reason_phrase),
      USIPY_SFMT(&viah->onwire.value),
      USIPY_SFMT(&fromh->onwire.value),
      USIPY_SFMT(&toh->onwire.value),
      (to_tag != NULL ? ";tag=" : ""), (to_tag != NULL ? to_tag : ""),
      USIPY_SFMT(&callidh->onwire.value),
      USIPY_SFMT(&cseqh->onwire.value),
      (contact_uri != NULL ? "Contact: <" : ""),
      (contact_uri != NULL ? contact_uri : ""),
      (contact_uri != NULL ? ">\r\n" : ""),
      "");
    assert(blen > 0 && (size_t)blen < sizeof(raw));
    return (usipy_sip_msg_ctor_fromwire(raw, (size_t)blen, &perr));
}

static struct usipy_msg *
build_auth_response(const struct usipy_msg *reqp, const struct usipy_sip_status *statusp,
  const char *to_tag, const char *header_name, const char *header_value)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_hdr *viah, *fromh, *toh, *callidh, *cseqh;
    char raw[1400];
    int blen;

    assert(reqp != NULL);
    assert(statusp != NULL);
    assert(header_name != NULL);
    assert(header_value != NULL);
    viah = find_header(reqp, USIPY_HF_VIA);
    fromh = find_header(reqp, USIPY_HF_FROM);
    toh = find_header(reqp, USIPY_HF_TO);
    callidh = find_header(reqp, USIPY_HF_CALLID);
    cseqh = find_header(reqp, USIPY_HF_CSEQ);
    blen = snprintf(raw, sizeof(raw),
      "SIP/2.0 %u %.*s\r\n"
      "Via: %.*s\r\n"
      "From: %.*s\r\n"
      "To: %.*s%s%s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %.*s\r\n"
      "%s: %s\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      statusp->code, USIPY_SFMT(&statusp->reason_phrase),
      USIPY_SFMT(&viah->onwire.value),
      USIPY_SFMT(&fromh->onwire.value),
      USIPY_SFMT(&toh->onwire.value),
      (to_tag != NULL ? ";tag=" : ""), (to_tag != NULL ? to_tag : ""),
      USIPY_SFMT(&callidh->onwire.value),
      USIPY_SFMT(&cseqh->onwire.value),
      header_name, header_value);
    assert(blen > 0 && (size_t)blen < sizeof(raw));
    return (usipy_sip_msg_ctor_fromwire(raw, (size_t)blen, &perr));
}

static struct usipy_msg *
build_uas_invite_request(void)
{
    static const char raw[] =
      "INVITE sip:bob@example.test SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 198.51.100.10:5060;branch=z9hG4bK-ua-inv-1;rport\r\n"
      "From: <sip:alice@example.test>;tag=caller1\r\n"
      "To: <sip:bob@example.test>\r\n"
      "Call-ID: ua-invite-1@example.test\r\n"
      "CSeq: 1 INVITE\r\n"
      "Contact: <sip:alice@198.51.100.10:5070>\r\n"
      "Record-Route: <sip:edge1.example.test;lr>\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;

    return (usipy_sip_msg_ctor_fromwire(raw, sizeof(raw) - 1, &perr));
}

static struct usipy_msg *
build_connected_bye(const struct usipy_msg *invp, const struct usipy_msg *respp)
{
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    const struct usipy_sip_hdr *fromh, *callidh;
    const struct usipy_sip_hdr_nameaddr *top, *contactp;
    const struct usipy_sip_hdr_cseq *cseqp;
    char raw[1024];
    int blen;

    assert(invp != NULL);
    assert(respp != NULL);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)invp,
      USIPY_HFT_MASK(USIPY_HF_FROM) | USIPY_HFT_MASK(USIPY_HF_CALLID), 1) == 0);
    assert(usipy_sip_msg_parse_hdrs((struct usipy_msg *)respp,
      USIPY_HFT_MASK(USIPY_HF_TO) | USIPY_HFT_MASK(USIPY_HF_CONTACT) |
      USIPY_HFT_MASK(USIPY_HF_CSEQ), 1) == 0);
    fromh = find_header(invp, USIPY_HF_FROM);
    callidh = find_header(invp, USIPY_HF_CALLID);
    top = find_header(respp, USIPY_HF_TO)->parsed.to;
    contactp = find_header(respp, USIPY_HF_CONTACT)->parsed.contact;
    cseqp = find_header(respp, USIPY_HF_CSEQ)->parsed.cseq;
    assert(top != NULL);
    assert(contactp != NULL);
    assert(cseqp != NULL);
    blen = snprintf(raw, sizeof(raw),
      "BYE %.*s SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 198.51.100.10:5060;branch=z9hG4bK-ua-bye-1;rport\r\n"
      "From: %.*s\r\n"
      "To: %.*s\r\n"
      "Call-ID: %.*s\r\n"
      "CSeq: %u BYE\r\n"
      "Content-Length: 0\r\n"
      "\r\n",
      USIPY_SFMT(&contactp->addr_spec),
      USIPY_SFMT(&fromh->onwire.value),
      USIPY_SFMT(&find_header(respp, USIPY_HF_TO)->onwire.value),
      USIPY_SFMT(&callidh->onwire.value),
      cseqp->val + 1);
    assert(blen > 0 && (size_t)blen < sizeof(raw));
    return (usipy_sip_msg_ctor_fromwire(raw, (size_t)blen, &perr));
}

static void
test_ua_outgoing_connect_disconnect(void)
{
    struct usipy_sip_ua_ctor_params ucp = {0};
    struct usipy_sip_ua_event ev = {0};
    struct emit_log elog = {0};
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *uap;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_msg *reqp, *respp;
    size_t invite_index, bye_index;
    int sock;

    tm = make_tm(&sock);
    elog.tm = tm;
    ucp.tm = tm;
    ucp.emit = capture_emit;
    ucp.emit_arg = &elog;
    uap = usipy_sip_ua_ctor(&ucp);
    assert(uap != NULL);

    ev.type = USIPY_SIP_UA_EVENT_DIAL;
    ev.data.dial = (struct usipy_sip_ua_dial_params){
      .request = &(struct usipy_sip_tm_new_uac_tr_params){
        .request_id = &(struct usipy_sip_tm_request_id){
        .call_id = &(struct usipy_str)USIPY_2STR("ua-out-1@example.test"),
        .cseq = 1,
        .method_type = USIPY_SIP_METHOD_INVITE,
        },
        .request_target = &(struct usipy_sip_tm_request_target){
        .request_uri = &(struct usipy_str)USIPY_2STR("sip:bob@example.test"),
        .target = &(struct usipy_sip_tm_addr){
          .af = AF_INET,
          .port = 5060,
          .transport = USIPY_SIP_TM_TRANSPORT_UDP,
          .host = USIPY_2STR("198.51.100.10"),
        },
        },
        .parties_by_username = &(struct usipy_sip_tm_request_parties){
        .from = &(struct usipy_str)USIPY_2STR("alice"),
        .to = &(struct usipy_str)USIPY_2STR("bob"),
        .contact = &(struct usipy_str)USIPY_2STR("alice"),
        },
        .invite_expires = 1,
        .payload = &(struct usipy_sip_tm_request_payload){
          .content_type = &(struct usipy_str)USIPY_2STR("application/sdp"),
          .body = &(struct usipy_str)USIPY_2STR("v=0\r\n"),
        },
        .callbacks = &(struct usipy_sip_tm_uac_callbacks){0},
      },
      .auth = &(struct usipy_sip_ua_credentials){
        .username = &(struct usipy_str)USIPY_2STR("alice"),
        .password = &(struct usipy_str)USIPY_2STR("secret"),
        .qop = &(struct usipy_str)USIPY_2STR("auth"),
      },
    };
    assert(usipy_sip_ua_on_event(uap, &ev, &invite_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DIALING);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    struct usipy_sip_hdr_match ctype_match = {.hdrslen = 1};
    assert(usipy_sip_msg_parse_hdrs_get(reqp, USIPY_HFT_MASK(USIPY_HF_CONTENTTYPE), 0,
      &ctype_match) == 0);
    assert(ctype_match.nhdrs == 1);
    assert(reqp->body.l == 5);
    assert(memcmp(reqp->body.s.ro, "v=0\r\n", 5) == 0);
    assert(find_header(reqp, USIPY_HF_CONTENTTYPE)->onwire.value.l == 15);
    assert(memcmp(find_header(reqp, USIPY_HF_CONTENTTYPE)->onwire.value.s.ro,
      "application/sdp", 15) == 0);
    respp = build_response(reqp, &usipy_sip_res_ok, "uas200", "sip:bob@198.51.100.10:5070");
    handle_incoming_msg(tm, txp, respp, 100);
    assert(usipy_sip_ua_on_tx_response(uap, invite_index, respp) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_CONNECTED);
    assert(elog.count == 1);
    assert(elog.emits[0].type == USIPY_SIP_UA_EMIT_CONNECT);
    assert(elog.emits[0].message == respp);

    ev.type = USIPY_SIP_UA_EVENT_DISCONNECT;
    assert(usipy_sip_ua_on_event(uap, &ev, &bye_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DISCONNECTED);
    txp = usipy_sip_tm_get_transaction(tm, bye_index);
    assert(txp != NULL);
    assert(txp->role == USIPY_SIP_TM_ROLE_UAC);
    assert(txp->common.id.method_type == USIPY_SIP_METHOD_BYE);
    assert(elog.count == 2);
    assert(elog.emits[1].type == USIPY_SIP_UA_EMIT_DISCONNECT);

    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(reqp);
    usipy_sip_ua_dtor(uap);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_ua_outgoing_reject(void)
{
    struct usipy_sip_ua_ctor_params ucp = {0};
    struct usipy_sip_ua_event ev = {0};
    struct emit_log elog = {0};
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *uap;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_msg *reqp, *respp;
    size_t invite_index;
    int sock;

    tm = make_tm(&sock);
    elog.tm = tm;
    ucp.tm = tm;
    ucp.emit = capture_emit;
    ucp.emit_arg = &elog;
    uap = usipy_sip_ua_ctor(&ucp);
    assert(uap != NULL);

    ev.type = USIPY_SIP_UA_EVENT_DIAL;
    ev.data.dial = (struct usipy_sip_ua_dial_params){
      .request = &(struct usipy_sip_tm_new_uac_tr_params){
        .request_id = &(struct usipy_sip_tm_request_id){
        .call_id = &(struct usipy_str)USIPY_2STR("ua-out-2@example.test"),
        .cseq = 1,
        .method_type = USIPY_SIP_METHOD_INVITE,
        },
        .request_target = &(struct usipy_sip_tm_request_target){
        .request_uri = &(struct usipy_str)USIPY_2STR("sip:bob@example.test"),
        .target = &(struct usipy_sip_tm_addr){
          .af = AF_INET,
          .port = 5060,
          .transport = USIPY_SIP_TM_TRANSPORT_UDP,
          .host = USIPY_2STR("198.51.100.10"),
        },
        },
        .parties_by_username = &(struct usipy_sip_tm_request_parties){
        .from = &(struct usipy_str)USIPY_2STR("alice"),
        .to = &(struct usipy_str)USIPY_2STR("bob"),
        .contact = &(struct usipy_str)USIPY_2STR("alice"),
        },
        .invite_expires = 1,
        .callbacks = &(struct usipy_sip_tm_uac_callbacks){0},
      },
    };
    assert(usipy_sip_ua_on_event(uap, &ev, &invite_index) == USIPY_SIP_TM_OK);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    respp = build_response(reqp, &usipy_sip_res_busy_here, "uas486", NULL);
    handle_incoming_msg(tm, txp, respp, 100);
    assert(usipy_sip_ua_on_tx_response(uap, invite_index, respp) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DISCONNECTED);
    assert(elog.count == 1);
    assert(elog.emits[0].type == USIPY_SIP_UA_EMIT_DISCONNECT);
    assert(elog.emits[0].message == respp);

    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(reqp);
    usipy_sip_ua_dtor(uap);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_ua_outgoing_auth_retry(void)
{
    struct usipy_sip_ua_ctor_params ucp = {0};
    struct usipy_sip_ua_event ev = {0};
    struct emit_log elog = {0};
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *uap;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_msg *req1p, *req2p, *res401p, *res200p;
    size_t invite_index;
    int sock;

    tm = make_tm(&sock);
    elog.tm = tm;
    ucp.tm = tm;
    ucp.emit = capture_emit;
    ucp.emit_arg = &elog;
    uap = usipy_sip_ua_ctor(&ucp);
    assert(uap != NULL);

    ev.type = USIPY_SIP_UA_EVENT_DIAL;
    ev.data.dial = (struct usipy_sip_ua_dial_params){
      .request = &(struct usipy_sip_tm_new_uac_tr_params){
        .request_id = &(struct usipy_sip_tm_request_id){
          .call_id = &(struct usipy_str)USIPY_2STR("ua-out-auth@example.test"),
          .cseq = 1,
          .method_type = USIPY_SIP_METHOD_INVITE,
        },
        .request_target = &(struct usipy_sip_tm_request_target){
          .request_uri = &(struct usipy_str)USIPY_2STR("sip:bob@example.test"),
          .target = &(struct usipy_sip_tm_addr){
            .af = AF_INET,
            .port = 5060,
            .transport = USIPY_SIP_TM_TRANSPORT_UDP,
            .host = USIPY_2STR("198.51.100.10"),
          },
        },
        .parties_by_username = &(struct usipy_sip_tm_request_parties){
          .from = &(struct usipy_str)USIPY_2STR("alice"),
          .to = &(struct usipy_str)USIPY_2STR("bob"),
          .contact = &(struct usipy_str)USIPY_2STR("alice"),
        },
        .invite_expires = 1,
        .payload = &(struct usipy_sip_tm_request_payload){
          .content_type = &(struct usipy_str)USIPY_2STR("application/sdp"),
          .body = &(struct usipy_str)USIPY_2STR("v=0\r\n"),
        },
        .callbacks = &(struct usipy_sip_tm_uac_callbacks){0},
      },
      .auth = &(struct usipy_sip_ua_credentials){
        .username = &(struct usipy_str)USIPY_2STR("alice"),
        .password = &(struct usipy_str)USIPY_2STR("secret"),
        .qop = &(struct usipy_str)USIPY_2STR("auth"),
      },
    };
    assert(usipy_sip_ua_on_event(uap, &ev, &invite_index) == USIPY_SIP_TM_OK);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    req1p = dup_tx_request(txp);
    res401p = build_auth_response(req1p, &usipy_sip_res_unauth, "uas401",
      "WWW-Authenticate",
      "Digest realm=\"example.test\", nonce=\"abcdef\", qop=\"auth\"");
    handle_incoming_msg(tm, txp, res401p, 100);
    assert(usipy_sip_ua_on_tx_response(uap, invite_index, res401p) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DIALING);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    req2p = dup_tx_request(txp);
    assert(find_header(req2p, USIPY_HF_CSEQ)->onwire.value.l == sizeof("2 INVITE") - 1);
    assert(memcmp(find_header(req2p, USIPY_HF_CSEQ)->onwire.value.s.ro,
      "2 INVITE", sizeof("2 INVITE") - 1) == 0);
    assert(find_header(req2p, USIPY_HF_AUTHORIZATION)->onwire.value.l != 0);
    assert(find_header(req2p, USIPY_HF_CONTENTTYPE)->onwire.value.l == 15);
    assert(memcmp(find_header(req2p, USIPY_HF_CONTENTTYPE)->onwire.value.s.ro,
      "application/sdp", 15) == 0);
    assert(req2p->body.l == 5);
    assert(memcmp(req2p->body.s.ro, "v=0\r\n", 5) == 0);
    res200p = build_response(req2p, &usipy_sip_res_ok, "uas200",
      "sip:bob@198.51.100.10:5070");
    handle_incoming_msg(tm, txp, res200p, 200);
    assert(usipy_sip_ua_on_tx_response(uap, invite_index, res200p) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_CONNECTED);
    assert(elog.count == 1);
    assert(elog.emits[0].type == USIPY_SIP_UA_EMIT_CONNECT);

    usipy_sip_msg_dtor(res200p);
    usipy_sip_msg_dtor(req2p);
    usipy_sip_msg_dtor(res401p);
    usipy_sip_msg_dtor(req1p);
    usipy_sip_ua_dtor(uap);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

static void
test_ua_incoming_connect_bye(void)
{
    struct usipy_sip_ua_ctor_params ucp = {0};
    struct usipy_sip_tm_new_uas_tr_params tpp;
    struct usipy_sip_ua_event ev = {0};
    struct emit_log elog = {0};
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *uap;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_msg *invp, *respp, *byep;
    size_t invite_index, bye_index;
    int sock;

    tm = make_tm(&sock);
    elog.tm = tm;
    ucp.tm = tm;
    ucp.emit = capture_emit;
    ucp.emit_arg = &elog;
    uap = usipy_sip_ua_ctor(&ucp);
    assert(uap != NULL);

    invp = build_uas_invite_request();
    tpp = (struct usipy_sip_tm_new_uas_tr_params){
      .request = invp,
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50,
        .t2_ms = 200,
        .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET,
        .port = 5060,
        .transport = USIPY_SIP_TM_TRANSPORT_UDP,
        .host = USIPY_2STR("192.0.2.55"),
      },
    };
    assert(usipy_sip_tm_new_uas_tr(tm, &tpp, &invite_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_on_transaction(uap, invite_index, invp) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_TRYING);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    assert(txp->role_data.uas.last_status_code == 100);
    assert(elog.count == 1);
    assert(elog.emits[0].type == USIPY_SIP_UA_EMIT_DIAL);
    assert(elog.emits[0].message == invp);

    ev.type = USIPY_SIP_UA_EVENT_CONNECT;
    ev.data.response = (struct usipy_sip_tm_uas_response_params){
      .status = &usipy_sip_res_ok,
    };
    assert(usipy_sip_ua_on_event(uap, &ev, &invite_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_CONNECTED);
    assert(elog.count == 2);
    assert(elog.emits[1].type == USIPY_SIP_UA_EMIT_CONNECT);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    respp = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(respp != NULL);

    byep = build_connected_bye(invp, respp);
    assert(usipy_sip_ua_matches_transaction(uap, byep) == 1);
    tpp.request = byep;
    assert(usipy_sip_tm_new_uas_tr(tm, &tpp, &bye_index) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_on_transaction(uap, bye_index, byep) == USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DISCONNECTED);
    assert(elog.count == 3);
    assert(elog.emits[2].type == USIPY_SIP_UA_EMIT_DISCONNECT);
    txp = usipy_sip_tm_get_transaction(tm, bye_index);
    assert(txp != NULL);
    assert(txp->role_data.uas.last_status_code == 200);

    usipy_sip_msg_dtor(byep);
    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(invp);
    usipy_sip_ua_dtor(uap);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

int
main(void)
{
    test_ua_outgoing_connect_disconnect();
    test_ua_outgoing_auth_retry();
    test_ua_outgoing_reject();
    test_ua_incoming_connect_bye();
    return (0);
}
