/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#ifndef __MONSTER_H
#define __MONSTER_H

struct digger_draw_api;

void domonsters(struct digger_draw_api *);
void incmont(int16_t n);
void erasemonsters(void);
void initmonsters(void);
int16_t monleft(void);
void killmon(int16_t mon);
int16_t killmonsters(int *clfirst,int *clcoll,bool spareplayer);
void checkmonscared(int16_t h);
void squashmonsters(int16_t bag,int *clfirst,int *clcoll);
bool monsbagstopped(struct digger_draw_api *ddap,int *clfirst,int *clcoll);
void mongold(void);

int16_t getfield(int16_t x,int16_t y);
void initmonlives(void);
bool monplayeralive(void);
bool monplayerholdsround(void);
bool monhauntover(void);
void monrestartround(bool bonusout);
int16_t getmonlives(void);
void addmonlife(void);

#endif
