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
/* The player who quit it then (-1 for none, the peer gone as well) */
static int quitter = -1;
/* And the other one too, on the same frame */
static bool quit_both = false;
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

/* While waiting for the peer, see netsim_sync_frame(): the player quit (the
   exit key), or closed the window (where checkkeyb() pumping the events
   is all it takes) */
static bool
quit_requested(void)
{

  checkkeyb();
  return (escape);
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
        &remote_freeze, &remote_lead_ms, quit_requested)) {
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
    quitter = localquit ? local_player : remote_player;
    quit_both = localquit && (remote_bits & NETSIM_CTRL_QUIT) != 0;
  }
  setremote(remote_freeze, remote_pause, remote_freezep, remote_pausep);
  return (true);
}

/* Which player quit the game, both peers leaving it on the same frame, or
   -1 if it wasn't quit that way */
int
netsim_quitter(void)
{

  return (quitter);
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

  return (quitsynced);
}

/*
 * End the session of the game just over, both peers having left it on the
 * same frame (quit, or played to its end, game_over): just one of them
 * hangs up, the one that quit (player 1 if both did, or none), the other
 * waiting for its BYE, hanging up itself only if it doesn't come. Any
 * other way, it's hung up here.
 */
void
netsim_end_game_session(bool game_over)
{
  bool here;

  if (!dgstate.netsim)
    return;
  if (quitsynced)
    here = quit_both ? netsim_local_player() == 0 :
      quitter == netsim_local_player();
  else
    here = !game_over || netsim_local_player() == 0;
  if (!here)
    (void)netsim_await_peer_hangup();
  netsim_stop_session();
  quitsynced = false;
  quitter = -1;
  quit_both = false;
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
