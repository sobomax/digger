/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

struct gamestate {
  int16_t nplayers,diggers,curplayer,startlev;
  bool levfflag;
  char levfname[132];
  char pldispbuf[14];
  int32_t randv;
  int8_t leveldat[8][10][15];
  int gtime;
  bool gauntlet, timeout, unlimlives;
  uint32_t ftime, cgtime;
};

extern struct gamestate dgstate;
