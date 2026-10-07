#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <unistd.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "public/microsippy.h"
#include "public/usipy_sip_tm_utils.h"
#include "usipy_sip_hdr.h"
#include "usipy_sip_hdr_db.h"
#include "usipy_tvpair.h"
#include "usipy_sip_hdr_nameaddr.h"
#include "usipy_sip_hdr_cseq.h"
#include "usipy_sip_res.h"
#include "usipy_sip_tm_internal.h"
#include "sip_ua/usipy_sip_ua_internal.h"

#define ASSERT_CALL_EQ(expr, expected) do { \
    int assert_rval = (expr); \
    assert(assert_rval == (expected)); \
} while (0)

struct emit_log {
    struct usipy_sip_tm *tm;
    size_t count;
    int check_peer_bye;
    int custom_bye_response;
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
    int rval;
    struct sockaddr_in sin = {0};

    sock = socket(AF_INET, SOCK_DGRAM, 0);
    assert(sock >= 0);
    sin.sin_family = AF_INET;
    rval = inet_pton(AF_INET, "127.0.0.1", &sin.sin_addr);
    assert(rval == 1);
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

/* Pumping the TM from the callback must not send the default BYE response. */
static void
capture_bye_emit(void *arg, const struct usipy_sip_ua_emit *emitp)
{
    struct emit_log *elog = arg;
    const struct usipy_sip_tm_tx *txp;
    static const struct usipy_str content_type = USIPY_2STR("text/plain");
    static const struct usipy_str body = USIPY_2STR("application BYE response");

    capture_emit(arg, emitp);
    if (!elog->check_peer_bye || emitp->type != USIPY_SIP_UA_EMIT_DISCONNECT) {
        return;
    }
    assert(emitp->state == USIPY_SIP_UA_STATE_DISCONNECTED);
    assert(emitp->message != NULL);
    assert(emitp->response != NULL);
    assert(emitp->response->status == &usipy_sip_res_ok);
    txp = usipy_sip_tm_get_transaction(elog->tm, emitp->transaction_index);
    assert(txp != NULL);
    assert(txp->role_data.uas.last_status_code < 200);
    if (elog->custom_bye_response) {
        emitp->response->content_type = &content_type;
        emitp->response->body = &body;
        run_tm_once(elog->tm, 0);
        assert(txp->role_data.uas.last_status_code < 200);
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
    ASSERT_CALL_EQ(usipy_sip_tm_run(&rin, &rout), USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_tm_handle_incoming(&hin, &hout), USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_msg_parse_hdrs((struct usipy_msg *)invp,
      USIPY_HFT_MASK(USIPY_HF_FROM) | USIPY_HFT_MASK(USIPY_HF_CALLID), 1), 0);
    ASSERT_CALL_EQ(usipy_sip_msg_parse_hdrs((struct usipy_msg *)respp,
      USIPY_HFT_MASK(USIPY_HF_TO) | USIPY_HFT_MASK(USIPY_HF_CONTACT) |
      USIPY_HFT_MASK(USIPY_HF_CSEQ), 1), 0);
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
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &invite_index),
      USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DIALING);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    struct usipy_sip_hdr_match *ctype_match =
      __builtin_alloca(USIPY_SIP_HDR_MATCH_SIZE(1));
    *ctype_match = (struct usipy_sip_hdr_match){.hdrslen = 1};
    ASSERT_CALL_EQ(usipy_sip_msg_parse_hdrs_get(reqp,
      USIPY_HFT_MASK(USIPY_HF_CONTENTTYPE), 0, ctype_match), 0);
    assert(ctype_match->nhdrs == 1);
    assert(reqp->body.l == 5);
    assert(memcmp(reqp->body.s.ro, "v=0\r\n", 5) == 0);
    assert(find_header(reqp, USIPY_HF_CONTENTTYPE)->onwire.value.l == 15);
    assert(memcmp(find_header(reqp, USIPY_HF_CONTENTTYPE)->onwire.value.s.ro,
      "application/sdp", 15) == 0);
    respp = build_response(reqp, &usipy_sip_res_ok, "uas200", "sip:bob@198.51.100.10:5070");
    handle_incoming_msg(tm, txp, respp, 100);
    ASSERT_CALL_EQ(usipy_sip_ua_on_tx_response(uap, invite_index, respp),
      USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_CONNECTED);
    assert(elog.count == 1);
    assert(elog.emits[0].type == USIPY_SIP_UA_EMIT_CONNECT);
    assert(elog.emits[0].message == respp);

    ev.type = USIPY_SIP_UA_EVENT_DISCONNECT;
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &bye_index),
      USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &invite_index),
      USIPY_SIP_TM_OK);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    respp = build_response(reqp, &usipy_sip_res_busy_here, "uas486", NULL);
    handle_incoming_msg(tm, txp, respp, 100);
    ASSERT_CALL_EQ(usipy_sip_ua_on_tx_response(uap, invite_index, respp),
      USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &invite_index),
      USIPY_SIP_TM_OK);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    req1p = dup_tx_request(txp);
    res401p = build_auth_response(req1p, &usipy_sip_res_unauth, "uas401",
      "WWW-Authenticate",
      "Digest realm=\"example.test\", nonce=\"abcdef\", qop=\"auth\"");
    handle_incoming_msg(tm, txp, res401p, 100);
    ASSERT_CALL_EQ(usipy_sip_ua_on_tx_response(uap, invite_index, res401p),
      USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_ua_on_tx_response(uap, invite_index, res200p),
      USIPY_SIP_TM_OK);
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
test_ua_incoming_connect_bye(int custom_response)
{
    struct usipy_sip_ua_ctor_params ucp = {0};
    struct usipy_sip_tm_new_uas_tr_params tpp;
    struct usipy_sip_ua_event ev = {0};
    struct emit_log elog = {0};
    struct usipy_msg_parse_err perr = USIPY_MSG_PARSE_ERR_init;
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *uap;
    const struct usipy_sip_tm_tx *txp;
    struct usipy_msg *invp, *respp, *byep, *bye_response;
    size_t invite_index, bye_index;
    int sock;

    tm = make_tm(&sock);
    elog.tm = tm;
    elog.check_peer_bye = 1;
    elog.custom_bye_response = custom_response;
    ucp.tm = tm;
    ucp.emit = capture_bye_emit;
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
    ASSERT_CALL_EQ(usipy_sip_tm_new_uas_tr(tm, &tpp, &invite_index),
      USIPY_SIP_TM_OK);
    ASSERT_CALL_EQ(usipy_sip_ua_on_transaction(uap, invite_index, invp),
      USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &invite_index),
      USIPY_SIP_TM_OK);
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
    ASSERT_CALL_EQ(usipy_sip_tm_new_uas_tr(tm, &tpp, &bye_index),
      USIPY_SIP_TM_OK);
    ASSERT_CALL_EQ(usipy_sip_ua_on_transaction(uap, bye_index, byep),
      USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DISCONNECTED);
    assert(elog.count == 3);
    assert(elog.emits[2].type == USIPY_SIP_UA_EMIT_DISCONNECT);
    txp = usipy_sip_tm_get_transaction(tm, bye_index);
    assert(txp != NULL);
    assert(txp->role_data.uas.last_status_code == 200);
    bye_response = usipy_sip_msg_ctor_fromwire(txp->common.outbound.raw.s.ro,
      txp->common.outbound.raw.l, &perr);
    assert(bye_response != NULL);
    if (custom_response) {
        assert(bye_response->body.l == strlen("application BYE response"));
        assert(memcmp(bye_response->body.s.ro, "application BYE response",
          bye_response->body.l) == 0);
    } else {
        assert(bye_response->body.l == 0);
    }
    usipy_sip_msg_dtor(bye_response);

    run_tm_once(tm, 3300);
    assert(elog.count == 3);

    usipy_sip_msg_dtor(byep);
    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(invp);
    usipy_sip_ua_dtor(uap);
    usipy_sip_tm_dtor(tm);
    close(sock);
}

/* Drive the real UA/dialog/transaction stack with a deterministic clock. */
struct accepted_call {
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *ua;
    struct usipy_msg *invite;
    struct usipy_msg *answer;
    struct emit_log events;
    size_t invite_index;
    size_t answers;
    size_t byes;
    size_t noacks;
    size_t failed;
    int fail_answers;
    int keep_unacked;
    uint64_t now;
    int sock;
};

static int
accepted_send(void *arg, size_t index, const struct usipy_sip_tm_tx *tx,
  const struct usipy_sip_tm_outbound *out)
{
    struct accepted_call *call = arg;

    if (tx->role == USIPY_SIP_TM_ROLE_UAS) {
        assert(index == call->invite_index);
        assert(tx->role_data.uas.last_status_code == 200);
        assert(out->raw.l == call->answer->onwire.l);
        assert(memcmp(out->raw.s.ro, call->answer->onwire.s.ro, out->raw.l) == 0);
        if (call->fail_answers) {
            call->failed++;
            return (-1);
        }
        call->answers++;
    } else {
        assert(tx->common.id.method_type == USIPY_SIP_METHOD_BYE);
        call->byes++;
    }
    return (0);
}

/* A run of the transactions that may fail (the 2xx not getting sent) */
static int
accepted_try(struct accepted_call *call, uint64_t now)
{
    struct usipy_sip_tm_run_in in = {
      .tm = call->tm, .now_ms = now,
      .send_to = accepted_send, .send_to_arg = call,
    };
    struct usipy_sip_tm_run_out out;

    call->now = now;
    return (usipy_sip_tm_run(&in, &out));
}

static struct usipy_sip_tm_run_out
accepted_run(struct accepted_call *call, uint64_t now)
{
    struct usipy_sip_tm_run_in in = {
      .tm = call->tm, .now_ms = now,
      .send_to = accepted_send, .send_to_arg = call,
    };
    struct usipy_sip_tm_run_out out;

    call->now = now;
    ASSERT_CALL_EQ(usipy_sip_tm_run(&in, &out), USIPY_SIP_TM_OK);
    return (out);
}

static int
accepted_no_ack(void *arg, size_t index, const struct usipy_sip_tm_tx *tx)
{
    struct accepted_call *call = arg;

    assert(index == call->invite_index);
    assert(tx->role_data.uas.last_status_code == 200);
    assert(call->noacks++ == 0);
    /* Digger's emit callback pumps transactions too. Re-entry must not
     * repeat the timeout callback or recursively disconnect the UA. */
    accepted_run(call, call->now);
    return (call->keep_unacked);
}

static void
accepted_init(struct accepted_call *call, enum usipy_sip_tm_transport transport,
  int fail_answers)
{
    *call = (struct accepted_call){0};
    call->sock = bind_loopback_udp();
    call->tm = usipy_sip_tm_ctor(&(struct usipy_sip_tm_ctor_params){
      .sock = call->sock, .transport = transport, .max_transactions = 8,
    });
    assert(call->tm != NULL);
    call->ua = usipy_sip_ua_ctor(&(struct usipy_sip_ua_ctor_params){
      .tm = call->tm, .emit = capture_emit, .emit_arg = &call->events,
    });
    assert(call->ua != NULL);
    call->invite = build_uas_invite_request();
    assert(call->invite != NULL);
    struct usipy_sip_tm_new_uas_tr_params params = {
      .request = call->invite,
      .callbacks = &(struct usipy_sip_tm_uas_callbacks){.arg = call},
      .timers = &(struct usipy_sip_tm_timer_policy){
        .t1_ms = 50, .t2_ms = 200, .t4_ms = 400,
      },
      .peer = &(struct usipy_sip_tm_addr){
        .af = AF_INET, .port = 5060, .transport = transport,
        .host = USIPY_2STR("198.51.100.10"),
      },
      .local = &(struct usipy_sip_tm_addr){
        .af = AF_INET, .port = 5060, .transport = transport,
        .host = USIPY_2STR("192.0.2.55"),
      },
    };
    ASSERT_CALL_EQ(usipy_sip_tm_new_uas_tr(call->tm, &params, &call->invite_index),
      USIPY_SIP_TM_OK);
    ASSERT_CALL_EQ(usipy_sip_ua_on_transaction(call->ua, call->invite_index,
      call->invite), USIPY_SIP_TM_OK);
    /* A provisional send must not increase the first 2xx retry interval. */
    run_tm_once(call->tm, 10);
    struct usipy_sip_ua_event ev = {
      .type = USIPY_SIP_UA_EVENT_CONNECT,
      .data.response = {
        .status = &usipy_sip_res_ok,
        .body = &(struct usipy_str)USIPY_2STR("test SDP"),
        .content_type = &(struct usipy_str)USIPY_2STR("application/sdp"),
        .callbacks = &(struct usipy_sip_tm_uas_callbacks){
          .no_ack = accepted_no_ack,
        },
      },
    };
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(call->ua, &ev, NULL), USIPY_SIP_TM_OK);
    call->answer = dup_tx_request(usipy_sip_tm_get_transaction(call->tm,
      call->invite_index));
    call->fail_answers = fail_answers;
    if (fail_answers) {
        assert(accepted_try(call, 100) != USIPY_SIP_TM_OK);
        assert(call->answers == 0 && call->failed == 1);
    } else {
        assert(accepted_run(call, 100).next_run_at_ms == 150);
        assert(call->answers == 1);
    }
    assert(usipy_sip_ua_get_state(call->ua) == USIPY_SIP_UA_STATE_CONNECTED);
}

static void
accepted_fini(struct accepted_call *call)
{
    usipy_sip_ua_dtor(call->ua);
    usipy_sip_tm_dtor(call->tm);
    usipy_sip_msg_dtor(call->invite);
    usipy_sip_msg_dtor(call->answer);
    close(call->sock);
}

static void
accepted_ack(struct accepted_call *call, uint32_t cseq, int wrong_tag)
{
    char raw[1024];
    const struct usipy_sip_hdr *to = find_header(call->answer, USIPY_HF_TO);
    struct usipy_sip_tm_handle_incoming_out out;
    const int len = snprintf(raw, sizeof(raw),
      "ACK sip:bob@example.test SIP/2.0\r\n"
      "Via: SIP/2.0/UDP 198.51.100.10:5060;branch=z9hG4bK-new-ack-branch\r\n"
      "From: <sip:alice@example.test>;tag=caller1\r\n"
      "To: %.*s\r\n"
      "Call-ID: ua-invite-1@example.test\r\n"
      "CSeq: %u ACK\r\n"
      "Content-Length: 0\r\n\r\n",
      wrong_tag ? (int)strlen("<sip:bob@example.test>;tag=wrong") : (int)to->onwire.value.l,
      wrong_tag ? "<sip:bob@example.test>;tag=wrong" : to->onwire.value.s.ro,
      cseq);
    assert(len > 0 && (size_t)len < sizeof(raw));
    struct usipy_sip_tm_handle_incoming_in in = {
      .tm = call->tm, .now_ms = call->now, .buf = raw, .len = (size_t)len,
    };
    ASSERT_CALL_EQ(usipy_sip_tm_handle_incoming(&in, &out),
      cseq == 1 && !wrong_tag ? USIPY_SIP_TM_OK : USIPY_SIP_TM_ERR_UNSUPPORTED);
    assert((out.event == USIPY_SIP_TM_EVENT_ACK_RX) == (cseq == 1 && !wrong_tag));
}

static void
test_ua_accepted_ack(enum usipy_sip_tm_transport transport)
{
    struct accepted_call call;
    accepted_init(&call, transport, 0);
    const struct usipy_sip_tm_tx *tx =
      usipy_sip_tm_get_transaction(call.tm, call.invite_index);
    assert(tx->state == USIPY_SIP_TM_STATE_ACCEPTED);
    assert(tx->common.timer.type == USIPY_SIP_TM_TIMER_L);
    assert(tx->common.timer.due_at_ms == 3300);
    assert(accepted_run(&call, 149).nsent == 0);
    assert(accepted_run(&call, 150).next_run_at_ms == 250);
    assert(call.answers == 2);

    /* A retransmitted INVITE is absorbed, without shifting the retry clock. */
    handle_incoming_msg(call.tm, tx, call.invite, 175);
    assert(accepted_run(&call, 175).next_run_at_ms == 250);
    assert(call.answers == 2);
    accepted_ack(&call, 2, 0);
    accepted_ack(&call, 1, 1);
    assert(accepted_run(&call, 250).next_run_at_ms == 450);
    assert(accepted_run(&call, 450).next_run_at_ms == 650);
    assert(accepted_run(&call, 650).next_run_at_ms == 850);
    assert(call.answers == 5);
    accepted_ack(&call, 1, 0);
    accepted_ack(&call, 1, 0);
    assert(accepted_run(&call, 850).next_run_at_ms == 3300);
    handle_incoming_msg(call.tm, tx, call.invite, 900);
    assert(accepted_run(&call, 900).nsent == 0);
    assert(accepted_run(&call, 3300).ntimeouts == 0);
    assert(tx->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(call.answers == 5 && call.noacks == 0 && call.byes == 0);
    assert(usipy_sip_ua_get_state(call.ua) == USIPY_SIP_UA_STATE_CONNECTED);
    usipy_sip_tm_reap_terminated(call.tm);
    /* Destroying the dialog after transaction reaping is safe. */
    accepted_fini(&call);
}

static void
test_ua_accepted_no_ack(enum usipy_sip_tm_transport transport)
{
    struct accepted_call call;
    accepted_init(&call, transport, 0);
    assert(accepted_run(&call, 150).next_run_at_ms == 250);
    assert(accepted_run(&call, 250).next_run_at_ms == 450);
    for (uint64_t now = 450; now <= 3250; now += 200) {
        const struct usipy_sip_tm_run_out out = accepted_run(&call, now);
        assert(out.nsent == 1 && out.ntimeouts == 0);
        assert(out.next_run_at_ms == (now < 3250 ? now + 200 : 3300));
    }
    assert(call.noacks == 0 && call.byes == 0);
    assert(accepted_run(&call, 3300).ntimeouts == 1);
    assert(call.answers == 18 && call.noacks == 1 && call.byes == 1);
    assert(usipy_sip_ua_get_state(call.ua) == USIPY_SIP_UA_STATE_DISCONNECTED);
    assert(call.events.count == 3);
    assert(call.events.emits[2].type == USIPY_SIP_UA_EMIT_DISCONNECT);
    const struct usipy_sip_tm_tx *bye = usipy_sip_tm_get_transaction(call.tm,
      call.events.emits[2].transaction_index);
    assert(bye != NULL && bye->common.id.method_type == USIPY_SIP_METHOD_BYE);
    struct usipy_msg *bye_msg = dup_tx_request(bye);
    struct usipy_msg *bye_ok = build_response(bye_msg, &usipy_sip_res_ok, NULL, NULL);
    handle_incoming_msg(call.tm, bye, bye_ok, 3301);
    accepted_run(&call, 10000);
    assert(call.noacks == 1 && call.answers == 18 && call.byes == 1);
    assert(call.events.count == 3);
    usipy_sip_msg_dtor(bye_ok);
    usipy_sip_msg_dtor(bye_msg);
    accepted_fini(&call);
}

static void
test_ua_accepted_destroy(void)
{
    struct accepted_call call;
    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 0);
    usipy_sip_ua_dtor(call.ua);
    call.ua = NULL;
    accepted_run(&call, 150);
    accepted_run(&call, 3300);
    assert(call.answers == 1 && call.noacks == 0 && call.byes == 0);
    accepted_fini(&call);
}

static void
test_ua_accepted_hangup(void)
{
    struct accepted_call call;
    const struct usipy_sip_ua_event ev = {.type = USIPY_SIP_UA_EVENT_DISCONNECT};

    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 0);
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(call.ua, &ev, NULL), USIPY_SIP_TM_OK);
    accepted_run(&call, 150);
    accepted_run(&call, 3300);
    assert(call.answers == 1 && call.noacks == 0 && call.byes > 0);
    assert(call.events.count == 3);
    accepted_fini(&call);
}

/* The no_ack callback keeps a call whose 2xx went without ACK: no BYE, the
 * UA isn't told, and the 2xx isn't sent any more. */
static void
test_ua_accepted_no_ack_kept(void)
{
    struct accepted_call call;
    const struct usipy_sip_tm_tx *tx;

    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 0);
    call.keep_unacked = 1;
    tx = usipy_sip_tm_get_transaction(call.tm, call.invite_index);
    for (uint64_t now = 150; now < 3300; now += 50) {
        accepted_run(&call, now);
    }
    const size_t answers = call.answers;
    assert(accepted_run(&call, 3300).ntimeouts == 1);
    assert(call.noacks == 1 && call.byes == 0);
    assert(tx->state == USIPY_SIP_TM_STATE_TERMINATED);
    assert(usipy_sip_ua_get_state(call.ua) == USIPY_SIP_UA_STATE_CONNECTED);
    assert(call.events.count == 2);
    accepted_run(&call, 10000);
    assert(call.answers == answers && call.byes == 0 && call.noacks == 1);
    accepted_fini(&call);
}

/* The 2xx can't be sent at all: Timer L still runs from the first try at
 * it, and ends the call when it's up. */
static void
test_ua_accepted_send_fails(void)
{
    struct accepted_call call;
    const struct usipy_sip_tm_tx *tx;

    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 1);
    tx = usipy_sip_tm_get_transaction(call.tm, call.invite_index);
    assert(tx->state == USIPY_SIP_TM_STATE_ACCEPTED);
    assert(tx->common.timer.type == USIPY_SIP_TM_TIMER_L);
    assert(tx->common.timer.due_at_ms == 3300);
    assert(accepted_try(&call, 1000) != USIPY_SIP_TM_OK);
    assert(call.failed == 2 && call.answers == 0);
    assert(tx->common.timer.due_at_ms == 3300);
    assert(accepted_run(&call, 3300).ntimeouts == 1);
    assert(call.noacks == 1 && call.byes == 1 && call.answers == 0);
    assert(usipy_sip_ua_get_state(call.ua) == USIPY_SIP_UA_STATE_DISCONNECTED);
    assert(call.events.count == 3);
    assert(call.events.emits[2].type == USIPY_SIP_UA_EMIT_DISCONNECT);
    accepted_fini(&call);
}

/* A call that's no longer up in the UA when its 2xx goes without ACK: the
 * dialog still ends it and tells the INVITE's no_ack, but not the UA. */
static void
test_ua_accepted_no_ack_not_connected(void)
{
    struct accepted_call call;

    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 0);
    assert(call.events.count == 2);
    usipy_sip_ua_transition(call.ua, USIPY_SIP_UA_STATE_DISCONNECTED);
    for (uint64_t now = 150; now < 3300; now += 50) {
        accepted_run(&call, now);
    }
    assert(accepted_run(&call, 3300).ntimeouts == 1);
    assert(call.noacks == 1 && call.byes == 1);
    assert(call.events.count == 2);
    assert(usipy_sip_ua_get_state(call.ua) == USIPY_SIP_UA_STATE_DISCONNECTED);
    accepted_fini(&call);
}

/* The calls the dialog drives the transaction with fail for one it doesn't
 * own, nor any longer once the transaction is reaped. */
static void
test_ua_accepted_owner(void)
{
    static const struct usipy_sip_tm_uas_owner no_owner;
    struct accepted_call call;
    struct usipy_sip_tm_uas_callbacks cb;
    struct usipy_sip_dialog *dp;
    const size_t unused = 7;

    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 0);
    dp = call.ua->dialogp;
    assert(dp != NULL);
    assert(usipy_sip_tm_get_transaction(call.tm, unused) == NULL);
    assert(usipy_sip_tm_uas_owned_tx(call.tm, call.invite_index, dp) != NULL);
    assert(usipy_sip_tm_uas_owned_tx(call.tm, call.invite_index, &call) == NULL);
    assert(usipy_sip_tm_uas_send_at(call.tm, call.invite_index, &call, 0) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    assert(usipy_sip_tm_uas_clear_owner(call.tm, call.invite_index, &call) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    assert(usipy_sip_tm_uas_set_owner(call.tm, unused, &no_owner, &call) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    assert(usipy_sip_tm_uas_set_owner(call.tm, 1000, &no_owner, &call) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    assert(usipy_sip_tm_uas_get_callbacks(call.tm, unused, &cb) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    /* Still its owner: the 2xx goes on being sent */
    assert(accepted_run(&call, 150).next_run_at_ms == 250);
    assert(call.answers == 2);
    accepted_ack(&call, 1, 0);
    assert(accepted_run(&call, 3300).ntimeouts == 0);
    usipy_sip_tm_reap_terminated(call.tm);
    assert(usipy_sip_tm_uas_owned_tx(call.tm, call.invite_index, dp) == NULL);
    assert(usipy_sip_tm_uas_send_at(call.tm, call.invite_index, dp, 0) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    assert(usipy_sip_tm_uas_clear_owner(call.tm, call.invite_index, dp) ==
      USIPY_SIP_TM_ERR_NOT_FOUND);
    accepted_fini(&call);
}

/* Timer policies are taken field by field: base timers left 0 are RFC
 * 3261's, other timers left 0 are derived from them. */
static void
test_tm_timer_policy(void)
{
    static const struct usipy_sip_tm_timer_policy rfc3261 =
      USIPY_SIP_TM_TIMER_POLICY_RFC3261;
    struct usipy_sip_tm_timer_policy p;
    struct accepted_call call;
    const struct usipy_sip_tm_tx *tx;

    usipy_sip_tm_timer_policy_resolve(&p, NULL);
    assert(memcmp(&p, &rfc3261, sizeof(p)) == 0);
    usipy_sip_tm_timer_policy_resolve(&p,
      &(struct usipy_sip_tm_timer_policy){.timer_l_ms = 200,
        .timer_b_ms = 25});
    assert(p.t1_ms == rfc3261.t1_ms && p.t2_ms == rfc3261.t2_ms &&
      p.t4_ms == rfc3261.t4_ms && p.timer_l_ms == 200);
    assert(p.timer_f_ms == 0 && p.timer_j_ms == 0 && p.timer_k_ms == 0);
    assert(p.timer_b_ms == 25);
    usipy_sip_tm_timer_policy_resolve(&p,
      &(struct usipy_sip_tm_timer_policy){.t1_ms = 50});
    assert(p.t1_ms == 50 && p.t2_ms == rfc3261.t2_ms &&
      p.t4_ms == rfc3261.t4_ms);

    /* Set on a transaction: all of it 0 is RFC 3261's */
    accepted_init(&call, USIPY_SIP_TM_TRANSPORT_UDP, 0);
    tx = usipy_sip_tm_get_transaction(call.tm, call.invite_index);
    ASSERT_CALL_EQ(usipy_sip_tm_set_timer_policy(call.tm, call.invite_index,
      &(struct usipy_sip_tm_timer_policy){0}), USIPY_SIP_TM_OK);
    assert(memcmp(&tx->common.timers, &rfc3261, sizeof(rfc3261)) == 0);
    accepted_fini(&call);
}

/* An accepted INVITE whose policy only has Timer L in it: the other
 * timers are RFC 3261's, Timer L that. */
static void
test_ua_accepted_timer_l_only(void)
{
    struct usipy_sip_tm *tm;
    struct usipy_msg *invite;
    size_t tx_index;
    const struct usipy_sip_tm_tx *tx;
    const int sock = bind_loopback_udp();

    tm = usipy_sip_tm_ctor(&(struct usipy_sip_tm_ctor_params){
      .sock = sock, .transport = USIPY_SIP_TM_TRANSPORT_UDP,
      .max_transactions = 4,
    });
    assert(tm != NULL);
    invite = build_uas_invite_request();
    assert(invite != NULL);
    ASSERT_CALL_EQ(usipy_sip_tm_new_uas_tr(tm,
      &(struct usipy_sip_tm_new_uas_tr_params){
        .request = invite,
        .timers = &(struct usipy_sip_tm_timer_policy){.timer_l_ms = 1234},
        .peer = &(struct usipy_sip_tm_addr){
          .af = AF_INET, .port = 5060, .transport = USIPY_SIP_TM_TRANSPORT_UDP,
          .host = USIPY_2STR("198.51.100.10"),
        },
        .local = &(struct usipy_sip_tm_addr){
          .af = AF_INET, .port = 5060, .transport = USIPY_SIP_TM_TRANSPORT_UDP,
          .host = USIPY_2STR("192.0.2.55"),
        },
      }, &tx_index), USIPY_SIP_TM_OK);
    tx = usipy_sip_tm_get_transaction(tm, tx_index);
    assert(tx->common.timers.t1_ms == 500 && tx->common.timers.t2_ms == 4000);
    ASSERT_CALL_EQ(usipy_sip_tm_send_uas_response(tm, tx_index,
      &(struct usipy_sip_tm_uas_response_params){.status = &usipy_sip_res_ok}),
      USIPY_SIP_TM_OK);
    assert(tx->state == USIPY_SIP_TM_STATE_ACCEPTED);
    assert(tx->common.timer.type == USIPY_SIP_TM_TIMER_L);
    assert(tx->common.timer.value_ms == 1234);
    usipy_sip_tm_dtor(tm);
    usipy_sip_msg_dtor(invite);
    close(sock);
}

static void
test_ua_disconnect_after_abandon(int deadline, int gone)
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
        .timers = &(struct usipy_sip_tm_timer_policy){
          .timer_b_ms = deadline ? 5 : 0,
        },
        .callbacks = &(struct usipy_sip_tm_uac_callbacks){0},
      },
    };
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &invite_index),
      USIPY_SIP_TM_OK);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    run_tm_once(tm, 0);
    txp = usipy_sip_tm_get_transaction(tm, invite_index);
    assert(txp != NULL);
    reqp = dup_tx_request(txp);
    respp = build_response(reqp, &usipy_sip_res_trying, "uas100", NULL);
    if (!deadline) {
        handle_incoming_msg(tm, txp, respp, 1);
    }
    run_tm_once(tm, deadline ? 5 : 1000);
    assert((txp->common.flags & USIPY_SIP_TM_F_ABANDONED) != 0);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DIALING);
    if (gone) {
        /* Reaped, and with gone > 1 its slot taken by someone else's INVITE */
        ASSERT_CALL_EQ(usipy_sip_tm_drop_transaction(tm, invite_index),
          USIPY_SIP_TM_OK);
        if (gone > 1) {
            size_t other_index;

            ASSERT_CALL_EQ(usipy_sip_tm_new_uac_tr(tm,
              &(struct usipy_sip_tm_new_uac_tr_params){
                .request_id = &(struct usipy_sip_tm_request_id){
                  .call_id = &(struct usipy_str)USIPY_2STR("other@example.test"),
                  .cseq = 1,
                  .method_type = USIPY_SIP_METHOD_INVITE,
                },
                .request_target = ev.data.dial.request->request_target,
                .parties_by_username = ev.data.dial.request->parties_by_username,
                .callbacks = &(struct usipy_sip_tm_uac_callbacks){0},
              }, &other_index), USIPY_SIP_TM_OK);
            assert(other_index == invite_index);
        }
    }
    ev.type = USIPY_SIP_UA_EVENT_DISCONNECT;
    ASSERT_CALL_EQ(usipy_sip_ua_on_event(uap, &ev, &invite_index), USIPY_SIP_TM_OK);
    assert(usipy_sip_ua_get_state(uap) == USIPY_SIP_UA_STATE_DISCONNECTED);
    if (gone > 1) {
        txp = usipy_sip_tm_get_transaction(tm, invite_index);
        assert(txp != NULL && (txp->common.flags & USIPY_SIP_TM_F_ABANDONED) == 0);
    }
    assert(elog.count == 1 && elog.emits[0].type == USIPY_SIP_UA_EMIT_DISCONNECT);

    usipy_sip_msg_dtor(respp);
    usipy_sip_msg_dtor(reqp);
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
    test_ua_disconnect_after_abandon(0, 0);
    test_ua_disconnect_after_abandon(1, 0);
    test_ua_disconnect_after_abandon(1, 1);
    test_ua_disconnect_after_abandon(1, 2);
    test_ua_incoming_connect_bye(0);
    test_ua_incoming_connect_bye(1);
    test_ua_accepted_ack(USIPY_SIP_TM_TRANSPORT_UDP);
    test_ua_accepted_ack(USIPY_SIP_TM_TRANSPORT_TCP);
    test_ua_accepted_no_ack(USIPY_SIP_TM_TRANSPORT_UDP);
    test_ua_accepted_no_ack(USIPY_SIP_TM_TRANSPORT_TCP);
    test_ua_accepted_destroy();
    test_ua_accepted_hangup();
    test_ua_accepted_no_ack_kept();
    test_ua_accepted_send_fails();
    test_ua_accepted_no_ack_not_connected();
    test_ua_accepted_owner();
    test_tm_timer_policy();
    test_ua_accepted_timer_l_only();
    return (0);
}
