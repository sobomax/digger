/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef __EDRF_FEED_H
#define __EDRF_FEED_H

#include <stdbool.h>
#include <stdint.h>

#ifdef DIGGER_INSTRUMENTATION
extern bool edrf_feeding; /* Feeding a recording to a NetSim game */

bool edrf_netfeed_open(const char *name);
void edrf_feedslot(int slot);
uint8_t edrf_feedpeek(void);
void edrf_feed_checkinput(int slot, uint8_t bits, uint32_t recorded);
bool edrf_feed_quitting(int slot);
#else
#define edrf_feeding false
#endif

#endif
