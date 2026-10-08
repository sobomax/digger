/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include "netsim_debug.h"

#include <stdlib.h>

#ifdef DIGGER_INSTRUMENTATION
#include <stdio.h>

#include "def.h"
#include "drawing.h"
#include "digger.h"
#include "monster.h"
#include "bags.h"
#include "game.h"
#include "netsim.h"
#include "digger_log.h"
#include "state_hash.h"

static bool netsim_trace_ready = false;
static bool netsim_trace_enabled = false;

void
netsim_trace_state(const char *phase, bool levdone, bool alldead, int penalty)
{
  uint32_t fld_hash, dig_hash, mon_hash, bag_hash;

  if (!dgstate.netsim)
    return;
  if (!netsim_trace_ready) {
    netsim_trace_enabled = getenv("DIGGER_NETSIM_TRACE") != NULL;
    netsim_trace_ready = true;
  }
  if (!netsim_trace_enabled)
    return;
  fld_hash = field_hash();
  dig_hash = digger_debug_hash();
  mon_hash = monster_debug_hash();
  bag_hash = bags_debug_hash();
  digger_log_printf(
    "netsim-trace: lp=%d frame=%u phase=%s rand=%08x field=%08x dig=%08x mon=%08x bag=%08x levdone=%d alldead=%d timeout=%d cur=%d penalty=%d\n",
    netsim_local_player() + 1, (unsigned int)getframe(), phase,
    (unsigned int)dgstate.randv, (unsigned int)fld_hash, (unsigned int)dig_hash,
    (unsigned int)mon_hash, (unsigned int)bag_hash, levdone ? 1 : 0,
    alldead ? 1 : 0, dgstate.timeout ? 1 : 0, dgstate.curplayer, penalty);
}
#endif
