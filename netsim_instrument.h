/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

/* NetSim test-only transport fault injection. */

#ifndef __NETSIM_INSTRUMENT_H
#define __NETSIM_INSTRUMENT_H

#ifdef DIGGER_INSTRUMENTATION
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void netsim_tx_mute(void);
bool netsim_tx_drop_check(size_t len);
uint32_t netsim_rx_drop_every_env(void);
#endif

#endif
