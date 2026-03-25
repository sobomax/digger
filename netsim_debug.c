/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include <stdlib.h>
#include <stdio.h>

#include "def.h"
#include "drawing.h"
#include "digger.h"
#include "monster.h"
#include "bags.h"
#include "game.h"
#include "netsim.h"
#include "digger_log.h"
#include "netsim_debug.h"

static bool netsim_trace_ready = false;
static bool netsim_trace_enabled = false;

uint32_t digger_debug_hash_append(uint32_t h);
uint32_t bags_debug_hash_append(uint32_t h);
uint32_t monster_debug_hash_append(uint32_t h);

uint32_t
debug_hash_mix(uint32_t h, uint32_t v)
{

  h ^= v + 0x9e3779b9U + (h << 6) + (h >> 2);
  return (h);
}

uint32_t
digger_debug_hash(void)
{

  return (digger_debug_hash_append(0x13579bdfU));
}

uint32_t
bags_debug_hash(void)
{

  return (bags_debug_hash_append(0x2468ace1U));
}

uint32_t
monster_debug_hash(void)
{

  return (monster_debug_hash_append(0x89abcdefU));
}

void
netsim_trace_state(const char *phase, bool levdone, bool alldead, int penalty)
{
  uint32_t field_hash, dig_hash, mon_hash, bag_hash;
  int i;

  if (!dgstate.netsim)
    return;
  if (!netsim_trace_ready) {
    netsim_trace_enabled = getenv("DIGGER_NETSIM_TRACE") != NULL;
    netsim_trace_ready = true;
  }
  if (!netsim_trace_enabled)
    return;
  field_hash = 0x10203040U;
  for (i = 0; i < MSIZE; i++)
    field_hash = debug_hash_mix(field_hash, (uint32_t)(uint16_t)field[i]);
  dig_hash = digger_debug_hash();
  mon_hash = monster_debug_hash();
  bag_hash = bags_debug_hash();
  digger_log_printf(
    "netsim-trace: lp=%d frame=%u phase=%s rand=%08x field=%08x dig=%08x mon=%08x bag=%08x levdone=%d alldead=%d timeout=%d cur=%d penalty=%d\n",
    netsim_local_player() + 1, (unsigned int)getframe(), phase,
    (unsigned int)dgstate.randv, (unsigned int)field_hash, (unsigned int)dig_hash,
    (unsigned int)mon_hash, (unsigned int)bag_hash, levdone ? 1 : 0,
    alldead ? 1 : 0, dgstate.timeout ? 1 : 0, dgstate.curplayer, penalty);
}
