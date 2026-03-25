/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef __NETSIM_DEBUG_H
#define __NETSIM_DEBUG_H

#include <stdint.h>

uint32_t debug_hash_mix(uint32_t h, uint32_t v);
uint32_t digger_debug_hash(void);
uint32_t bags_debug_hash(void);
uint32_t monster_debug_hash(void);
void netsim_trace_state(const char *phase, bool levdone, bool alldead,
  int penalty);

#endif
