/* Digger Remastered
   Copyright (c) Maksym Sobolyev <sobomax@sippysoft.com> */

#pragma once

#include <stdbool.h>

struct digger_draw_api;

/*
 * NetSim's call on the title screen, before its game: the peer's put to our
 * player to take or not, or ours waiting for the peer's to take, which ours
 * may give up on.
 */

enum netsim_dialog_result {
  NETSIM_DIALOG_GO = 0, /* Taken: its game to start */
  NETSIM_DIALOG_DECLINED, /* Not taken, or given up on, by our player */
  NETSIM_DIALOG_FAILED /* Gone: the peer's hung up or declined, or failed */
};

enum netsim_dialog_result netsim_dialog_run(struct digger_draw_api *ddap,
  bool incoming);
