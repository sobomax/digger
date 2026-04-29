#include <sys/param.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>

#include "usipy_port/log.h"
#include "usipy_port/network.h"
#include "usipy_port/perftimer.h"

#include "usipy_debug.h"
#include "usipy_types.h"
#include "public/usipy_str.h"
#include "usipy_misc.h"
#include "usipy_sip_udp_task.h"
#include "public/usipy_msg_heap.h"
#include "public/usipy_sip_sline.h"
#include "public/usipy_sip_msg.h"
#include "usipy_sip_req.h"
#include "usipy_sip_res.h"
#include "usipy_sip_tid.h"
#include "usipy_sip_hdr.h"
#include "public/usipy_sip_hdr_types.h"
#include "usipy_sip_hdr_db.h"

#include <string.h>

#define MAX_UDP_SIZE 1472 /* MTU 1500, no fragmentation */

static uint16_t
usipy_sip_udp_task_hton16(uint16_t hp)
{

#if USIPY_BIGENDIAN
    return (hp);
#else
    return ((uint16_t)((hp << 8) | (hp >> 8)));
#endif
}

#define TIME_HDR_PARSE(hm, to) do { \
        timer_opbegin(&ods); \
        rval = usipy_sip_msg_parse_hdrs(msg, hm, to); \
        opd = timer_opend(&ods); \
        USIPY_LOGI(cfp->log_tag, "usipy_sip_msg_parse_hdrs(" #hm ", " #to ") = %d: took %u %s", \
          rval, opd, ods.dunit); \
    } while (0);

void *
usipy_sip_udp_task(void *pvParameters)
{
    char rx_buffer[MAX_UDP_SIZE];
    char addr_str[128];
    int addr_family;
    int ip_protocol;
    const struct usipy_sip_udp_task_conf *cfp;
    const struct usipy_sip_status notb = {
      .code = 666,
      .reason_phrase = USIPY_2STR("For it is a human number")
    };

    cfp = (struct usipy_sip_udp_task_conf *)pvParameters;
    while (1) {
        union {
            struct sockaddr_in v4;
#ifdef IPPROTO_IPV6
            struct sockaddr_in6 v6;
#endif
        } destAddr;
        if (cfp->sip_af == AF_INET) {
            destAddr.v4.sin_addr.s_addr = htonl(INADDR_ANY);
            destAddr.v4.sin_family = AF_INET;
            destAddr.v4.sin_port = usipy_sip_udp_task_hton16(cfp->sip_port);
            addr_family = AF_INET;
            ip_protocol = IPPROTO_IP;
            inet_ntop(AF_INET, &destAddr.v4.sin_addr, addr_str, sizeof(addr_str) - 1);
	} else {
#ifdef IPPROTO_IPV6
            bzero(&destAddr.v6.sin6_addr, sizeof(destAddr.v6.sin6_addr));
            destAddr.v6.sin6_family = AF_INET6;
            destAddr.v6.sin6_port = usipy_sip_udp_task_hton16(cfp->sip_port);
            addr_family = AF_INET6;
            ip_protocol = IPPROTO_IPV6;
            inet_ntop(AF_INET6, &destAddr.v6.sin6_addr, addr_str, sizeof(addr_str) - 1);
#else
            USIPY_LOGE(cfp->log_tag, "IPv6 is NOT compiled in");
            break;
#endif
        }

        int sock = socket(addr_family, SOCK_DGRAM, ip_protocol);
        if (sock < 0) {
            USIPY_LOGE(cfp->log_tag, "Unable to create socket: errno %d", errno);
            break;
        }
        USIPY_LOGI(cfp->log_tag, "Socket created");
        USIPY_LOGI(cfp->log_tag, "Binding UDP port %u", cfp->sip_port);

        int err;
        if (cfp->sip_af == AF_INET) {
            err = bind(sock, (struct sockaddr *)&destAddr.v4, sizeof(destAddr.v4));
        } else {
#ifdef IPPROTO_IPV6
            err = bind(sock, (struct sockaddr *)&destAddr.v6, sizeof(destAddr.v6));
#else
	    err = -1;
	    errno = EAFNOSUPPORT;
#endif
        }
        if (err < 0) {
            USIPY_LOGE(cfp->log_tag, "Socket unable to bind: errno %d", errno);
            break;
        }
        USIPY_LOGI(cfp->log_tag, "Socket binded");

        while (1) {

            USIPY_LOGI(cfp->log_tag, "Waiting for data");
            union {
                struct sockaddr_in v4;
#ifdef IPPROTO_IPV6
                struct sockaddr_in6 v6;
#endif
            } sourceAddr;

            socklen_t socklen;
            if (cfp->sip_af == AF_INET) {
                socklen = sizeof(sourceAddr.v4);
#ifdef IPPROTO_IPV6
            } else {
                socklen = sizeof(sourceAddr.v6);
#endif
            }
            int len = recvfrom(sock, rx_buffer, sizeof(rx_buffer) - 1, 0, (struct sockaddr *)&sourceAddr, &socklen);

            // Error occured during receiving
            if (len < 0) {
                USIPY_LOGE(cfp->log_tag, "recvfrom failed: errno %d", errno);
                break;
            }
            // Data received
            else {
                // Get the sender's ip address as string
                if (sourceAddr.v4.sin_family == AF_INET) {
                    inet_ntop(AF_INET, &sourceAddr.v4.sin_addr, addr_str, sizeof(addr_str) - 1);
#ifdef IPPROTO_IPV6
                } else if (sourceAddr.v6.sin6_family == AF_INET6) {
                    inet_ntop(AF_INET6, &sourceAddr.v6.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif
                } else {
                    USIPY_LOGE(cfp->log_tag, "recvfrom unknown AF: %d", sourceAddr.v4.sin_family);
                    break;
                }
                USIPY_LOGI(cfp->log_tag, "Received %d bytes from %s:", len, addr_str);
		USIPY_LOGI(cfp->log_tag, "%.*s", len, rx_buffer);

                struct usipy_msg_parse_err cerror = USIPY_MSG_PARSE_ERR_init;
                struct timer_opduration ods;
                unsigned int opd;
                int rval;

                timer_opbegin(&ods);
                struct usipy_msg *msg = usipy_sip_msg_ctor_fromwire(rx_buffer, len, &cerror);
                opd = timer_opend(&ods);
                if (msg != NULL) {
                    err = sendto(sock, rx_buffer, len, 0, (struct sockaddr *)&sourceAddr, socklen);
                    if (err == len && msg->kind == USIPY_SIP_MSG_REQ) {
                        struct usipy_msg *res;
                        res = usipy_sip_res_ctor_fromreq(msg, &notb);
                        if (res != NULL) {
                            err = sendto(sock, res->onwire.s.ro, res->onwire.l, 0,
                              (struct sockaddr *)&sourceAddr, socklen);
                            usipy_sip_msg_dtor(res);
                        }
                    }
                }
                USIPY_LOGI(cfp->log_tag, "usipy_sip_msg_ctor_fromwire() = %p: took %u %s",
                  msg, opd, ods.dunit);
                if (msg == NULL) {
                    continue;
                }

                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_VIA), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_CONTACT), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_CSEQ), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_CALLID), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_TO), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_FROM), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_ROUTE), 0);
                TIME_HDR_PARSE(USIPY_HFT_MASK(USIPY_HF_RECORDROUTE), 0);

                if (msg->kind == USIPY_SIP_MSG_REQ) {
                    timer_opbegin(&ods);
                    rval = usipy_sip_req_parse_ruri(msg);
                    opd = timer_opend(&ods);
                    USIPY_LOGI(cfp->log_tag, "usipy_sip_req_parse_ruri() = %d: took %u %s", rval,
                      opd, ods.dunit);
                }

                usipy_sip_msg_dump(msg, cfp->log_tag);
                usipy_sip_msg_dtor(msg);

                if (err < 0) {
                    USIPY_LOGE(cfp->log_tag, "Error occured during sending: errno %d", errno);
                    break;
                }
                cerror = USIPY_MSG_PARSE_ERR_init;

                msg = usipy_sip_msg_ctor_fromwire(rx_buffer, len, &cerror);
                if (msg == NULL) {
                    USIPY_DABORT();
                    continue;
                }

                struct usipy_sip_tid tid;
                timer_opbegin(&ods);
                rval = usipy_sip_msg_get_tid(msg, &tid);
                opd = timer_opend(&ods);
                USIPY_LOGI(cfp->log_tag, "usipy_sip_msg_get_tid() = %d: took %u %s", rval,
                  opd, ods.dunit);
                if (rval == 0) {
                    usipy_sip_tid_dump(&tid, cfp->log_tag, "  tid");
                }

                TIME_HDR_PARSE(USIPY_HF_TID_MASK, 1);
                if (msg->kind == USIPY_SIP_MSG_REQ) {
                    struct usipy_msg *res;
                    timer_opbegin(&ods);
                    res = usipy_sip_res_ctor_fromreq(msg, &notb);
                    opd = timer_opend(&ods);
                    USIPY_LOGI(cfp->log_tag, "usipy_sip_res_ctor_fromreq() = %p: "
                      "took %u %s", res, opd, ods.dunit);
                    if (res != NULL) {
                        usipy_sip_msg_dtor(res);
                    }
                }
                usipy_sip_msg_dtor(msg);
            }
        }

        if (sock != -1) {
            USIPY_LOGE(cfp->log_tag, "Shutting down socket and restarting...");
            shutdown(sock, 0);
            close(sock);
        }
    }
    cfp->faterr(cfp->faterr_arg);
    return (NULL);
}
