/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool
netsim_configure(const char *spec)
{

  (void)spec;
  return (false);
}

bool
netsim_configured(void)
{

  return (false);
}

void
netsim_sync_enabled(bool enabled)
{

  (void)enabled;
}

bool
netsim_start_session(bool initiated_locally)
{

  (void)initiated_locally;
  return (false);
}

void
netsim_stop_session(bool send_exit)
{

  (void)send_exit;
}

void
netsim_shutdown(void)
{
}

bool
netsim_sync_frame(uint32_t frame, uint8_t local_bits, bool local_freeze,
  uint8_t *remote_bits, bool *remote_freeze, int *remote_lead_ms)
{

  (void)frame;
  (void)local_bits;
  (void)local_freeze;
  (void)remote_bits;
  if (remote_freeze != NULL)
    *remote_freeze = false;
  if (remote_lead_ms != NULL)
    *remote_lead_ms = 0;
  return (false);
}

int
netsim_local_player(void)
{

  return (0);
}

bool
netsim_session_active(void)
{

  return (false);
}

bool
netsim_peer_exited(void)
{

  return (false);
}

bool
netsim_pump_title_events(void)
{

  return (false);
}

bool
netsim_remote_start_requested(void)
{

  return (false);
}

enum netsim_title_status
netsim_title_status_get(void)
{

  return (NETSIM_TITLE_OFF);
}
