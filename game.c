/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bags.h"
#include "digger.h"
#include "draw_api.h"
#include "drawing.h"
#include "input.h"
#include "main.h"
#include "monster.h"
#include "record.h"
#include "scores.h"
#include "sound.h"
#include "game.h"

/* Game state that is shared by multiple modules */
struct gamestate dgstate = {
  .nplayers = 1, .diggers = 1, .curplayer = 0, .startlev = 1,
  .levfflag = false, .randv = 0, .gtime = 0, .gauntlet = false,
  .timeout = false, .unlimlives = false, .flashplayer = false,
  .levnotdrawn = false, .alldead = false,
  .leveldat = {{"S   B     HHHHS",
                "V  CC  C  V B  ",
                "VB CC  C  V    ",
                "V  CCB CB V CCC",
                "V  CC  C  V CCC",
                "HH CC  C  V CCC",
                " V    B B V    ",
                " HHHH     V    ",
                "C   V     V   C",
                "CC  HHHHHHH  CC"},
               {"SHHHHH  B B  HS",
                " CC  V       V ",
                " CC  V CCCCC V ",
                "BCCB V CCCCC V ",
                "CCCC V       V ",
                "CCCC V B  HHHH ",
                " CC  V CC V    ",
                " BB  VCCCCV CC ",
                "C    V CC V CC ",
                "CC   HHHHHH    "},
               {"SHHHHB B BHHHHS",
                "CC  V C C V BB ",
                "C   V C C V CC ",
                " BB V C C VCCCC",
                "CCCCV C C VCCCC",
                "CCCCHHHHHHH CC ",
                " CC  C V C  CC ",
                " CC  C V C     ",
                "C    C V C    C",
                "CC   C H C   CC"},
               {"SHBCCCCBCCCCBHS",
                "CV  CCCCCCC  VC",
                "CHHH CCCCC HHHC",
                "C  V  CCC  V  C",
                "   HHH C HHH   ",
                "  B  V B V  B  ",
                "  C  VCCCV  C  ",
                " CCC HHHHH CCC ",
                "CCCCC CVC CCCCC",
                "CCCCC CHC CCCCC"},
               {"SHHHHHHHHHHHHHS",
                "VBCCCCBVCCCCCCV",
                "VCCCCCCV CCBC V",
                "V CCCC VCCBCCCV",
                "VCCCCCCV CCCC V",
                "V CCCC VBCCCCCV",
                "VCCBCCCV CCCC V",
                "V CCBC VCCCCCCV",
                "VCCCCCCVCCCCCCV",
                "HHHHHHHHHHHHHHH"},
               {"SHHHHHHHHHHHHHS",
                "VCBCCV V VCCBCV",
                "VCCC VBVBV CCCV",
                "VCCCHH V HHCCCV",
                "VCC V CVC V CCV",
                "VCCHH CVC HHCCV",
                "VC V CCVCC V CV",
                "VCHHBCCVCCBHHCV",
                "VCVCCCCVCCCCVCV",
                "HHHHHHHHHHHHHHH"},
               {"SHCCCCCVCCCCCHS",
                " VCBCBCVCBCBCV ",
                "BVCCCCCVCCCCCVB",
                "CHHCCCCVCCCCHHC",
                "CCV CCCVCCC VCC",
                "CCHHHCCVCCHHHCC",
                "CCCCV CVC VCCCC",
                "CCCCHH V HHCCCC",
                "CCCCCV V VCCCCC",
                "CCCCCHHHHHCCCCC"},
               {"HHHHHHHHHHHHHHS",
                "V CCBCCCCCBCC V",
                "HHHCCCCBCCCCHHH",
                "VBV CCCCCCC VBV",
                "VCHHHCCCCCHHHCV",
                "VCCBV CCC VBCCV",
                "VCCCHHHCHHHCCCV",
                "VCCCC V V CCCCV",
                "VCCCCCV VCCCCCV",
                "HHHHHHHHHHHHHHH"}}
};

extern struct digger_draw_api *ddap;

static struct game
{
  int16_t level;
  bool levdone;
} gamedat[2];

static int16_t penalty=0;

int16_t getlevch(int16_t x,int16_t y,int16_t l)
{
  if ((l==3 || l==4) && !dgstate.levfflag && dgstate.diggers==2 && y==9 && (x==6 || x==8))
    return 'H';
  return dgstate.leveldat[l-1][y][x];
}

int16_t levplan(void)
{
  int16_t l=levno();
  if (l>8)
    l=(l&3)+5; /* Level plan: 12345678, 678, (5678) 247 times, 5 forever */
  return l;
}

int16_t levof10(void)
{
  if (gamedat[dgstate.curplayer].level>10)
    return 10;
  return gamedat[dgstate.curplayer].level;
}

int16_t levno(void)
{
  return gamedat[dgstate.curplayer].level;
}

void setdead(bool df)
{
  dgstate.alldead=df;
}

static void initlevel(void)
{
  gamedat[dgstate.curplayer].levdone=false;
  makefield();
  makeemfield();
  initbags();
  dgstate.levnotdrawn=true;
}

static void checklevdone(void)
{
  if ((countem()==0 || monleft()==0) && isalive())
    gamedat[dgstate.curplayer].levdone=true;
  else
    gamedat[dgstate.curplayer].levdone=false;
}

void incpenalty(void)
{
  penalty++;
}

bool gamestep(void)
{
  penalty=0;
  dodigger(ddap);
  domonsters(ddap);
  dobags(ddap);
  if (penalty>8)
    incmont(penalty-8);
  testpause();
  checklevdone();
  return (!dgstate.alldead && !gamedat[dgstate.curplayer].levdone && !escape && !dgstate.timeout);
}

static void drawscreen(struct digger_draw_api *ddap)
{
  creatembspr();
  drawstatics(ddap);
  drawbags();
  drawemeralds();
  initdigger();
  initmonsters();
}

void drawlevel()
{
  int16_t t,c,i;
  if (!dgstate.levnotdrawn)
    return;
  dgstate.levnotdrawn=false;
  drawscreen(ddap);
  if (dgstate.flashplayer) {
    dgstate.flashplayer=false;
    strcpy(dgstate.pldispbuf,"PLAYER ");
    if (dgstate.curplayer==0)
      strcat(dgstate.pldispbuf,"1");
    else
      strcat(dgstate.pldispbuf,"2");
    cleartopline();
    for (t=0;t<15;t++)
      for (c=1;c<=3;c++) {
        outtext(ddap, dgstate.pldispbuf,108,0,c);
        writecurscore(ddap, c);
        newframe();
        if (escape)
          return;
      }
    drawscores(ddap);
    for (i=0;i<dgstate.diggers;i++)
      addscore(ddap, i,0);
  }
}

static void initchars(void)
{
  initmbspr();
  initdigger();
  initmonsters();
}

static int getalllives(void)
{
  int t=0,i;
  for (i=dgstate.curplayer;i<dgstate.diggers+dgstate.curplayer;i++)
    t+=getlives(i);
  return t;
}

void initgame(void)
{
  if (dgstate.gauntlet) {
    dgstate.cgtime=dgstate.gtime*1193181l;
    dgstate.timeout=false;
  }
  initlives();
  gamedat[0].level=dgstate.startlev;
  if (dgstate.nplayers==2)
    gamedat[1].level=dgstate.startlev;
  dgstate.alldead=false;
  ddap->gclear();
  dgstate.curplayer=0;
  initlevel();
  dgstate.curplayer=1;
  initlevel();
  zeroscores();
  bonusvisible=true;
  dgstate.flashplayer = (dgstate.nplayers==2) ? true : false;
  dgstate.curplayer=0;
}

void startlevel(void)
{
  initmbspr();

  if (playing)
    dgstate.randv=playgetrand();
  else
    dgstate.randv=0;
#ifdef INTDRF
  fprintf(info,"%lu\n",dgstate.randv);
  frame=0;
#endif
  recputrand(dgstate.randv);
  if (!dgstate.levnotdrawn)
    initchars();
  drawlevel();

  erasetext(ddap, 8, 108,0,3);
  initscores(ddap);
  drawlives(ddap);
  music(1, 1.0);

  flushkeybuf();
  for (int i=0;i<dgstate.diggers;i++)
    readdirect(i);
}

void game(void)
{
  int16_t t,i;
  initgame();
  while (getalllives()!=0 && !escape && !dgstate.timeout) {
    while (!dgstate.alldead && !escape && !dgstate.timeout) {
      startlevel();
      while (gamestep()) continue;
      erasediggers();
      musicoff();
      t=20;
      while ((getnmovingbags()!=0 || t!=0) && !escape && !dgstate.timeout) {
        if (t!=0)
          t--;
        penalty=0;
        dobags(ddap);
        dodigger(ddap);
        domonsters(ddap);
        if (penalty<8)
          t=0;
      }
      soundstop();
      for (i=0;i<dgstate.diggers;i++)
        killfire(i);
      erasebonus(ddap);
      cleanupbags();
      savefield();
      erasemonsters();
      recputeol();
      if (playing)
        playskipeol();
      if (escape)
        recputeog();
      if (gamedat[dgstate.curplayer].levdone) {
        soundlevdone();
        if (getenv("DIGGER_CI_RUN_DTL") != NULL) {
          game_dbg_info_emit();
        }
      }
      if (countem()==0 || gamedat[dgstate.curplayer].levdone) {
#ifdef INTDRF
        fprintf(info,"%i\n",frame);
#endif
        for (i=dgstate.curplayer;i<dgstate.diggers+dgstate.curplayer;i++)
          if (getlives(i)>0 && !digalive(i))
            declife(i);
        drawlives(ddap);
        gamedat[dgstate.curplayer].level++;
        if (gamedat[dgstate.curplayer].level>1000)
          gamedat[dgstate.curplayer].level=1000;
        initlevel();
      }
      else
        if (dgstate.alldead) {
#ifdef INTDRF
          fprintf(info,"%i\n",frame);
#endif
          for (i=dgstate.curplayer;i<dgstate.curplayer+dgstate.diggers;i++)
            if (getlives(i)>0)
              declife(i);
          drawlives(ddap);
        }
      if ((dgstate.alldead && getalllives()==0 && !dgstate.gauntlet && !escape) || dgstate.timeout)
        endofgame(ddap);
    }
    dgstate.alldead=false;
    if (dgstate.nplayers==2 && getlives(1-dgstate.curplayer)!=0) {
      dgstate.curplayer=1-dgstate.curplayer;
      dgstate.flashplayer=dgstate.levnotdrawn=true;
    }
  }
#ifdef INTDRF
  fprintf(info,"-1\n%lu\n%i",getscore0(),gamedat[0].level);
#endif
}
