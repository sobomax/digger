/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * NetSim from the game's side: each frame's controls exchanged with the
 * peer, and what the peer is up to.
 */

bool netsim_game_frame(uint32_t frame, bool local_freeze, bool local_pause,
  bool use_pause_latch, bool localquit, bool *remote_freezep,
  bool *remote_pausep);
bool netsim_remote_pause_active(void);
bool netsim_quit_synced(void);
int netsim_quitter(void);
void netsim_drain_frames(void);
bool netsim_draining(void);
