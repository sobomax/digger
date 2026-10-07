/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#ifndef __NETSIM_DEBUG_H
#define __NETSIM_DEBUG_H

#include <stdbool.h>

void netsim_trace_state(const char *phase, bool levdone, bool alldead,
  int penalty);

#endif
