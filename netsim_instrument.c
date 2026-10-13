/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

/* NetSim test-only transport fault injection. */

#include "netsim_instrument.h"

#include <stdlib.h>

#ifdef DIGGER_INSTRUMENTATION
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>
#include "def.h"
#include "digger_log.h"
#include "digger_math.h"
#include "netsim_platform.h"
#if NETSIM_PLATFORM_SUPPORTED
#include "microsippy/src/public/usipy_sip_tm.h"
#endif

/* Testing: with synthetic loss, how rarely a call or a frame may be lost
   for all of its packets lost (see loss_sends()): once in so many */
#define NETSIM_LOSS_ALL_LOST_ODDS 1000000000ULL

/* Testing: nothing is sent any more, see netsim_tx_mute() */
static atomic_bool g_tx_muted;

/* Testing: send nothing from now on, as if gone */
void
netsim_tx_mute(void)
{

  if (!atomic_exchange(&g_tx_muted, true))
    digger_log_printf("netsim: muted, nothing is sent from now on\n");
}

/* Testing: the synthetic loss asked for, as 1 packet in so many (the worse
   of the two ways, if both), 0 for none */
uint32_t
netsim_loss_every(void)
{
  static const char *const names[] = {"DIGGER_NETSIM_TX_DROP_EVERY",
    "DIGGER_NETSIM_RX_DROP_EVERY"};
  uint32_t every = 0;
  size_t i;

  for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    const char *envp = getenv(names[i]);
    char *endp;
    unsigned long v;

    if (envp == NULL || envp[0] == '\0')
      continue;
    v = strtoul(envp, &endp, 10);
    if (endp == envp || *endp != '\0' || v == 0 || v > UINT32_MAX)
      continue;
    if (every == 0 || v < every)
      every = (uint32_t)v;
  }
  return (every);
}

/*
 * Testing: the synthetic loss drops packets at random, one in so many on
 * average, rather than every so many: that would keep hitting the same one
 * of a message retransmitted just as often (a 200 OK every time, say). The
 * seed it goes by is DIGGER_NETSIM_LOSS_SEED, or a random one, told either
 * way for a run to be played again; salt makes one of it for each user.
 */
uint64_t
netsim_loss_seed(uint32_t salt)
{
  static bool ready = false;
  static uint32_t seed;

  if (!ready) {
    const char *envp;
    char *endp;
    unsigned long v;
    uint64_t ns;

    envp = getenv("DIGGER_NETSIM_LOSS_SEED");
    if (envp != NULL && envp[0] != '\0' &&
        (v = strtoul(envp, &endp, 10), endp != envp && *endp == '\0' &&
         v <= UINT32_MAX)) {
      seed = (uint32_t)v;
    } else {
#if NETSIM_PLATFORM_SUPPORTED
      ns = netsim_monotonic_ns();
#else
      ns = (uint64_t)time(NULL);
#endif
      ns ^= (uint64_t)(uintptr_t)&seed;
      seed = (uint32_t)(ns ^ (ns >> 32));
    }
    digger_log_printf("netsim: synthetic loss seed %u "
      "(DIGGER_NETSIM_LOSS_SEED)\n", (unsigned int)seed);
    ready = true;
  }
  return (((uint64_t)salt << 32) | seed);
}

/* Whether packet n of those the seed (see netsim_loss_seed()) is for is to
   go, one in every on average: by the game's splitmix64() */
bool
netsim_loss_roll(uint64_t seed, uint32_t n, uint32_t every)
{

  return (splitmix64(seed, n) % every == 0);
}

/* Testing: how many times a packet has to go for all of them to be lost
   once in NETSIM_LOSS_ALL_LOST_ODDS only, with the synthetic loss asked for;
   0 for none */
static uint32_t
loss_sends(void)
{
  uint64_t odds;
  uint32_t every, n;

  every = netsim_loss_every();
  if (every < 2)
    return (0);
  for (n = 1, odds = every; odds < NETSIM_LOSS_ALL_LOST_ODDS; n++)
    odds *= every;
  return (n);
}

/*
 * Testing: with synthetic loss a call is lost if all of its INVITEs are, or
 * all of the 200s to it: only 4 of each go out in the window_ms (5 s) they
 * have, T1 (500 ms) and then twice as long apart each time, lost all once
 * in some hundreds of calls at 1 in 5. Over the loopback the tests play on,
 * with next to no round trip time, they go out as soon (ms) as it takes for
 * that many of them to make it once in a billion calls (as many as the
 * tests' runs on all their platforms make are some thousands); 0 with no
 * such loss.
 */
uint32_t
netsim_loss_retry_sip(uint32_t window_ms)
{
  static int retry_ms = -1;
  uint32_t n, ms, t, d;

  if (retry_ms >= 0)
    return ((uint32_t)retry_ms);
  retry_ms = 0;
  if ((n = loss_sends()) == 0)
    return (0);
  ms = window_ms >> (n - 1);
  if (ms == 0)
    ms = 1;
  if (ms >= 500)
    return (0); /* RFC 3261's T1 does */
  retry_ms = (int)ms;
  /* How many go out in the time, as soon as it is (1 ms at least) */
  for (n = 1, t = 0, d = ms; t + d < window_ms; n++) {
    t += d;
    d *= 2;
  }
  digger_log_printf("netsim-sip: synthetic loss of 1 in %u: INVITEs and 200s "
    "sent again in %u ms, for %u of them in %u ms\n",
    (unsigned int)netsim_loss_every(), (unsigned int)ms, (unsigned int)n,
    (unsigned int)window_ms);
  return (ms);
}

#if NETSIM_PLATFORM_SUPPORTED
/*
 * Testing: with synthetic loss, all of a call's messages sent again sooner
 * (see netsim_loss_retry_sip()) by T1: its INVITE and the 200 to it, and
 * those of its dialog too (a BYE), that go by the INVITE's timers; how long
 * a transaction goes on (Timers F and J, 64*T1) as ever.
 */
void
netsim_loss_timers_sip(struct usipy_sip_tm_timer_policy *tp,
  uint32_t window_ms)
{
  uint32_t retry_ms;

  retry_ms = netsim_loss_retry_sip(window_ms);
  if (retry_ms == 0)
    return;
  tp->t1_ms = retry_ms;
  tp->timer_f_ms = 64 * USIPY_SIP_TM_TIMER_POLICY_RFC3261.t1_ms;
  tp->timer_j_ms = 64 * USIPY_SIP_TM_TIMER_POLICY_RFC3261.t1_ms;
}
#endif

/*
 * Testing: with synthetic loss a frame is as good as lost, and its game
 * with it, if all of the some 9 sends it gets in the sync timeout are (or
 * the peer's that ack it), which of the many frames of the tests' games
 * some will be. Over the loopback they play on, with next to no round trip
 * time, a frame is sent again sooner (in 1 ms at first) and so many times
 * in the sync timeout, for all of them to be lost once in a billion frames
 * only; 0 with no such loss.
 */
int
netsim_loss_frame_sends(void)
{
  static int sends = -1;

  if (sends >= 0)
    return (sends);
  sends = (int)loss_sends();
  if (sends != 0)
    digger_log_printf("netsim: synthetic loss of 1 in %u: a frame sent again "
      "in 1 ms at first, %d times in the sync timeout\n",
      (unsigned int)netsim_loss_every(), sends);
  return (sends);
}

bool
netsim_tx_drop_check(size_t len)
{
  static bool ready = false;
  static uint32_t drop_every = 0;
  static uint32_t drop_count = 0;
  static uint64_t loss_seed;

  if (!ready) {
    const char *envp;
    char *endp;
    unsigned long v;

    envp = getenv("DIGGER_NETSIM_TX_DROP_EVERY");
    if (envp != NULL && envp[0] != '\0') {
      v = strtoul(envp, &endp, 10);
      if (endp == envp || *endp != '\0' || v == 0 || v > UINT32_MAX) {
        digger_log_printf(
          "netsim: ignoring invalid DIGGER_NETSIM_TX_DROP_EVERY=%s\n", envp);
      } else {
        drop_every = (uint32_t)v;
        loss_seed = netsim_loss_seed(1);
        digger_log_printf(
          "netsim: synthetic tx loss enabled, dropping 1 in %u packets\n",
          (unsigned int)drop_every);
      }
    }
    ready = true;
  }
  if (atomic_load(&g_tx_muted))
    return (true);
  if (drop_every == 0)
    return (false);
  drop_count++;
  if (!netsim_loss_roll(loss_seed, drop_count, drop_every))
    return (false);
  digger_log_printf("netsim: synthetic tx drop packet=%u len=%u\n",
    (unsigned int)drop_count, (unsigned int)len);
  return (true);
}

uint32_t
netsim_rx_drop_every_env(void)
{
  const char *envp;
  char *endp;
  unsigned long v;

  envp = getenv("DIGGER_NETSIM_RX_DROP_EVERY");
  if (envp == NULL || envp[0] == '\0')
    return (0);
  v = strtoul(envp, &endp, 10);
  if (endp == envp || *endp != '\0' || v == 0 || v > UINT32_MAX) {
    digger_log_printf(
      "netsim-rx: ignoring invalid DIGGER_NETSIM_RX_DROP_EVERY=%s\n", envp);
    return (0);
  }
  return ((uint32_t)v);
}

#endif
