/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#pragma once

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

int16_t getlevch(int16_t,int16_t,int16_t);
void gamestep(void);
int16_t levplan(void);
int16_t levno(void);
int16_t levof10(void);
void incpenalty(void);
void setdead(bool);
void game(void);
