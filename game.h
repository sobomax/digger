/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

struct gamestate {
  int16_t nplayers,diggers,curplayer,startlev;
  bool levfflag;
  char levfname[132];
  char pldispbuf[14];
  int32_t randv;
  int netsim_remote_lead_ms;
  int8_t leveldat[8][MHEIGHT][MWIDTH + 1];
  int gtime;
  bool gauntlet, netsim, timeout, unlimlives;
  uint32_t ftime, cgtime;
  /* Game ticks so far: the game's own clock, as it goes on the same on a
     playback and on both NetSim peers, unlike the frames (see syncframe()) */
  uint32_t ticks;
};

extern struct gamestate dgstate;

void game_tick(void);
