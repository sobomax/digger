#include <sys/socket.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>

#include "usipy_types.h"
#include "usipy_sip_udp_task.h"

#include "usipy_stdout_watch.h"

#define EXAMPLE_SIP_PORT 5060

int
main(int argc, const char **argv)
{
    struct usipy_sip_udp_task_conf stc = {
      .sip_port = EXAMPLE_SIP_PORT,
      .log_tag = "usipy_test",
#ifdef CONFIG_EXAMPLE_IPV6
      .sip_af = AF_INET6
#else
      .sip_af = AF_INET
#endif
    };

#ifndef NO_THREADS
    pthread_t tmthread, swthread;

    assert(pthread_create(&tmthread, NULL, usipy_sip_udp_task, &stc) == 0);
    assert(pthread_create(&swthread, NULL, usipy_stdout_watch, NULL) == 0);
    return (pthread_join(tmthread, NULL));
#else
    usipy_sip_udp_task(&stc);
    return (0);
#endif
}
