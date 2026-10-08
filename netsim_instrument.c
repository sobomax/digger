/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

/* NetSim test-only transport fault injection. */

#include "netsim_instrument.h"

#include <stdlib.h>

#ifdef DIGGER_INSTRUMENTATION
#include <stdatomic.h>
#include "digger_log.h"

/* Testing: nothing is sent any more, see netsim_tx_mute() */
static atomic_bool g_tx_muted;

/* Testing: send nothing from now on, as if gone */
void
netsim_tx_mute(void)
{

  if (!atomic_exchange(&g_tx_muted, true))
    digger_log_printf("netsim: muted, nothing is sent from now on\n");
}

bool
netsim_tx_drop_check(size_t len)
{
  static bool ready = false;
  static uint32_t drop_every = 0;
  static uint32_t drop_count = 0;

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
        digger_log_printf(
          "netsim: synthetic tx loss enabled, dropping every %uth packet\n",
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
  if (drop_count % drop_every != 0)
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
