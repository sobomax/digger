/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdlib.h>

#include "def.h"
#include "digger_types.h"
#include "monster.h"
#include "monster_obj.h"
#include "main.h"
#include "sprite.h"
#include "digger.h"
#include "drawing.h"
#include "bags.h"
#include "sound.h"
#include "scores.h"
#include "record.h"
#include "game.h"

static struct monster
{
  int16_t h,v,xr,yr,dir,t,hnt,death,bag,dtime,stime,chase;
  bool flag;
  struct monster_obj *mop;
} mondat[6];

static int16_t nextmonster=0,totalmonsters=0,maxmononscr=0,nextmontime=0,mongaptime=0;
static int16_t chase=0;

static bool unbonusflag=false;

static void createmonster(void);
static void monai(struct digger_draw_api *, int16_t mon);
static void mondie(struct digger_draw_api *, int16_t mon);
static bool fieldclear(int16_t dir,int16_t x,int16_t y);
static void squashmonster(int16_t mon,int16_t death,int16_t bag);
static int16_t nmononscr(void);

#define ISNOB(mop) (CALL_METHOD((mop), isnobbin))
#define ISHOB(mop) (!CALL_METHOD((mop), isnobbin))

void initmonsters(void)
{
  int16_t i;
  for (i=0;i<MONSTERS;i++)
    mondat[i].flag=false;
  nextmonster=0;
  mongaptime=45-(levof10()<<1);
  totalmonsters=levof10()+5;
  switch (levof10()) {
    case 1:
      maxmononscr=3;
      break;
    case 2:
    case 3:
    case 4:
    case 5:
    case 6:
    case 7:
      maxmononscr=4;
      break;
    case 8:
    case 9:
    case 10:
      maxmononscr=5;
  }
  nextmontime=10;
  unbonusflag=true;
}

void erasemonsters(void)
{
  int16_t i;
  for (i=0;i<MONSTERS;i++)
    if (mondat[i].flag)
      erasespr(i+FIRSTMONSTER);
}

void domonsters(struct digger_draw_api *ddap)
{
  int16_t i;
  if (nextmontime>0)
    nextmontime--;
  else {
    if (nextmonster<totalmonsters && nmononscr()<maxmononscr && isalive() &&
        !bonusmode)
      createmonster();
    if (unbonusflag && nextmonster==totalmonsters && nextmontime==0)
      if (isalive()) {
        unbonusflag=false;
        createbonus();
      }
  }
  for (i=0;i<MONSTERS;i++)
    if (mondat[i].flag) {
      if (mondat[i].hnt>10-levof10()) {
        if (ISNOB(mondat[i].mop)) {
          CALL_METHOD(mondat[i].mop, mutate);
          mondat[i].hnt=0;
        }
      }
      if (CALL_METHOD(mondat[i].mop, isalive))
        if (mondat[i].t==0) {
          monai(ddap, i);
          if (randno(15-levof10())==0) /* Need to split for determinism */
            if (ISNOB(mondat[i].mop) && CALL_METHOD(mondat[i].mop, isalive))
              monai(ddap, i);
        }
        else
          mondat[i].t--;
      else
        mondie(ddap, i);
    }
}

static void
createmonster(void)
{
  int16_t i;
  for (i=0;i<MONSTERS;i++)
    if (!mondat[i].flag) {
      mondat[i].flag=true;
      mondat[i].t=0;
      mondat[i].hnt=0;
      mondat[i].h=14;
      mondat[i].v=0;
      mondat[i].xr=0;
      mondat[i].yr=0;
      mondat[i].dir=DIR_LEFT;
      mondat[i].chase=chase+dgstate.curplayer;
      if (mondat[i].mop != NULL) {
        CALL_METHOD(mondat[i].mop, dtor);
      }
      mondat[i].mop = monster_obj_ctor(i, MON_NOBBIN, DIR_LEFT, 292, 18);
      chase=(chase+1)%dgstate.diggers;
      nextmonster++;
      nextmontime=mongaptime;
      mondat[i].stime=5;
      CALL_METHOD(mondat[i].mop, put);
      break;
    }
}

bool mongotgold=false;

void mongold(void)
{
  mongotgold=true;
}

static void
monai(struct digger_draw_api *ddap, int16_t mon)
{
  int16_t monox,monoy,dir,mdirp1,mdirp2,mdirp3,mdirp4,t;
  int clcoll[SPRITES],clfirst[TYPES],i,m,dig;
  struct obj_position mopos;
  bool push, bagf, mopos_changed;

  CALL_METHOD(mondat[mon].mop, getpos, &mopos);
  monox = mopos.x;
  monoy = mopos.y;
  if (mondat[mon].xr==0 && mondat[mon].yr==0) {

    /* If we are here the monster needs to know which way to turn next. */

    /* Turn hobbin back into nobbin if it's had its time */

    if (mondat[mon].hnt>30+(levof10()<<1))
      if (ISHOB(mondat[mon].mop)) {
        mondat[mon].hnt=0;
        CALL_METHOD(mondat[mon].mop, mutate);
      }

    /* Set up monster direction properties to chase Digger */

    dig=mondat[mon].chase;
    if (!digalive(dig))
      dig=(dgstate.diggers-1)-dig;

    if (abs(diggery(dig)-mopos.y)>abs(diggerx(dig)-mopos.x)) {
      if (diggery(dig)<mopos.y) { mdirp1=DIR_UP;    mdirp4=DIR_DOWN; }
                                 else { mdirp1=DIR_DOWN;  mdirp4=DIR_UP; }
      if (diggerx(dig)<mopos.x) { mdirp2=DIR_LEFT;  mdirp3=DIR_RIGHT; }
                                 else { mdirp2=DIR_RIGHT; mdirp3=DIR_LEFT; }
    }
    else {
      if (diggerx(dig)<mopos.x) { mdirp1=DIR_LEFT;  mdirp4=DIR_RIGHT; }
                                 else { mdirp1=DIR_RIGHT; mdirp4=DIR_LEFT; }
      if (diggery(dig)<mopos.y) { mdirp2=DIR_UP;    mdirp3=DIR_DOWN; }
                                 else { mdirp2=DIR_DOWN;  mdirp3=DIR_UP; }
    }

    /* In bonus mode, run away from Digger */

    if (bonusmode) {
      t=mdirp1; mdirp1=mdirp4; mdirp4=t;
      t=mdirp2; mdirp2=mdirp3; mdirp3=t;
    }

    /* Adjust priorities so that monsters don't reverse direction unless they
       really have to */

    dir=reversedir(mondat[mon].dir);
    if (dir==mdirp1) {
      mdirp1=mdirp2;
      mdirp2=mdirp3;
      mdirp3=mdirp4;
      mdirp4=dir;
    }
    if (dir==mdirp2) {
      mdirp2=mdirp3;
      mdirp3=mdirp4;
      mdirp4=dir;
    }
    if (dir==mdirp3) {
      mdirp3=mdirp4;
      mdirp4=dir;
    }

    /* Introduce a random element on levels <6 : occasionally swap p1 and p3 */

    if (randno(levof10()+5)==1) /* Need to split for determinism */
      if (levof10()<6) {
        t=mdirp1;
        mdirp1=mdirp3;
        mdirp3=t;
      }

    /* Check field and find direction */

    if (fieldclear(mdirp1,mondat[mon].h,mondat[mon].v))
      dir=mdirp1;
    else
      if (fieldclear(mdirp2,mondat[mon].h,mondat[mon].v))
        dir=mdirp2;
      else
        if (fieldclear(mdirp3,mondat[mon].h,mondat[mon].v))
          dir=mdirp3;
        else
          if (fieldclear(mdirp4,mondat[mon].h,mondat[mon].v))
            dir=mdirp4;

    /* Hobbins don't care about the field: they go where they want. */
    if (ISHOB(mondat[mon].mop))
      dir=mdirp1;

    /* Monsters take a time penalty for changing direction */

    if (mondat[mon].dir!=dir)
      mondat[mon].t++;

    /* Save the new direction */

    mondat[mon].dir=dir;
  }

  /* If monster is about to go off edge of screen, stop it. */

  if ((mopos.x==292 && mondat[mon].dir==DIR_RIGHT) ||
      (mopos.x==12 && mondat[mon].dir==DIR_LEFT) ||
      (mopos.y==180 && mondat[mon].dir==DIR_DOWN) ||
      (mopos.y==18 && mondat[mon].dir==DIR_UP))
    mondat[mon].dir=DIR_NONE;

  /* Change hdir for hobbin */

  if (mondat[mon].dir==DIR_LEFT || mondat[mon].dir==DIR_RIGHT) {
    mopos.dir=mondat[mon].dir;
    CALL_METHOD(mondat[mon].mop, setpos, &mopos);
  }

  /* Hobbins dig */

  if (ISHOB(mondat[mon].mop))
    eatfield(mopos.x, mopos.y, mondat[mon].dir);

  /* (Draw new tunnels) and move monster */
  mopos_changed = true;
  switch (mondat[mon].dir) {
    case DIR_RIGHT:
      if (ISHOB(mondat[mon].mop))
        drawrightblob(mopos.x, mopos.y);
      mopos.x += 4;
      break;
    case DIR_UP:
      if (ISHOB(mondat[mon].mop))
        drawtopblob(mopos.x, mopos.y);
      mopos.y -= 3;
      break;
    case DIR_LEFT:
      if (ISHOB(mondat[mon].mop))
        drawleftblob(mopos.x, mopos.y);
      mopos.x -= 4;
      break;
    case DIR_DOWN:
      if (ISHOB(mondat[mon].mop))
        drawbottomblob(mopos.x, mopos.y);
      mopos.y += 3;
      break;
    default:
      mopos_changed = false;
  }

  /* Hobbins can eat emeralds */
  if (ISHOB(mondat[mon].mop))
    hitemerald((mopos.x-12)/20,(mopos.y-18)/18,
               (mopos.x-12)%20,(mopos.y-18)%18,
               mondat[mon].dir);

  /* If Digger's gone, don't bother */
  if (!isalive() && mopos_changed) {
    mopos.x = monox;
    mopos.y = monoy;
    mopos_changed = false;
  }

  /* If monster's just started, don't move yet */

  if (mondat[mon].stime != 0) {
    mondat[mon].stime--;
    if (mopos_changed) {
      mopos.x = monox;
      mopos.y = monoy;
      mopos_changed = false;
    }
  }

  /* Increase time counter for hobbin */
  if (ISHOB(mondat[mon].mop) && mondat[mon].hnt < 100)
    mondat[mon].hnt++;

  if (mopos_changed) {
    CALL_METHOD(mondat[mon].mop, setpos, &mopos);
  }

  /* Draw monster */

  push=true;
  CALL_METHOD(mondat[mon].mop, animate);
  for (i=0;i<TYPES;i++)
    clfirst[i]=first[i];
  for (i=0;i<SPRITES;i++)
    clcoll[i]=coll[i];
  incpenalty();

  /* Collision with another monster */

  if (clfirst[2]!=-1) {
    mondat[mon].t++; /* Time penalty */
    /* Ensure both aren't moving in the same dir. */
    i=clfirst[2];
    do {
      m=i-FIRSTMONSTER;
      if (mondat[mon].dir==mondat[m].dir && mondat[m].stime==0 &&
          mondat[mon].stime==0)
        mondat[m].dir=reversedir(mondat[m].dir);
      /* The kludge here is to preserve playback for a bug in previous
         versions. */
      if (!kludge)
        incpenalty();
      else
        if (!(m&1))
          incpenalty();
      i=clcoll[i];
    } while (i!=-1);
    if (kludge)
      if (clfirst[0]!=-1)
        incpenalty();
  }

  /* Check for collision with bag */

  i=clfirst[1];
  bagf=false;
  while (i!=-1) {
    if (bagexist(i-FIRSTBAG)) {
      bagf=true;
      break;
    }
    i=clcoll[i];
  }

  if (bagf) {
    mondat[mon].t++; /* Time penalty */
    mongotgold=false;
    if (mondat[mon].dir==DIR_RIGHT || mondat[mon].dir==DIR_LEFT) { 
      push=pushbags(ddap, mondat[mon].dir,clfirst,clcoll);      /* Horizontal push */
      mondat[mon].t++; /* Time penalty */
    }
    else
      if (!pushudbags(ddap, clfirst,clcoll)) /* Vertical push */
        push=false;
    if (mongotgold) /* No time penalty if monster eats gold */
      mondat[mon].t=0;
    if (ISHOB(mondat[mon].mop) && mondat[mon].hnt>1)
      removebags(clfirst,clcoll); /* Hobbins eat bags */
  }

  /* Increase hobbin cross counter */

  if (ISNOB(mondat[mon].mop) && clfirst[2]!=-1 && isalive())
    mondat[mon].hnt++;

  /* See if bags push monster back */

  if (!push) {
    if (mopos_changed) {
      mopos.x = monox;
      mopos.y = monoy;
      CALL_METHOD(mondat[mon].mop, setpos, &mopos);
      mopos_changed = false;
    }
    CALL_METHOD(mondat[mon].mop, animate);
    incpenalty();
    if (ISNOB(mondat[mon].mop)) /* The other way to create hobbin: stuck on h-bag */
      mondat[mon].hnt++;
    if ((mondat[mon].dir==DIR_UP || mondat[mon].dir==DIR_DOWN) &&
        ISNOB(mondat[mon].mop))
      mondat[mon].dir=reversedir(mondat[mon].dir); /* If vertical, give up */
  }

  /* Collision with Digger */

  if (clfirst[4]!=-1 && isalive()) {
    if (bonusmode) {
      killmon(mon);
      i=clfirst[4];
      while (i!=-1) {
        if (digalive(i-FIRSTDIGGER+dgstate.curplayer))
          sceatm(ddap, i-FIRSTDIGGER+dgstate.curplayer);
        i=clcoll[i];
      }
      soundeatm(); /* Collision in bonus mode */
    }
    else {
      i=clfirst[4];
      while (i!=-1) {
        if (digalive(i-FIRSTDIGGER+dgstate.curplayer))
          killdigger(i-FIRSTDIGGER+dgstate.curplayer,3,0); /* Kill Digger */
        i=clcoll[i];
      }
    }
  }

  /* Update co-ordinates */

  mondat[mon].h=(mopos.x-12)/20;
  mondat[mon].v=(mopos.y-18)/18;
  mondat[mon].xr=(mopos.x-12)%20;
  mondat[mon].yr=(mopos.y-18)%18;
}

static void
mondie(struct digger_draw_api *ddap, int16_t mon)
{
  struct obj_position monpos;

  switch (mondat[mon].death) {
    case 1:
      CALL_METHOD(mondat[mon].mop, getpos, &monpos);
      if (bagy(mondat[mon].bag) + 6 > monpos.y) {
        monpos.y = bagy(mondat[mon].bag);
        CALL_METHOD(mondat[mon].mop, setpos, &monpos);
      }
      CALL_METHOD(mondat[mon].mop, animate);
      incpenalty();
      if (getbagdir(mondat[mon].bag)==-1) {
        mondat[mon].dtime=1;
        mondat[mon].death=4;
      }
      break;
    case 4:
      if (mondat[mon].dtime!=0)
        mondat[mon].dtime--;
      else {
        killmon(mon);
        if (dgstate.diggers==2)
          scorekill2(ddap);
        else
          scorekill(ddap, dgstate.curplayer);
      }
  }
}

static bool
fieldclear(int16_t dir,int16_t x,int16_t y)
{
  switch (dir) {
    case DIR_RIGHT:
      if (x<14)
        if ((getfield(x+1,y)&0x2000)==0)
          if ((getfield(x+1,y)&1)==0 || (getfield(x,y)&0x10)==0)
            return true;
      break;
    case DIR_UP:
      if (y>0)
        if ((getfield(x,y-1)&0x2000)==0)
          if ((getfield(x,y-1)&0x800)==0 || (getfield(x,y)&0x40)==0)
            return true;
      break;
    case DIR_LEFT:
      if (x>0)
        if ((getfield(x-1,y)&0x2000)==0)
          if ((getfield(x-1,y)&0x10)==0 || (getfield(x,y)&1)==0)
            return true;
      break;
    case DIR_DOWN:
      if (y<9)
        if ((getfield(x,y+1)&0x2000)==0)
          if ((getfield(x,y+1)&0x40)==0 || (getfield(x,y)&0x800)==0)
            return true;
  }
  return false;
}

void checkmonscared(int16_t h)
{
  int16_t m;
  for (m=0;m<MONSTERS;m++)
    if (h==mondat[m].h && mondat[m].dir==DIR_UP)
      mondat[m].dir=DIR_DOWN;
}

void killmon(int16_t mon)
{
  if (mondat[mon].flag) {
    mondat[mon].flag = false;
    CALL_METHOD(mondat[mon].mop, kill);
    if (bonusmode)
      totalmonsters++;
  }
}

void squashmonsters(int16_t bag,int *clfirst,int *clcoll)
{
  int next=clfirst[2],m;
  struct obj_position monpos;

  while (next!=-1) {
    m=next-FIRSTMONSTER;
    CALL_METHOD(mondat[m].mop, getpos, &monpos);
    if (monpos.y >= bagy(bag))
      squashmonster(m,1,bag);
    next=clcoll[next];
  }
}

int16_t killmonsters(int *clfirst,int *clcoll)
{
  int next=clfirst[2],m,n=0;
  while (next!=-1) {
    m=next-FIRSTMONSTER;
    killmon(m);
    n++;
    next=clcoll[next];
  }
  return n;
}

static void
squashmonster(int16_t mon,int16_t death,int16_t bag)
{
  CALL_METHOD(mondat[mon].mop, damage);
  mondat[mon].death=death;
  mondat[mon].bag=bag;
}

int16_t monleft(void)
{
  return nmononscr()+totalmonsters-nextmonster;
}

static int16_t
nmononscr(void)
{
  int16_t i,n=0;
  for (i=0;i<MONSTERS;i++)
    if (mondat[i].flag)
      n++;
  return n;
}

void incmont(int16_t n)
{
  int16_t m;
  if (n>MONSTERS)
    n=MONSTERS;
  for (m=1;m<n;m++)
    mondat[m].t++;
}

int16_t getfield(int16_t x,int16_t y)
{
  return field[y*15+x];
}
