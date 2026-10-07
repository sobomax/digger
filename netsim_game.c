/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "def.h"
#include "digger.h"
#include "edrf.h"
#include "game.h"
#include "input.h"
#include "netsim.h"
#include "netsim_game.h"

/* The peer has the game paused, as of the last frame */
static bool remote_pause_active = false;
/* Both peers quit the game on the same frame, see netsim_game_frame() */
static bool quitsynced = false;
/* netsim_drain_frames() is running: the game is over already */
static bool draining = false;

/* What the peer is up to on this frame: frozen, paused (or neither,
   without a peer) */
static void
setremote(bool freeze, bool pause, bool *remote_freezep, bool *remote_pausep)
{

  remote_pause_active = pause;
  if (remote_freezep != NULL)
    *remote_freezep = freeze;
  if (remote_pausep != NULL)
    *remote_pausep = pause;
}

/*
 * The frame's controls (or a freeze, a pause, or quitting it: localquit)
 * to the peer, and the peer's back, see syncframe(). Returns false with
 * the peer gone.
 */
bool
netsim_game_frame(uint32_t frame, bool local_freeze, bool local_pause,
  bool use_pause_latch, bool localquit, bool *remote_freezep,
  bool *remote_pausep)
{
  uint8_t local_bits;
  uint8_t remote_bits;
  bool remote_freeze = false;
  bool remote_pause = false;
  int remote_lead_ms = 0;
  int local_player;
  int remote_player;

  dgstate.netsim_remote_lead_ms = 0;
  if (!dgstate.netsim || !netsim_session_active() || escape) {
    setremote(false, false, remote_freezep, remote_pausep);
    return (true);
  }
  local_player = netsim_local_player();
  remote_player = 1 - local_player;
  local_bits = input_snapshot_primary_controls();
  if (edrf_feeding)
    local_bits = edrf_feedpeek(); /* Replaying a recording over NetSim */
  if (local_pause ||
      (use_pause_latch && pausef && getlives(local_player) > 0))
    local_bits |= NETSIM_CTRL_PAUSE;
  if (localquit)
    local_bits |= NETSIM_CTRL_QUIT;
  if (!netsim_sync_frame(frame, local_bits, local_freeze, &remote_bits,
        &remote_freeze, &remote_lead_ms)) {
    escape = true;
    input_set_network_controls(local_player, 0);
    input_set_network_controls(remote_player, 0);
    setremote(false, false, remote_freezep, remote_pausep);
    return (false);
  }
  remote_pause = (remote_bits & NETSIM_CTRL_PAUSE) != 0;
  dgstate.netsim_remote_lead_ms = remote_lead_ms;
  input_set_network_controls(local_player,
    local_bits & ~(NETSIM_CTRL_PAUSE | NETSIM_CTRL_QUIT | NETSIM_CTRL_FREEZE));
  input_set_network_controls(remote_player,
    remote_bits & ~(NETSIM_CTRL_PAUSE | NETSIM_CTRL_QUIT | NETSIM_CTRL_FREEZE));
  if (localquit || (remote_bits & NETSIM_CTRL_QUIT) != 0) {
    escape = true;
    quitsynced = true;
  }
  setremote(remote_freeze, remote_pause, remote_freezep, remote_pausep);
  return (true);
}

bool
netsim_remote_pause_active(void)
{

  return (remote_pause_active);
}

/* Whether the game just over was quit by both peers on the same frame
   (and so, they can wait for each other), see netsim_game_frame() */
bool
netsim_quit_synced(void)
{
  bool q = quitsynced;

  quitsynced = false;
  return (q);
}

/*
 * The game is over here, but the peer may still miss the last frames we
 * sent: hold on to the session, sending frozen frames, until the peer is
 * done too (it freezes) or gone, so that ours can be sent again if lost.
 */
void
netsim_drain_frames(void)
{
  bool rfreeze = false, oescape = escape;

  if (!dgstate.netsim || !netsim_session_active())
    return;
  escape = false;
  draining = true;
  while (!rfreeze && !escape)
    if (!freezeframe(true, &rfreeze))
      break;
  draining = false;
  escape = oescape;
}

bool
netsim_draining(void)
{

  return (draining);
}
