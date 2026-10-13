/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

/* NetSim test-only transport fault injection. */

#ifndef __NETSIM_INSTRUMENT_H
#define __NETSIM_INSTRUMENT_H

#ifdef DIGGER_INSTRUMENTATION
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct usipy_sip_tm_timer_policy;

void netsim_tx_mute(void);
bool netsim_tx_drop_check(size_t len);
uint32_t netsim_rx_drop_every_env(void);
uint32_t netsim_loss_every(void);
uint32_t netsim_loss_retry_sip(uint32_t window_ms);
void netsim_loss_timers_sip(struct usipy_sip_tm_timer_policy *tp,
  uint32_t window_ms);
int netsim_loss_frame_sends(void);
uint64_t netsim_loss_seed(uint32_t salt);
bool netsim_loss_roll(uint64_t seed, uint32_t n, uint32_t every);
#endif

#endif
