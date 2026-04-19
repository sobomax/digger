#ifndef _usipy_sip_udp_task_h
#define _usipy_sip_udp_task_h

#include <stdint.h>

#include "usipy_types.h"

DEFINE_RAW_METHOD(usipy_sip_udp_task_faterr, void, void *);

struct usipy_sip_udp_task_conf {
    uint16_t sip_port;
    int sip_af;
    const char *log_tag;
    usipy_sip_udp_task_faterr_f faterr;
    void *faterr_arg;
};

void *usipy_sip_udp_task(void *);

#endif
