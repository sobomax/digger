#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "usipy_tm_uac_cli.h"

int
usipy_tm_uac_cli_parse_u32(const char *arg, uint32_t minv, uint32_t maxv,
  uint32_t *outp)
{
    char *endp;
    unsigned long v;

    if (arg == NULL || outp == NULL) {
        return (-1);
    }
    errno = 0;
    v = strtoul(arg, &endp, 10);
    if (errno != 0 || endp == arg || *endp != '\0' || v < minv || v > maxv) {
        return (-1);
    }
    *outp = (uint32_t)v;
    return (0);
}

int
usipy_tm_uac_cli_parse_target(const char *ip, uint16_t port,
  struct sockaddr_storage *ss, socklen_t *slenp,
  struct usipy_sip_tm_addr *targetp)
{
    struct in_addr v4;
#ifdef AF_INET6
    struct in6_addr v6;
#endif

    memset(ss, '\0', sizeof(*ss));
    if (inet_pton(AF_INET, ip, &v4) == 1) {
        struct sockaddr_in *sin = (struct sockaddr_in *)ss;

        sin->sin_family = AF_INET;
        sin->sin_port = htons(port);
        sin->sin_addr = v4;
        *slenp = sizeof(*sin);
        targetp->af = AF_INET;
    } else {
#ifdef AF_INET6
        if (inet_pton(AF_INET6, ip, &v6) != 1) {
            return (-1);
        }
        {
            struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)ss;

            sin6->sin6_family = AF_INET6;
            sin6->sin6_port = htons(port);
            sin6->sin6_addr = v6;
            *slenp = sizeof(*sin6);
            targetp->af = AF_INET6;
        }
#else
        return (-1);
#endif
    }
    targetp->port = port;
    targetp->transport = USIPY_SIP_TM_TRANSPORT_UDP;
    targetp->host = (struct usipy_str){.s.ro = ip, .l = strlen(ip)};
    return (0);
}

int
usipy_tm_uac_cli_format_uri_host(char *buf, size_t len,
  const struct usipy_sip_tm_addr *targetp)
{
    int blen;

    if (targetp->af == AF_INET) {
        blen = snprintf(buf, len, "%.*s", USIPY_SFMT(&targetp->host));
        return ((blen < 0 || (size_t)blen >= len) ? -1 : blen);
    }
#ifdef AF_INET6
    if (targetp->af == AF_INET6) {
        blen = snprintf(buf, len, "[%.*s]", USIPY_SFMT(&targetp->host));
        return ((blen < 0 || (size_t)blen >= len) ? -1 : blen);
    }
#endif
    return (-1);
}
