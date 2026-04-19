#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "usipy_tm_uac.h"
#include "usipy_tm_uac_cli.h"

enum usipy_register_cli_id_mode {
    USIPY_REGISTER_CLI_ID_DEBUG = 0,
    USIPY_REGISTER_CLI_ID_PRODUCTION
};

struct usipy_register_cli_ctx {
    int sock;
    int stop;
    int error;
    int auth_retry_started;
    uint16_t final_status;
    unsigned int expires;
    struct usipy_sip_tm *tm;
    struct usipy_sip_tm_addr peer;
    struct usipy_sip_tm_addr local;
    struct usipy_str username;
    struct usipy_str password;
    struct usipy_str qop;
};

static void
usage(const char *argv0)
{
    fprintf(stderr,
      "usage: %s [--id-policy debug|production] [--expires seconds] [--port port] [--timeout-ms ms] server-ip username password\n",
      argv0);
}

static int
socket_send_to(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_sip_tm_outbound *outp)
{
    struct usipy_register_cli_ctx *ctx = arg;
    ssize_t sent;

    (void)tx_index;
    (void)txp;
    sent = send(ctx->sock, outp->raw.s.ro, outp->raw.l, 0);
    return (sent == (ssize_t)outp->raw.l ? 0 : -1);
}

static void
register_response(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  const struct usipy_msg *msg)
{
    struct usipy_register_cli_ctx *ctx = arg;
    const unsigned int scode = msg->sline.parsed.sl.status.code;

    (void)txp;
    ctx->final_status = (uint16_t)scode;
    if ((scode == 401 || scode == 407) && !ctx->auth_retry_started) {
        if (usipy_tm_uac_register_reply_auth(ctx->tm, tx_index, msg, &ctx->username,
          &ctx->password, &ctx->qop, NULL, 0) != USIPY_SIP_TM_OK) {
            ctx->error = 1;
            ctx->stop = 1;
            return;
        }
        ctx->auth_retry_started = 1;
        return;
    }
    if (scode >= 200 && scode < 300) {
        if (usipy_tm_uac_extract_register_expires(msg, &ctx->username,
          &ctx->expires) != 0) {
            ctx->error = 1;
        }
        ctx->stop = 1;
        return;
    }
    if (scode >= 200) {
        ctx->error = 1;
        ctx->stop = 1;
    }
}

static void
register_timeout(void *arg, size_t tx_index, const struct usipy_sip_tm_tx *txp,
  enum usipy_sip_tm_uac_timeout_id timeout_id)
{
    struct usipy_register_cli_ctx *ctx = arg;

    (void)tx_index;
    (void)txp;
    (void)timeout_id;
    ctx->error = 1;
    ctx->stop = 1;
}

int
main(int argc, char **argv)
{
    enum usipy_register_cli_id_mode id_mode = USIPY_REGISTER_CLI_ID_PRODUCTION;
    struct usipy_tm_uac_production_ids production_ids = {0};
    struct usipy_sip_tm_ctor_params tm_ctorp = {
      .transport = USIPY_SIP_TM_TRANSPORT_UDP,
      .max_transactions = 2,
    };
    struct usipy_register_cli_ctx ctx = {
      .sock = -1,
      .qop = (struct usipy_str)USIPY_2STR("auth"),
    };
    struct sockaddr_storage peer_ss;
    socklen_t peer_slen = 0;
    struct usipy_sip_tm_new_uac_tr_params tp = {0};
    struct usipy_sip_tm_run_in rin;
    struct usipy_sip_tm_run_out rout;
    struct usipy_sip_tm_handle_incoming_in hin;
    struct usipy_sip_tm_handle_incoming_out hout;
    struct pollfd pfd;
    struct usipy_sip_tm_tx const *txp;
    size_t tx_index = USIPY_SIP_TM_TX_INDEX_NONE;
    char uri_host[INET6_ADDRSTRLEN + 2];
    char request_uri_buf[128];
    char debug_call_id[96];
    char rxbuf[2048];
    const char *server_ip;
    const char *username;
    const char *password;
    struct usipy_str request_uri;
    struct usipy_str call_id;
    uint32_t expires = 300;
    uint32_t timeout_ms = 0;
    uint32_t port = 5060;
    uint64_t deadline_ms = USIPY_SIP_TM_TIME_NONE;
    int blen;
    int argi = 1;
    int rval = 1;

    while (argi < argc && strncmp(argv[argi], "--", 2) == 0) {
        if (strcmp(argv[argi], "--id-policy") == 0) {
            if (++argi >= argc) {
                usage(argv[0]);
                return (1);
            }
            if (strcmp(argv[argi], "debug") == 0) {
                id_mode = USIPY_REGISTER_CLI_ID_DEBUG;
            } else if (strcmp(argv[argi], "production") == 0) {
                id_mode = USIPY_REGISTER_CLI_ID_PRODUCTION;
            } else {
                usage(argv[0]);
                return (1);
            }
            argi++;
            continue;
        }
        if (strcmp(argv[argi], "--expires") == 0) {
            if (++argi >= argc ||
              usipy_tm_uac_cli_parse_u32(argv[argi], 1, UINT32_MAX, &expires) != 0) {
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
    server_ip = argv[argi++];
    username = argv[argi++];
    password = argv[argi++];
    if (usipy_tm_uac_cli_parse_target(server_ip, (uint16_t)port, &peer_ss, &peer_slen,
      &ctx.peer) != 0) {
        fprintf(stderr, "invalid server ip: %s\n", server_ip);
        return (1);
    }
    if (usipy_tm_uac_cli_format_uri_host(uri_host, sizeof(uri_host), &ctx.peer) < 0) {
        fprintf(stderr, "unable to format URI host\n");
        return (1);
    }
    blen = (port == 5060 ?
      snprintf(request_uri_buf, sizeof(request_uri_buf), "sip:%s", uri_host) :
      snprintf(request_uri_buf, sizeof(request_uri_buf), "sip:%s:%u", uri_host, port));
    if (blen < 0 || (size_t)blen >= sizeof(request_uri_buf)) {
        fprintf(stderr, "unable to format request URI\n");
        return (1);
    }
    request_uri = (struct usipy_str){.s.ro = request_uri_buf,
      .l = (size_t)blen};
    ctx.username = (struct usipy_str){.s.ro = username, .l = strlen(username)};
    ctx.password = (struct usipy_str){.s.ro = password, .l = strlen(password)};

    ctx.sock = socket(ctx.peer.af, SOCK_DGRAM, 0);
    if (ctx.sock < 0) {
        perror("socket");
        return (1);
    }
    if (connect(ctx.sock, (const struct sockaddr *)&peer_ss, peer_slen) != 0) {
        perror("connect");
        goto done;
    }
    if (id_mode == USIPY_REGISTER_CLI_ID_PRODUCTION) {
        if (usipy_tm_uac_production_ids_init(&production_ids) != 0) {
            fprintf(stderr, "unable to initialize production identifiers\n");
            goto done;
        }
        tm_ctorp.id_policy.arg = &production_ids;
        tm_ctorp.id_policy.cb = usipy_tm_uac_production_id_policy;
        call_id = production_ids.call_id_s;
    } else {
        blen = snprintf(debug_call_id, sizeof(debug_call_id), "reg-1@%s", server_ip);
        if (blen < 0 || (size_t)blen >= sizeof(debug_call_id)) {
            fprintf(stderr, "unable to initialize debug call-id\n");
            goto done;
        }
        call_id = (struct usipy_str){.s.ro = debug_call_id, .l = (size_t)blen};
    }
    tm_ctorp.sock = ctx.sock;
    if (id_mode != USIPY_REGISTER_CLI_ID_PRODUCTION) {
        memset(&tm_ctorp.id_policy, '\0', sizeof(tm_ctorp.id_policy));
    }
    ctx.tm = usipy_sip_tm_ctor(&tm_ctorp);
    if (ctx.tm == NULL) {
        fprintf(stderr, "unable to initialize SIP TM\n");
        goto done;
    }
    tp.request_id.call_id = call_id;
    tp.request_id.cseq = 1;
    tp.request_id.method_type = USIPY_SIP_METHOD_REGISTER;
    tp.request_target.request_uri = request_uri;
    tp.request_target.target = ctx.peer;
    tp.parties_by_username.from = ctx.username;
    tp.parties_by_username.to = ctx.username;
    tp.parties_by_username.contact = ctx.username;
    tp.contact_expires = expires;
    tp.callbacks.arg = &ctx;
    tp.callbacks.response = register_response;
    tp.callbacks.timeout = register_timeout;
    if (usipy_sip_tm_new_uac_tr(ctx.tm, &tp, &tx_index) != USIPY_SIP_TM_OK) {
        fprintf(stderr, "unable to create SIP transaction\n");
        goto done;
    }
    txp = usipy_sip_tm_get_transaction(ctx.tm, tx_index);
    if (txp == NULL) {
        fprintf(stderr, "unable to fetch SIP transaction\n");
        goto done;
    }
    ctx.local = txp->common.local;
    memset(&rin, '\0', sizeof(rin));
    rin.tm = ctx.tm;
    rin.send_to = socket_send_to;
    rin.send_to_arg = &ctx;
    memset(&hin, '\0', sizeof(hin));
    hin.tm = ctx.tm;
    hin.peer = ctx.peer;
    hin.local = ctx.local;
    if (timeout_ms != 0) {
        deadline_ms = usipy_tm_uac_mono_ms() + timeout_ms;
    }
    while (!ctx.stop) {
        uint64_t now_ms = usipy_tm_uac_mono_ms();
        int wait_ms;
        ssize_t nread;
        int poll_r;

        if (deadline_ms != USIPY_SIP_TM_TIME_NONE && now_ms >= deadline_ms) {
            fprintf(stderr, "register timeout\n");
            goto done;
        }
        rin.now_ms = now_ms;
        if (usipy_sip_tm_run(&rin, &rout) != USIPY_SIP_TM_OK) {
            fprintf(stderr, "tm run failed\n");
            goto done;
        }
        if (ctx.stop) {
            break;
        }
        wait_ms = -1;
        if (rout.next_run_at_ms != USIPY_SIP_TM_TIME_NONE) {
            uint64_t next_wait_ms = (rout.next_run_at_ms > now_ms) ?
              (rout.next_run_at_ms - now_ms) : 0;

            wait_ms = (int)next_wait_ms;
        }
        if (deadline_ms != USIPY_SIP_TM_TIME_NONE) {
            uint64_t deadline_wait_ms = (deadline_ms > now_ms) ? (deadline_ms - now_ms) : 0;

            if (wait_ms < 0 || (uint64_t)wait_ms > deadline_wait_ms) {
                wait_ms = (int)deadline_wait_ms;
            }
        }
        pfd.fd = ctx.sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll_r = poll(&pfd, 1, wait_ms);
        if (poll_r < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            goto done;
        }
        if (poll_r == 0 || (pfd.revents & POLLIN) == 0) {
            continue;
        }
        nread = recv(ctx.sock, rxbuf, sizeof(rxbuf), 0);
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            goto done;
        }
        hin.now_ms = usipy_tm_uac_mono_ms();
        hin.buf = rxbuf;
        hin.len = (size_t)nread;
        {
            const int in_rval = usipy_sip_tm_handle_incoming(&hin, &hout);

            if (in_rval != USIPY_SIP_TM_OK && in_rval != USIPY_SIP_TM_ERR_NOT_FOUND) {
                fprintf(stderr, "incoming SIP handling failed\n");
                goto done;
            }
        }
    }
    if (ctx.error || ctx.final_status < 200 || ctx.final_status >= 300) {
        fprintf(stderr, "register failed with status %u\n", ctx.final_status);
        goto done;
    }
    printf("%u\n", ctx.expires);
    rval = 0;

done:
    if (ctx.tm != NULL) {
        usipy_sip_tm_dtor(ctx.tm);
    }
    if (ctx.sock >= 0) {
        close(ctx.sock);
    }
    return (rval);
}
