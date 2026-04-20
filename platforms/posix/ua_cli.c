#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "public/usipy_sip_msg.h"
#include "public/usipy_sip_dialog.h"
#include "public/usipy_sip_ua.h"
#include "public/usipy_sip_tm_utils.h"
#include "public/usipy_sip_ua_utils.h"
#include "public/usipy_sip_response_utils.h"
#include "public/usipy_str.h"
#include "usipy_sip_method_db.h"
#include "usipy_sip_res.h"
#include "usipy_tvpair.h"
#include "usipy_sip_uri.h"
#include "usipy_tm_uac.h"
#include "usipy_tm_uac_cli.h"

enum ua_cli_id_mode {
    UA_CLI_ID_DEBUG = 0,
    UA_CLI_ID_PRODUCTION
};

struct ua_cli_ctx {
    int sock;
    int stop;
    int error;
    int report_activity;
    size_t max_transactions;
    struct usipy_sip_register_state reg;
    uint64_t hangup_at_ms;
    size_t invite_index;
    struct usipy_sip_tm *tm;
    struct usipy_sip_ua *uap;
    struct usipy_sip_tm_addr peer;
    struct usipy_sip_tm_addr local;
    struct usipy_str username;
    struct usipy_str password;
    struct usipy_str qop;
    struct usipy_str request_uri;
    struct usipy_str call_id;
    struct usipy_str sdp;
    int ua_reset_needed;
    int auto_hangup_pending;
    int stdin_closed;
    int stdin_line_overflow;
    uint32_t next_invite_cseq;
    uint16_t server_port;
    size_t pending_dial_len;
    size_t stdin_line_len;
    char server_uri_host[INET6_ADDRSTRLEN + 2];
    char pending_dial[512];
    char stdin_line[512];
    const char *stop_reason;
    char sdp_buf[512];
};

static const struct usipy_sip_status ua_cli_res_forbidden = {
  .code = 403,
  .reason_phrase = USIPY_2STR("Forbidden"),
};

static const struct usipy_sip_status ua_cli_res_not_found = {
  .code = 404,
  .reason_phrase = USIPY_2STR("Not Found"),
};

static void register_response(void *, size_t, const struct usipy_sip_tm_tx *,
  const struct usipy_msg *);
static void register_timeout(void *, size_t, const struct usipy_sip_tm_tx *,
  enum usipy_sip_tm_uac_timeout_id);
static void ua_emit(void *, const struct usipy_sip_ua_emit *);
static void outgoing_response(void *, size_t, const struct usipy_sip_tm_tx *,
  const struct usipy_msg *);
static void outgoing_timeout(void *, size_t, const struct usipy_sip_tm_tx *,
  enum usipy_sip_tm_uac_timeout_id);
static int apply_ua_reset(struct ua_cli_ctx *);

static const char *sip_tm_err_name(int);
static void format_first_line(char *, size_t, const char *, size_t);

static void
usage(const char *argv0)
{
    fprintf(stderr,
      "usage: %s [--activity] [--id-policy debug|production] [--expires seconds] [--port port] [--timeout-ms ms] server-ip username password\n",
      argv0);
}

static const char *
sip_tm_err_name(int err)
{
    switch (err) {
    case USIPY_SIP_TM_OK:
        return ("ok");

    case USIPY_SIP_TM_ERR_PARSE:
        return ("parse");

    case USIPY_SIP_TM_ERR_BADMSG:
        return ("badmsg");

    case USIPY_SIP_TM_ERR_NOT_FOUND:
        return ("not-found");

    case USIPY_SIP_TM_ERR_UNSUPPORTED:
        return ("unsupported");

    case USIPY_SIP_TM_ERR_INVAL:
        return ("invalid");

    case USIPY_SIP_TM_ERR_NOMEM:
        return ("nomem");

    case USIPY_SIP_TM_ERR_NOSPC:
        return ("nospc");
    }
    return ("unknown");
}

static void
format_first_line(char *buf, size_t len, const char *raw, size_t rawlen)
{
    size_t linelen;

    if (buf == NULL || len == 0) {
        return;
    }
    buf[0] = '\0';
    if (raw == NULL || rawlen == 0) {
        return;
    }
    linelen = 0;
    while (linelen < rawlen && raw[linelen] != '\r' && raw[linelen] != '\n') {
        linelen += 1;
    }
    if (linelen >= len) {
        linelen = len - 1;
    }
    memcpy(buf, raw, linelen);
    buf[linelen] = '\0';
}

static void
report_activityf(const struct ua_cli_ctx *ctx, const char *fmt, ...)
{
    va_list ap;

    if (ctx == NULL || !ctx->report_activity) {
        return;
    }
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

static int
socket_send_to(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_sip_tm_outbound *outp)
{
    const struct ua_cli_ctx *ctx = arg;
    ssize_t sent;

    (void)tx_index;
    (void)txp;
    sent = send(ctx->sock, outp->raw.s.ro, outp->raw.l, 0);
    return (sent == (ssize_t)outp->raw.l ? 0 : -1);
}

static int
run_tm_now(struct ua_cli_ctx *ctx, uint64_t now_ms, const char *stop_reason)
{
    struct usipy_sip_tm_run_in rin = {
      .now_ms = now_ms,
      .tm = ctx->tm,
      .send_to = socket_send_to,
      .send_to_arg = ctx,
    };
    struct usipy_sip_tm_run_out rout;

    if (usipy_sip_tm_run(&rin, &rout) != USIPY_SIP_TM_OK) {
        ctx->error = 1;
        ctx->stop = 1;
        ctx->stop_reason = stop_reason;
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    return (USIPY_SIP_TM_OK);
}

static int
build_fixed_sdp(struct ua_cli_ctx *ctx)
{
    const char *net_type;
    int blen;

    if (ctx == NULL) {
        return (-1);
    }
    net_type = ctx->local.af == AF_INET6 ? "IP6" : "IP4";
    blen = snprintf(ctx->sdp_buf, sizeof(ctx->sdp_buf),
      "v=0\r\n"
      "o=- 0 0 IN %s %.*s\r\n"
      "s=ua_cli\r\n"
      "c=IN %s %.*s\r\n"
      "t=0 0\r\n"
      "m=audio 40000 RTP/AVP 0\r\n"
      "a=rtpmap:0 PCMU/8000\r\n",
      net_type, USIPY_SFMT(&ctx->local.host), net_type, USIPY_SFMT(&ctx->local.host));
    if (blen < 0 || (size_t)blen >= sizeof(ctx->sdp_buf)) {
        return (-1);
    }
    ctx->sdp = (struct usipy_str){.s.ro = ctx->sdp_buf, .l = (size_t)blen};
    return (0);
}

static void
clear_call(struct ua_cli_ctx *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->invite_index != USIPY_SIP_TM_TX_INDEX_NONE) {
        const struct usipy_sip_tm_tx *txp = usipy_sip_tm_get_transaction(ctx->tm,
          ctx->invite_index);

        if (txp != NULL) {
            (void)usipy_sip_tm_drop_transaction(ctx->tm, ctx->invite_index);
        }
        ctx->invite_index = USIPY_SIP_TM_TX_INDEX_NONE;
    }
    ctx->hangup_at_ms = USIPY_SIP_TM_TIME_NONE;
    ctx->auto_hangup_pending = 0;
}

static int
start_pending_dial(struct ua_cli_ctx *ctx)
{
    struct usipy_sip_ua_event ev = {0};
    const struct usipy_str to_user = {
      .s.ro = ctx->pending_dial,
      .l = ctx->pending_dial_len,
    };
    char req_uri_buf[1024];
    size_t tx_index;
    int blen, rval;

    if (ctx == NULL || ctx->pending_dial_len == 0 || ctx->uap == NULL) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (usipy_sip_ua_get_state(ctx->uap) != USIPY_SIP_UA_STATE_IDLE) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    blen = (ctx->server_port == 5060 ?
      snprintf(req_uri_buf, sizeof(req_uri_buf), "sip:%s@%s",
        ctx->pending_dial, ctx->server_uri_host) :
      snprintf(req_uri_buf, sizeof(req_uri_buf), "sip:%s@%s:%u",
        ctx->pending_dial, ctx->server_uri_host, ctx->server_port));
    if (blen < 0 || (size_t)blen >= sizeof(req_uri_buf)) {
        report_activityf(ctx, "ignored dial target-too-long");
        ctx->pending_dial_len = 0;
        ctx->pending_dial[0] = '\0';
        return (USIPY_SIP_TM_OK);
    }
    ev.type = USIPY_SIP_UA_EVENT_DIAL;
    ev.data.dial = (struct usipy_sip_ua_dial_params){
      .request = {
        .request_id = {
        .cseq = ctx->next_invite_cseq++,
        .method_type = USIPY_SIP_METHOD_INVITE,
        },
        .request_target = {
        .request_uri = (struct usipy_str){.s.ro = req_uri_buf, .l = (size_t)strlen(req_uri_buf)},
        .target = ctx->peer,
        },
        .parties_by_username = {
        .from = ctx->username,
        .to = to_user,
        .contact = ctx->username,
        },
        .invite_expires = 60,
        .content_type = (struct usipy_str)USIPY_2STR("application/sdp"),
        .body = ctx->sdp,
        .callbacks = {
          .arg = ctx,
          .response = outgoing_response,
          .timeout = outgoing_timeout,
        },
      },
      .auth = {
        .username = ctx->username,
        .password = ctx->password,
        .qop = ctx->qop,
      },
    };
    rval = usipy_sip_ua_on_event(ctx->uap, &ev, &tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        report_activityf(ctx, "dial failed: %s (%d) target=%s",
          sip_tm_err_name(rval), rval, ctx->pending_dial);
        ctx->pending_dial_len = 0;
        ctx->pending_dial[0] = '\0';
        return (USIPY_SIP_TM_OK);
    }
    ctx->invite_index = tx_index;
    ctx->pending_dial_len = 0;
    ctx->pending_dial[0] = '\0';
    report_activityf(ctx, "dialing %.*s", USIPY_SFMT(&to_user));
    return (USIPY_SIP_TM_OK);
}

static int
queue_or_start_dial(struct ua_cli_ctx *ctx, const char *line, size_t len)
{
    struct usipy_sip_ua_event ev = {
      .type = USIPY_SIP_UA_EVENT_DISCONNECT,
    };
    enum usipy_sip_ua_state state;
    size_t tx_index;
    int rval;

    if (ctx == NULL || line == NULL || len == 0) {
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (len >= sizeof(ctx->pending_dial)) {
        report_activityf(ctx, "ignored dial line-too-long");
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    memcpy(ctx->pending_dial, line, len);
    ctx->pending_dial[len] = '\0';
    ctx->pending_dial_len = len;
    if (apply_ua_reset(ctx) != USIPY_SIP_TM_OK) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    if (ctx->uap == NULL) {
        return (USIPY_SIP_TM_ERR_UNSUPPORTED);
    }
    state = usipy_sip_ua_get_state(ctx->uap);
    if (state == USIPY_SIP_UA_STATE_IDLE) {
        return (start_pending_dial(ctx));
    }
    if (state == USIPY_SIP_UA_STATE_DISCONNECTED) {
        ctx->ua_reset_needed = 1;
        return (USIPY_SIP_TM_OK);
    }
    rval = usipy_sip_ua_on_event(ctx->uap, &ev, &tx_index);
    if (rval != USIPY_SIP_TM_OK) {
        report_activityf(ctx, "disconnect-before-dial failed: %s (%d)",
          sip_tm_err_name(rval), rval);
        return (rval);
    }
    return (USIPY_SIP_TM_OK);
}

static int
apply_ua_reset(struct ua_cli_ctx *ctx)
{
    if (ctx == NULL || !ctx->ua_reset_needed) {
        return (USIPY_SIP_TM_OK);
    }
    clear_call(ctx);
    if (usipy_sip_ua_reset(&ctx->uap, &(const struct usipy_sip_ua_ctor_params){
          .tm = ctx->tm,
          .emit = ua_emit,
          .emit_arg = ctx,
        }) != USIPY_SIP_TM_OK) {
        ctx->error = 1;
        ctx->stop = 1;
        ctx->stop_reason = "ua-reset";
        return (USIPY_SIP_TM_ERR_NOMEM);
    }
    ctx->ua_reset_needed = 0;
    return (USIPY_SIP_TM_OK);
}

static int
process_stdin(struct ua_cli_ctx *ctx)
{
    char buf[256];
    ssize_t nread;

    if (ctx == NULL || ctx->stdin_closed) {
        return (USIPY_SIP_TM_OK);
    }
    nread = read(STDIN_FILENO, buf, sizeof(buf));
    if (nread < 0) {
        if (errno == EINTR) {
            return (USIPY_SIP_TM_OK);
        }
        return (USIPY_SIP_TM_ERR_INVAL);
    }
    if (nread == 0) {
        ctx->stdin_closed = 1;
        return (USIPY_SIP_TM_OK);
    }
    for (ssize_t i = 0; i < nread; i++) {
        const char ch = buf[i];

        if (ctx->stdin_line_overflow) {
            if (ch == '\n') {
                ctx->stdin_line_overflow = 0;
                ctx->stdin_line_len = 0;
                report_activityf(ctx, "ignored dial line-too-long");
            }
            continue;
        }
        if (ch == '\r') {
            continue;
        }
        if (ch != '\n') {
            if (ctx->stdin_line_len >= sizeof(ctx->stdin_line) - 1) {
                ctx->stdin_line_overflow = 1;
                continue;
            }
            ctx->stdin_line[ctx->stdin_line_len++] = ch;
            continue;
        }
        while (ctx->stdin_line_len != 0 &&
          (ctx->stdin_line[ctx->stdin_line_len - 1] == ' ' ||
           ctx->stdin_line[ctx->stdin_line_len - 1] == '\t')) {
            ctx->stdin_line_len -= 1;
        }
        size_t off = 0;
        while (off < ctx->stdin_line_len &&
          (ctx->stdin_line[off] == ' ' || ctx->stdin_line[off] == '\t')) {
            off += 1;
        }
        if (ctx->stdin_line_len > off) {
            (void)queue_or_start_dial(ctx, ctx->stdin_line + off,
              ctx->stdin_line_len - off);
        }
        ctx->stdin_line_len = 0;
    }
    return (USIPY_SIP_TM_OK);
}

static void
ua_emit(void *arg, const struct usipy_sip_ua_emit *emitp)
{
    struct ua_cli_ctx *ctx = arg;
    struct usipy_sip_ua_event ev = {0};
    size_t tx_index;
    int rval;

    if (ctx == NULL || emitp == NULL) {
        return;
    }
    if (run_tm_now(ctx, usipy_tm_uac_mono_ms(), "ua-emit-run") != USIPY_SIP_TM_OK) {
        return;
    }
    switch (emitp->type) {
    case USIPY_SIP_UA_EMIT_DIAL:
        report_activityf(ctx, "incoming-call");
        ctx->invite_index = emitp->transaction_index;
        ev.type = USIPY_SIP_UA_EVENT_CONNECT;
        ev.data.response.status = usipy_sip_res_ok;
        ev.data.response.content_type = (struct usipy_str)USIPY_2STR("application/sdp");
        ev.data.response.body = ctx->sdp;
        rval = usipy_sip_ua_on_event(ctx->uap, &ev, &tx_index);
        if (rval != USIPY_SIP_TM_OK) {
            report_activityf(ctx,
              "incoming-call failed: answer %s (%d) tx=%zu active=%zu",
              sip_tm_err_name(rval), rval, emitp->transaction_index,
              usipy_sip_tm_nactive(ctx->tm));
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "invite-answer";
        }
        return;

    case USIPY_SIP_UA_EMIT_CONNECT:
        ctx->invite_index = emitp->transaction_index;
        ctx->hangup_at_ms = usipy_tm_uac_mono_ms() + 60000u;
        report_activityf(ctx, emitp->role == USIPY_SIP_TM_ROLE_UAS ?
          "answered" : "connected");
        return;

    case USIPY_SIP_UA_EMIT_DISCONNECT:
        if (ctx->auto_hangup_pending) {
            report_activityf(ctx, "hangup auto");
        } else if (emitp->message != NULL && emitp->message->kind == USIPY_SIP_MSG_REQ &&
          emitp->message->sline.parsed.rl.method->cantype == USIPY_SIP_METHOD_BYE) {
            report_activityf(ctx, "hangup remote");
        } else if (emitp->message != NULL && emitp->message->kind == USIPY_SIP_MSG_RES) {
            report_activityf(ctx, "call failed %u",
              emitp->message->sline.parsed.sl.status.code);
        } else {
            report_activityf(ctx, "hangup local");
        }
        ctx->auto_hangup_pending = 0;
        ctx->ua_reset_needed = 1;
        return;
    }
}

static void
outgoing_response(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
    struct ua_cli_ctx *ctx = arg;
    int rval;

    (void)txp;
    if (ctx == NULL || msg == NULL || msg->kind != USIPY_SIP_MSG_RES || ctx->uap == NULL) {
        return;
    }
    if (msg->sline.parsed.sl.status.code > 100 &&
      msg->sline.parsed.sl.status.code < 200) {
        report_activityf(ctx, "progress %u", msg->sline.parsed.sl.status.code);
    }
    rval = usipy_sip_ua_on_tx_response(ctx->uap, tx_index, msg);
    if (rval != USIPY_SIP_TM_OK) {
        report_activityf(ctx, "outgoing-response failed: %s (%d)",
          sip_tm_err_name(rval), rval);
        ctx->error = 1;
        ctx->stop = 1;
        ctx->stop_reason = "outgoing-response";
    }
}

static void
outgoing_timeout(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  enum usipy_sip_tm_uac_timeout_id timeout_id)
{
    struct ua_cli_ctx *ctx = arg;

    (void)tx_index;
    (void)txp;
    report_activityf(ctx, "call timeout %u", (unsigned int)timeout_id);
    ctx->ua_reset_needed = 1;
}

static int
start_register(struct ua_cli_ctx *ctx, size_t *indexp)
{
    report_activityf(ctx, "register cseq=%u", ctx->reg.next_cseq);
    return (usipy_sip_register_start(&ctx->reg,
      &(const struct usipy_sip_register_start_params){
        .tm = ctx->tm,
        .call_id = ctx->call_id,
        .request_uri = ctx->request_uri,
        .target = ctx->peer,
        .username = ctx->username,
        .callbacks = {
          .arg = ctx,
          .response = register_response,
          .timeout = register_timeout,
        },
      }, indexp));
}

static int
send_stateless_response(void *arg, const void *buf, size_t len)
{
    const struct ua_cli_ctx *ctx = arg;
    ssize_t sent;

    sent = send(ctx->sock, buf, len, 0);
    return (sent == (ssize_t)len ? 0 : -1);
}

static void
register_response(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
    struct ua_cli_ctx *ctx = arg;
    enum usipy_sip_register_response_result reg_rval;
    const unsigned int scode = msg->sline.parsed.sl.status.code;
    const int was_registered = ctx->reg.registered_once;

    if (usipy_sip_register_handle_response(&ctx->reg, ctx->tm, tx_index, txp, msg,
          &ctx->username, &ctx->password, &ctx->qop, usipy_tm_uac_mono_ms(),
          &reg_rval) != USIPY_SIP_TM_OK) {
        ctx->error = 1;
        ctx->stop = 1;
        ctx->stop_reason = "register-auth-build";
        return;
    }
    switch (reg_rval) {
    case USIPY_SIP_REGISTER_RESPONSE_AUTH_RETRY:
        report_activityf(ctx, "register challenge %u", scode);
        report_activityf(ctx, "register auth-retry");
        return;

    case USIPY_SIP_REGISTER_RESPONSE_ESTABLISHED:
        if (!was_registered) {
            printf("%u\n", ctx->reg.expires);
            fflush(stdout);
        }
        report_activityf(ctx, "registered %u", ctx->reg.expires);
        return;

    case USIPY_SIP_REGISTER_RESPONSE_FINAL:
        report_activityf(ctx, "register failed %u", scode);
        ctx->error = 1;
        ctx->stop = 1;
        ctx->stop_reason = "register-final";
        return;

    case USIPY_SIP_REGISTER_RESPONSE_PENDING:
    case USIPY_SIP_REGISTER_RESPONSE_ERROR:
        return;
    }
}

static void
register_timeout(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  enum usipy_sip_tm_uac_timeout_id timeout_id)
{
    struct ua_cli_ctx *ctx = arg;

    (void)tx_index;
    (void)txp;
    (void)timeout_id;
    usipy_sip_register_handle_timeout(&ctx->reg);
    report_activityf(ctx, "register timeout");
    ctx->error = 1;
    ctx->stop = 1;
    ctx->stop_reason = "register-timeout";
}

static void
incoming_request(void *arg, const struct usipy_sip_tm_handle_incoming_in *hin,
  const struct usipy_msg *msg)
{
    struct ua_cli_ctx *ctx = arg;
    uint8_t method_type;

    if (ctx == NULL || hin == NULL || msg == NULL || msg->kind != USIPY_SIP_MSG_REQ) {
        return;
    }
    if (apply_ua_reset(ctx) != USIPY_SIP_TM_OK) {
        return;
    }
    method_type = msg->sline.parsed.rl.method->cantype;
    if (!usipy_sip_tm_addr_same(&ctx->peer, &hin->peer)) {
        if (usipy_sip_tm_send_simple_response(ctx->tm, hin, msg,
              &ua_cli_res_forbidden) != USIPY_SIP_TM_OK) {
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "forbidden-response";
        } else {
            report_activityf(ctx, "rejected forbidden");
        }
        return;
    }
    if (ctx->uap != NULL && usipy_sip_ua_matches_transaction(ctx->uap, msg)) {
        struct usipy_sip_tm_new_uas_tr_params tp = {
          .request = msg,
          .timers = hin->timers,
          .peer = hin->peer,
          .local = hin->local,
        };
        size_t tx_index;
        int rval;

        rval = usipy_sip_tm_new_uas_tr(ctx->tm, &tp, &tx_index);
        if (rval != USIPY_SIP_TM_OK) {
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "dialog-new-uas";
            return;
        }
        rval = usipy_sip_ua_on_transaction(ctx->uap, tx_index, msg);
        if (rval != USIPY_SIP_TM_OK) {
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "dialog-handle";
            return;
        }
        return;
    }
    if (!usipy_sip_ua_request_targets_user(msg, &ctx->username)) {
        if (usipy_sip_tm_send_simple_response(ctx->tm, hin, msg,
              &ua_cli_res_not_found) != USIPY_SIP_TM_OK) {
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "not-found-response";
        } else {
            report_activityf(ctx, "rejected wrong-user");
        }
        return;
    }
    if (method_type == USIPY_SIP_METHOD_INVITE) {
        struct usipy_sip_tm_new_uas_tr_params tp = {
          .request = msg,
          .timers = hin->timers,
          .peer = hin->peer,
          .local = hin->local,
        };
        size_t tx_index;
        int rval;

        if (ctx->uap == NULL || usipy_sip_ua_get_state(ctx->uap) != USIPY_SIP_UA_STATE_IDLE) {
            if (usipy_sip_send_stateless_response(msg, &usipy_sip_res_busy_here,
                  send_stateless_response, ctx) !=
              USIPY_SIP_TM_OK) {
                ctx->error = 1;
                ctx->stop = 1;
                ctx->stop_reason = "busy-response";
            } else {
                report_activityf(ctx, "rejected busy");
            }
            return;
        }
        rval = usipy_sip_tm_new_uas_tr(ctx->tm, &tp, &tx_index);
        if (rval != USIPY_SIP_TM_OK) {
            report_activityf(ctx,
              "incoming-call failed: new-uas %s (%d) method=%.*s active=%zu",
              sip_tm_err_name(rval), rval,
              USIPY_SFMT(&msg->sline.parsed.rl.onwire.method),
              usipy_sip_tm_nactive(ctx->tm));
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "invite-new-uas";
            return;
        }
        rval = usipy_sip_ua_on_transaction(ctx->uap, tx_index, msg);
        if (rval != USIPY_SIP_TM_OK) {
            report_activityf(ctx,
              "incoming-call failed: ua %s (%d) method=%.*s tx=%zu active=%zu",
              sip_tm_err_name(rval), rval,
              USIPY_SFMT(&msg->sline.parsed.rl.onwire.method), tx_index,
              usipy_sip_tm_nactive(ctx->tm));
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "invite-ua";
        }
        return;
    }
    if (method_type == USIPY_SIP_METHOD_BYE) {
        if (usipy_sip_tm_send_simple_response(ctx->tm, hin, msg,
              &ua_cli_res_not_found) != USIPY_SIP_TM_OK) {
            ctx->error = 1;
            ctx->stop = 1;
            ctx->stop_reason = "bye-not-found-response";
        } else {
            report_activityf(ctx, "rejected no-call");
        }
        return;
    }
    if (usipy_sip_send_stateless_response(msg, &usipy_sip_res_not_impl,
          send_stateless_response, ctx) != USIPY_SIP_TM_OK) {
        ctx->error = 1;
        ctx->stop = 1;
        ctx->stop_reason = "unsupported-request";
    } else {
        report_activityf(ctx, "rejected unsupported: %.*s",
          USIPY_SFMT(&msg->sline.parsed.rl.onwire.method));
    }
}

int
main(int argc, char **argv)
{
    enum ua_cli_id_mode id_mode = UA_CLI_ID_PRODUCTION;
    struct usipy_tm_uac_production_ids production_ids = {0};
    struct ua_cli_ctx ctx = {
      .sock = -1,
      .max_transactions = 32,
      .qop = (struct usipy_str)USIPY_2STR("auth"),
      .reg = {
        .next_cseq = 1,
        .next_refresh_at_ms = USIPY_SIP_TM_TIME_NONE,
      },
      .next_invite_cseq = 1,
      .hangup_at_ms = USIPY_SIP_TM_TIME_NONE,
      .invite_index = USIPY_SIP_TM_TX_INDEX_NONE,
    };
    struct usipy_sip_tm_ctor_params tm_ctorp = {
      .transport = USIPY_SIP_TM_TRANSPORT_UDP,
      .max_transactions = ctx.max_transactions,
      .callbacks = {
        .arg = &ctx,
        .incoming_request = incoming_request,
      },
    };
    struct sockaddr_storage peer_ss;
    socklen_t peer_slen = 0;
    struct usipy_sip_tm_run_in rin;
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin;
    struct usipy_sip_tm_handle_incoming_out hout;
    struct pollfd pfds[2];
    const struct usipy_sip_tm_tx *txp;
    size_t reg_index;
    char uri_host[INET6_ADDRSTRLEN + 2];
    char request_uri_buf[128];
    char debug_call_id[96];
    char rxbuf[2048];
    const char *server_ip;
    const char *username;
    const char *password;
    uint32_t timeout_ms = 0;
    uint32_t port = 5060;
    uint64_t register_deadline_ms = USIPY_SIP_TM_TIME_NONE;
    const char *exit_reason = "normal";
    int blen;
    int argi = 1;
    int rval = 1;

    while (argi < argc && strncmp(argv[argi], "--", 2) == 0) {
        if (strcmp(argv[argi], "--activity") == 0) {
            ctx.report_activity = 1;
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--id-policy") == 0) {
            if (++argi >= argc) {
                usage(argv[0]);
                return (1);
            }
            if (strcmp(argv[argi], "debug") == 0) {
                id_mode = UA_CLI_ID_DEBUG;
            } else if (strcmp(argv[argi], "production") == 0) {
                id_mode = UA_CLI_ID_PRODUCTION;
            } else {
                usage(argv[0]);
                return (1);
            }
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--expires") == 0) {
            if (++argi >= argc ||
              usipy_tm_uac_cli_parse_u32(argv[argi], 1, UINT32_MAX,
                &ctx.reg.requested_expires) != 0) {
                usage(argv[0]);
                return (1);
            }
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--port") == 0) {
            if (++argi >= argc ||
              usipy_tm_uac_cli_parse_u32(argv[argi], 1, 65535, &port) != 0) {
                usage(argv[0]);
                return (1);
            }
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--timeout-ms") == 0) {
            if (++argi >= argc ||
              usipy_tm_uac_cli_parse_u32(argv[argi], 1, UINT32_MAX, &timeout_ms) != 0) {
                usage(argv[0]);
                return (1);
            }
            argi++;
            continue;
        }
        usage(argv[0]);
        return (1);
    }
    if (argc - argi != 3) {
        usage(argv[0]);
        return (1);
    }
    if (ctx.reg.requested_expires == 0) {
        ctx.reg.requested_expires = 300;
    }
    server_ip = argv[argi++];
    username = argv[argi++];
    password = argv[argi++];
    ctx.username = (struct usipy_str){.s.ro = username, .l = strlen(username)};
    ctx.password = (struct usipy_str){.s.ro = password, .l = strlen(password)};
    if (usipy_tm_uac_cli_parse_target(server_ip, (uint16_t)port, &peer_ss, &peer_slen,
      &ctx.peer) != 0) {
        fprintf(stderr, "invalid server ip: %s\n", server_ip);
        return (1);
    }
    if (usipy_tm_uac_cli_format_uri_host(uri_host, sizeof(uri_host), &ctx.peer) < 0) {
        fprintf(stderr, "unable to format URI host\n");
        return (1);
    }
    memcpy(ctx.server_uri_host, uri_host, strlen(uri_host) + 1);
    ctx.server_port = (uint16_t)port;
    blen = (port == 5060 ?
      snprintf(request_uri_buf, sizeof(request_uri_buf), "sip:%s", uri_host) :
      snprintf(request_uri_buf, sizeof(request_uri_buf), "sip:%s:%u", uri_host, port));
    if (blen < 0 || (size_t)blen >= sizeof(request_uri_buf)) {
        fprintf(stderr, "unable to format request URI\n");
        return (1);
    }
    ctx.request_uri = (struct usipy_str){.s.ro = request_uri_buf, .l = (size_t)blen};
    ctx.sock = socket(ctx.peer.af, SOCK_DGRAM, 0);
    if (ctx.sock < 0) {
        perror("socket");
        return (1);
    }
    if (connect(ctx.sock, (const struct sockaddr *)&peer_ss, peer_slen) != 0) {
        perror("connect");
        exit_reason = "connect";
        goto done;
    }
    if (id_mode == UA_CLI_ID_PRODUCTION) {
        if (usipy_tm_uac_production_ids_init(&production_ids) != 0) {
            fprintf(stderr, "unable to initialize production identifiers\n");
            exit_reason = "id-init";
            goto done;
        }
        tm_ctorp.id_policy.arg = &production_ids;
        tm_ctorp.id_policy.cb = usipy_tm_uac_production_id_policy;
        ctx.call_id = production_ids.call_id_s;
    } else {
        blen = snprintf(debug_call_id, sizeof(debug_call_id), "ua-1@%s", server_ip);
        if (blen < 0 || (size_t)blen >= sizeof(debug_call_id)) {
            fprintf(stderr, "unable to initialize debug call-id\n");
            exit_reason = "debug-call-id";
            goto done;
        }
        ctx.call_id = (struct usipy_str){.s.ro = debug_call_id, .l = (size_t)blen};
    }
    tm_ctorp.sock = ctx.sock;
    if (id_mode != UA_CLI_ID_PRODUCTION) {
        memset(&tm_ctorp.id_policy, '\0', sizeof(tm_ctorp.id_policy));
    }
    ctx.tm = usipy_sip_tm_ctor(&tm_ctorp);
    if (ctx.tm == NULL) {
        fprintf(stderr, "unable to initialize SIP TM\n");
        exit_reason = "tm-ctor";
        goto done;
    }
    if (usipy_sip_ua_reset(&ctx.uap, &(const struct usipy_sip_ua_ctor_params){
          .tm = ctx.tm,
          .emit = ua_emit,
          .emit_arg = &ctx,
        }) != USIPY_SIP_TM_OK) {
        fprintf(stderr, "unable to initialize SIP UA\n");
        exit_reason = "ua-ctor";
        goto done;
    }
    if (start_register(&ctx, &reg_index) != USIPY_SIP_TM_OK) {
        fprintf(stderr, "unable to create REGISTER transaction\n");
        exit_reason = "register-start";
        goto done;
    }
    txp = usipy_sip_tm_get_transaction(ctx.tm, reg_index);
    if (txp == NULL) {
        fprintf(stderr, "unable to fetch REGISTER transaction\n");
        exit_reason = "register-fetch";
        goto done;
    }
    ctx.local = txp->common.local;
    if (build_fixed_sdp(&ctx) != 0) {
        fprintf(stderr, "unable to build fixed SDP\n");
        exit_reason = "build-sdp";
        goto done;
    }
    memset(&rin, '\0', sizeof(rin));
    rin.tm = ctx.tm;
    rin.send_to = socket_send_to;
    rin.send_to_arg = &ctx;
    memset(&hin, '\0', sizeof(hin));
    hin.tm = ctx.tm;
    hin.peer = ctx.peer;
    hin.local = ctx.local;
    if (timeout_ms != 0) {
        register_deadline_ms = usipy_tm_uac_mono_ms() + timeout_ms;
    }
    while (!ctx.stop) {
        uint64_t now_ms = usipy_tm_uac_mono_ms();
        uint64_t wake_at_ms = USIPY_SIP_TM_TIME_NONE;
        int wait_ms;
        ssize_t nread;
        int poll_r;

        if (apply_ua_reset(&ctx) != USIPY_SIP_TM_OK) {
            exit_reason = "ua-reset";
            goto done;
        }
        if (ctx.pending_dial_len != 0 &&
          usipy_sip_ua_get_state(ctx.uap) == USIPY_SIP_UA_STATE_IDLE &&
          start_pending_dial(&ctx) != USIPY_SIP_TM_OK) {
            exit_reason = "dial-start";
            goto done;
        }
        if (!ctx.reg.registered_once && register_deadline_ms != USIPY_SIP_TM_TIME_NONE &&
          now_ms >= register_deadline_ms) {
            fprintf(stderr, "register timeout\n");
            exit_reason = "register-deadline";
            goto done;
        }
        if (!ctx.reg.registering && ctx.reg.next_refresh_at_ms != USIPY_SIP_TM_TIME_NONE &&
          ctx.reg.next_refresh_at_ms <= now_ms &&
          start_register(&ctx, &reg_index) != USIPY_SIP_TM_OK) {
            fprintf(stderr, "unable to refresh registration\n");
            exit_reason = "refresh-start";
            goto done;
        }
        if (ctx.uap != NULL && ctx.hangup_at_ms != USIPY_SIP_TM_TIME_NONE &&
          ctx.hangup_at_ms <= now_ms) {
            struct usipy_sip_ua_event ev = {
              .type = USIPY_SIP_UA_EVENT_DISCONNECT,
            };
            size_t bye_index;

            ctx.auto_hangup_pending = 1;
            if (usipy_sip_ua_on_event(ctx.uap, &ev, &bye_index) != USIPY_SIP_TM_OK) {
                fprintf(stderr, "unable to send BYE\n");
                exit_reason = "auto-bye";
                goto done;
            }
            usipy_sip_tm_reap_terminated(ctx.tm);
            continue;
        }
        rin.now_ms = now_ms;
        if (usipy_sip_tm_run(&rin, &rout) != USIPY_SIP_TM_OK) {
            fprintf(stderr, "tm run failed\n");
            exit_reason = "tm-run";
            goto done;
        }
        usipy_sip_tm_reap_terminated(ctx.tm);
        if (ctx.stop) {
            break;
        }
        if (rout.next_run_at_ms != USIPY_SIP_TM_TIME_NONE) {
            wake_at_ms = rout.next_run_at_ms;
        }
        if (!ctx.reg.registered_once && register_deadline_ms != USIPY_SIP_TM_TIME_NONE &&
          (wake_at_ms == USIPY_SIP_TM_TIME_NONE || register_deadline_ms < wake_at_ms)) {
            wake_at_ms = register_deadline_ms;
        }
        if (!ctx.reg.registering && ctx.reg.next_refresh_at_ms != USIPY_SIP_TM_TIME_NONE &&
          (wake_at_ms == USIPY_SIP_TM_TIME_NONE || ctx.reg.next_refresh_at_ms < wake_at_ms)) {
            wake_at_ms = ctx.reg.next_refresh_at_ms;
        }
        if (ctx.uap != NULL && ctx.hangup_at_ms != USIPY_SIP_TM_TIME_NONE &&
          (wake_at_ms == USIPY_SIP_TM_TIME_NONE || ctx.hangup_at_ms < wake_at_ms)) {
            wake_at_ms = ctx.hangup_at_ms;
        }
        wait_ms = -1;
        if (wake_at_ms != USIPY_SIP_TM_TIME_NONE) {
            wait_ms = (wake_at_ms > now_ms) ? (int)(wake_at_ms - now_ms) : 0;
        }
        pfds[0].fd = ctx.sock;
        pfds[0].events = POLLIN;
        pfds[0].revents = 0;
        pfds[1].fd = STDIN_FILENO;
        pfds[1].events = (ctx.stdin_closed ? 0 : POLLIN);
        pfds[1].revents = 0;
        poll_r = poll(pfds, ctx.stdin_closed ? 1 : 2, wait_ms);
        if (poll_r < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            exit_reason = "poll";
            goto done;
        }
        if (poll_r == 0) {
            continue;
        }
        if (!ctx.stdin_closed && (pfds[1].revents & POLLIN) != 0) {
            if (process_stdin(&ctx) != USIPY_SIP_TM_OK) {
                perror("read");
                exit_reason = "stdin-read";
                goto done;
            }
            if (ctx.stop) {
                break;
            }
        }
        if ((pfds[0].revents & POLLIN) == 0) {
            continue;
        }
        nread = recv(ctx.sock, rxbuf, sizeof(rxbuf), 0);
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            exit_reason = "recv";
            goto done;
        }
        hin.now_ms = usipy_tm_uac_mono_ms();
        hin.buf = rxbuf;
        hin.len = (size_t)nread;
        {
            char first_line[160];
            const int in_rval = usipy_sip_tm_handle_incoming(&hin, &hout);

            if (in_rval != USIPY_SIP_TM_OK && hout.error != USIPY_SIP_TM_ERR_NOT_FOUND) {
                format_first_line(first_line, sizeof(first_line), rxbuf, (size_t)nread);
                if (in_rval == USIPY_SIP_TM_ERR_PARSE ||
                  in_rval == USIPY_SIP_TM_ERR_BADMSG ||
                  in_rval == USIPY_SIP_TM_ERR_UNSUPPORTED) {
                    report_activityf(&ctx,
                      "ignored incoming %s (%d) len=%zd first-line=\"%s\"",
                      sip_tm_err_name(in_rval), in_rval, nread, first_line);
                    continue;
                }
                fprintf(stderr,
                  "incoming SIP handling failed: %s (%d), match=%d, event=%d, tx=%zu, first-line=\"%s\"\n",
                  sip_tm_err_name(in_rval), in_rval, hout.match_kind, hout.event,
                  hout.transaction_index, first_line);
                exit_reason = "incoming-fatal";
                goto done;
            }
        }
        usipy_sip_tm_reap_terminated(ctx.tm);
    }
    if (ctx.error) {
        exit_reason = (ctx.stop_reason != NULL ? ctx.stop_reason : "callback-error");
        goto done;
    }
    rval = 0;

done:
    if (ctx.report_activity) {
        report_activityf(&ctx,
          "exit reason=%s stop=%d error=%d registered=%d registering=%d status=%u active=%zu",
          (ctx.stop_reason != NULL ? ctx.stop_reason : exit_reason), ctx.stop,
          ctx.error, ctx.reg.registered_once, ctx.reg.registering, ctx.reg.status,
          (ctx.tm != NULL ? usipy_sip_tm_nactive(ctx.tm) : 0));
    }
    clear_call(&ctx);
    if (ctx.uap != NULL) {
        usipy_sip_ua_dtor(ctx.uap);
    }
    if (ctx.tm != NULL) {
        usipy_sip_tm_dtor(ctx.tm);
    }
    if (ctx.sock >= 0) {
        close(ctx.sock);
    }
    return (rval);
}
