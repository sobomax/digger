/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdlib.h>
#include <string.h>

#include "def.h"
#include "digger_types.h"
#include "draw_api.h"
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
#include "input.h"
#include "state_hash.h"

static struct monster
{
  int16_t h,v,xr,yr,dir,t,hnt,death,bag,dtime,stime,chase;
  bool flag;
  struct monster_obj *mop;
} mondat[6];

static int16_t nextmonster=0,totalmonsters=0,maxmononscr=0,nextmontime=0,mongaptime=0;
static int16_t chase=0;

static bool unbonusflag=false;

/* Asymmetric two-player mode: index of the monster controlled by the second
   player (or -1), and the direction that player currently requests. */
static int16_t possessed=-1;
static int16_t posdir=DIR_NONE;
static int16_t monlives=0;
/* Monster player's run of emeralds, as digger's emn/emocttime */
static int16_t monemn=0,monemocttime=0;
static bool monlivesdirty=false;

static void createmonster(void);
static void monai(struct digger_draw_api *, int16_t mon);
static int16_t aidir(int16_t mon, const struct obj_position *mopos);
static int16_t playerdir(int16_t mon);
static void mondie(struct digger_draw_api *, int16_t mon);
static bool fieldclear(int16_t dir,int16_t x,int16_t y);
static void squashmonster(int16_t mon,int16_t death,int16_t bag);
static int16_t nmononscr(void);
static void unpossess(int16_t mon);
static void monbonusstop(void);
static bool monsactive(void);

static int monplayerno(void);
static bool canpossess(void);

/* Monster lives of their own only exist in the plain two player vs monster
   mode; in the alternate one each player has one pool of lives for both
   roles, and haunting (see monplayerno()) is free. */
#define MONLIVESUSED (dgstate.monplayer && dgstate.nplayers==1)

/* The player who controls the currently possessed monster */
static int possessedby=-1;
/* Haunted: the haunting player has HAUNTLIVES monsters in all, and every
   one of them that dies pays HAUNTREWARD to the other player. Losing the
   last one ends the game in the other player's favour. */
#define HAUNTLIVES 7
#define HAUNTREWARD 8250
static bool hauntover=false;
static int16_t hauntpending=0; /* earned, paid when the next one appears */
static int16_t hauntrewards=0; /* due now, paid out by domonsters() */
static int hauntrewardto=-1;
/* Frames left of the Hobbin spell the possessed monster got from the bonus */
static int16_t monbonustime=0;
/* Frames left of the flash+beep that announces it, then the bonus tune */
static int16_t monbonusflash=0;
static bool monintenreset=false;

#define ISNOB(mop) (CALL_METHOD((mop), isnobbin))
#define ISHOB(mop) (!CALL_METHOD((mop), isnobbin))

void initmonsters(void)
{
  int16_t i;
  for (i=0;i<MONSTERS;i++) {
    if (mondat[i].mop != NULL) {
      CALL_METHOD(mondat[i].mop, dtor);
      mondat[i].mop = NULL;
    }
  }
  memset(mondat, '\0', sizeof(mondat));
  nextmonster=0;
  chase=0;
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
  monbonusstop();
  possessed=-1;
  possessedby=-1;
  posdir=DIR_NONE;
  monemn=0;
  monemocttime=0;
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
  int mp=monplayerno();

  if (dgstate.monplayer) {
    readdirect(1);
    posdir=getdirect(1);
  }
  else if (mp!=-1)
    posdir=getdirect(mp); /* Haunting: dodigger() has read the keys */
  if (mp!=-1 && monemocttime>0)
    monemocttime--;
  /* The bonus keeps the possessed monster a Hobbin: hold its Hobbin age
     young (but old enough to eat bags, see monai()), and once the time is up
     let monai() turn it back at the next grid point, as it would normally
     do. */
  if (monintenreset) {
    monintenreset=false;
    ddap->ginten(0);
  }
  if (monbonusflash>0 && possessed!=-1) {
    monbonusflash--;
    ddap->ginten(monbonusflash&1);
    soundbonus();
    if (monbonusflash==0) {
      ddap->ginten(0);
      soundbonusoff();
      music(MUSIC_BONUS, 1.0);
    }
  }
  if (monbonustime>0 && possessed!=-1) {
    if (--monbonustime==0) {
      mondat[possessed].hnt=ISHOB(mondat[possessed].mop) ? 100 : 0;
      soundbonusoff(); /* End of the warning beeps */
      ddap->ginten(0);
      monbonusstop();
      if (isalive())
        music(MUSIC_MAIN, 1.0);
    }
    else {
      mondat[possessed].hnt=1;
      /* About to wear off: flash the screen and beep, the way Digger's
         bonus mode warns, and blink the monster between its normal and
         possessed colours (slower, as it isn't redrawn every frame) */
      if (monbonustime<20) {
        ddap->ginten(monbonustime&1);
        monster_obj_setpossessed(mondat[possessed].mop,
          (monbonustime&4)==0);
        soundbonus();
      }
    }
  }
  for (;hauntrewards>0;hauntrewards--) {
    addscore(ddap, hauntrewardto, HAUNTREWARD);
    soundgold(); /* Ka-ching: it's a money reward */
  }
  if (monlivesdirty) {
    monlivesdirty=false;
    drawlives(ddap);
  }
  if (nextmontime>0)
    nextmontime--;
  else {
    if (nextmonster<totalmonsters && nmononscr()<maxmononscr && monsactive() &&
        !bonusmode && monbonustime==0)
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
            if (ISNOB(mondat[i].mop) && CALL_METHOD(mondat[i].mop, isalive) &&
                i!=possessed)
              monai(ddap, i);
        }
        else
          mondat[i].t--;
      else
        mondie(ddap, i);
    }
}

void
monster_debug_hash_append(struct state_hash *shp)
{
  int i;
  struct obj_position pos;
  bool flag;

  STATE_HASH_VAR(shp, nextmonster);
  STATE_HASH_VAR(shp, totalmonsters);
  STATE_HASH_VAR(shp, maxmononscr);
  STATE_HASH_VAR(shp, nextmontime);
  STATE_HASH_VAR(shp, mongaptime);
  STATE_HASH_VAR(shp, chase);
  STATE_HASH_VAR(shp, unbonusflag);
  /* Only in the modes that have them, so that other games hash the same
     as they always did (eDRF checkpoints) */
  if (dgstate.monplayer || dgstate.haunted) {
    STATE_HASH_VAR(shp, possessed);
    STATE_HASH_VAR(shp, monlives);
    STATE_HASH_VAR(shp, monemn);
    STATE_HASH_VAR(shp, monemocttime);
    STATE_HASH_VAR(shp, monbonustime);
    STATE_HASH_VAR(shp, monbonusflash);
  }
  for (i = 0; i < MONSTERS; i++) {
    STATE_HASH_VAR(shp, mondat[i].h);
    STATE_HASH_VAR(shp, mondat[i].v);
    STATE_HASH_VAR(shp, mondat[i].xr);
    STATE_HASH_VAR(shp, mondat[i].yr);
    STATE_HASH_VAR(shp, mondat[i].dir);
    STATE_HASH_VAR(shp, mondat[i].t);
    STATE_HASH_VAR(shp, mondat[i].hnt);
    STATE_HASH_VAR(shp, mondat[i].death);
    STATE_HASH_VAR(shp, mondat[i].bag);
    STATE_HASH_VAR(shp, mondat[i].dtime);
    STATE_HASH_VAR(shp, mondat[i].stime);
    STATE_HASH_VAR(shp, mondat[i].chase);
    STATE_HASH_VAR(shp, mondat[i].flag);
    if (mondat[i].mop != NULL) {
      CALL_METHOD(mondat[i].mop, getpos, &pos);
      STATE_HASH_VAR(shp, pos.x);
      STATE_HASH_VAR(shp, pos.y);
      STATE_HASH_VAR(shp, pos.dir);
      flag = CALL_METHOD(mondat[i].mop, isalive);
      STATE_HASH_VAR(shp, flag);
      flag = ISNOB(mondat[i].mop);
      STATE_HASH_VAR(shp, flag);
    }
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
      if (possessed==-1 && canpossess()) {
        possessed=i;
        possessedby=monplayerno();
        /* The haunting player is back: pay the other one for the monster
           they lost before */
        hauntrewards+=hauntpending;
        hauntpending=0;
      }
      mondat[i].mop = monster_obj_ctor(i, MON_NOBBIN, possessed==i, DIR_LEFT,
        292, 18);
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
  int16_t monox,monoy,dir;
  int clcoll[SPRITES],clfirst[TYPES],i,m,dig,n;
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

    if (mon==possessed)
      dir=playerdir(mon);
    else
      dir=aidir(mon,&mopos);

    /* Monsters take a time penalty for changing direction */

    if (mondat[mon].dir!=dir)
      mondat[mon].t++;

    /* Save the new direction */

    mondat[mon].dir=dir;
  }
  else if (mon==possessed && mondat[mon].dir!=DIR_NONE &&
           posdir==reversedir(mondat[mon].dir)) {
    /* Player-controlled monster can turn back mid-cell */
    mondat[mon].dir=posdir;
    mondat[mon].t++;
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

  /* Hobbins can eat emeralds, the monster player scores for them */
  if (ISHOB(mondat[mon].mop))
    if (hitemerald((mopos.x-12)/20,(mopos.y-18)/18,
                   (mopos.x-12)%20,(mopos.y-18)%18,
                   mondat[mon].dir) && mon==possessed) {
      if (monemocttime==0)
        monemn=0;
      scoreemerald(ddap, possessedby);
      soundem();
      soundemerald(monemn);
      monemn++;
      if (monemn==8) {
        monemn=0;
        scoreoctave(ddap, possessedby);
      }
      monemocttime=9;
    }

  /* If Digger's gone, don't bother */
  if (!monsactive() && mopos_changed) {
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
    if (mongotgold) { /* No time penalty if monster eats gold */
      mondat[mon].t=0;
      if (mon==possessed) {
        scoregold(ddap, possessedby);
        soundgold();
      }
    }
    if (ISHOB(mondat[mon].mop) && mondat[mon].hnt>1) {
      n=removebags(clfirst,clcoll); /* Hobbins eat bags */
      /* The monster player gets the gold in them */
      if (mon==possessed)
        for (;n>0;n--) {
          scoregold(ddap, possessedby);
          soundgold();
        }
    }
  }

  /* Increase hobbin cross counter */

  if (ISNOB(mondat[mon].mop) && clfirst[2]!=-1 && monsactive())
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

  /* The monster player can eat the bonus too: it's gone for Digger, and the
     monster becomes a Hobbin for as long as Digger's bonus mode would last */
  if (mon==possessed && clfirst[0]!=-1 && bonusvisible) {
    erasebonus(ddap);
    scorebonus(ddap, possessedby);
    soundeatm();
    if (ISNOB(mondat[mon].mop))
      CALL_METHOD(mondat[mon].mop, mutate);
    mondat[mon].hnt=0;
    monbonustime=250-levof10()*20;
    /* Announce it the way Digger's bonus mode runs out: flashing screen
       and beeps, then the bonus tune (on the normal palette, as a bright
       one is what Digger's own bonus looks like) */
    monbonusflash=20;
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
        dig=i-FIRSTDIGGER+dgstate.curplayer;
        if (digalive(dig)) {
          killdigger(dig,3,0); /* Kill Digger */
          if (mon==possessed && !digalive(dig))
            scorekill(ddap, possessedby); /* Monster player scores */
        }
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

/* Pick direction for a computer-controlled monster sitting on a grid node */
static int16_t
aidir(int16_t mon, const struct obj_position *mopos)
{
  int16_t dir,mdirp1,mdirp2,mdirp3,mdirp4,t;
  int dig;

  /* Set up monster direction properties to chase Digger */

  dig=mondat[mon].chase;
  /* The other Digger of the current player's (the same one, if it's the
     only one) */
  if (!digalive(dig))
    dig=2*dgstate.curplayer+(dgstate.diggers-1)-dig;

  if (abs(diggery(dig)-mopos->y)>abs(diggerx(dig)-mopos->x)) {
    if (diggery(dig)<mopos->y) { mdirp1=DIR_UP;    mdirp4=DIR_DOWN; }
                               else { mdirp1=DIR_DOWN;  mdirp4=DIR_UP; }
    if (diggerx(dig)<mopos->x) { mdirp2=DIR_LEFT;  mdirp3=DIR_RIGHT; }
                               else { mdirp2=DIR_RIGHT; mdirp3=DIR_LEFT; }
  }
  else {
    if (diggerx(dig)<mopos->x) { mdirp1=DIR_LEFT;  mdirp4=DIR_RIGHT; }
                               else { mdirp1=DIR_RIGHT; mdirp4=DIR_LEFT; }
    if (diggery(dig)<mopos->y) { mdirp2=DIR_UP;    mdirp3=DIR_DOWN; }
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

  return dir;
}

/* Pick direction for the player-controlled monster sitting on a grid node.
   Nobbins keep going their current way until the requested turn becomes
   possible, Hobbins dig wherever they are told. No input means stop. */
static int16_t
playerdir(int16_t mon)
{
  int16_t dir=mondat[mon].dir;

  if (posdir==DIR_NONE)
    return DIR_NONE;
  if (ISHOB(mondat[mon].mop))
    return posdir;
  if (fieldclear(posdir,mondat[mon].h,mondat[mon].v))
    return posdir;
  if (dir!=DIR_NONE && fieldclear(dir,mondat[mon].h,mondat[mon].v))
    return dir;
  return DIR_NONE;
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
  unpossess(mon);
  if (mondat[mon].flag) {
    mondat[mon].flag = false;
    CALL_METHOD(mondat[mon].mop, kill);
    /* Monsters killed in either bonus get replaced once it's over */
    if (bonusmode || monbonustime>0)
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

/* A bag pushed into monsters is stopped by them (see pushbag()). The ones
   the computer moves are heading into it, so it's a bag they're stuck on, or
   one they eat as Hobbins (see monai()), but the player's can stand still or
   go the other way: that counts the same for it. Returns whether it eats the
   bag. */
bool monsbagstopped(struct digger_draw_api *ddap,int *clfirst,int *clcoll)
{
  int next=clfirst[2],m;
  while (next!=-1) {
    m=next-FIRSTMONSTER;
    if (m==possessed) {
      if (ISNOB(mondat[m].mop))
        mondat[m].hnt++;
      else if (mondat[m].hnt>1) {
        scoregold(ddap, possessedby);
        soundgold();
        return true;
      }
    }
    next=clcoll[next];
  }
  return false;
}

int16_t killmonsters(int *clfirst,int *clcoll,bool spareplayer)
{
  int next=clfirst[2],m,n=0;
  while (next!=-1) {
    m=next-FIRSTMONSTER;
    if (!spareplayer || m!=possessed) {
      killmon(m);
      n++;
    }
    next=clcoll[next];
  }
  return n;
}

static void
squashmonster(int16_t mon,int16_t death,int16_t bag)
{
  unpossess(mon);
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

/* Player-controlled monster has been killed: it costs the monster player a
   life, and control passes to the next monster to spawn. */
static void
unpossess(int16_t mon)
{
  if (mon!=possessed)
    return;
  possessed=-1;
  /* Killed during its own bonus: replaced like the others, see killmon() */
  if (monbonustime>0) {
    totalmonsters++;
    monbonusstop();
  }
  if (dgstate.haunted) {
    if (monlives>0)
      monlives--;
    hauntrewardto=1-possessedby;
    hauntpending++;
    monlivesdirty=true;
    if (monlives==0) {
      /* No next monster to wait for */
      hauntover=true;
      hauntrewards+=hauntpending;
      hauntpending=0;
    }
    return;
  }
  if (MONLIVESUSED) {
    if (monlives>0) {
      monlives--;
      monlivesdirty=true;
    }
  }
  else if (getlives(possessedby)>0) {
    declife(possessedby);
    monlivesdirty=true;
  }
}

/* End the possessed monster's bonus Hobbin spell (and its warning) */
static void
monbonusstop(void)
{
  /* Back to the main tune, unless Digger is dying (dirge) or gone */
  if (monbonustime>0 && isalive())
    music(MUSIC_MAIN, 1.0);
  if (monbonusflash>0) {
    /* Cut short while flashing: domonsters() puts the palette back */
    monintenreset=true;
    soundbonusoff();
    monbonusflash=0;
  }
  if (monbonustime>0 || possessed!=-1) {
    if (monbonustime>0 && monbonustime<20) {
      soundbonusoff();
      monintenreset=true; /* Cut short while flashing, see domonsters() */
    }
    if (possessed!=-1 && mondat[possessed].mop!=NULL)
      monster_obj_setpossessed(mondat[possessed].mop, true);
  }
  monbonustime=0;
}

/* The player controlling monsters right now, or -1 if nobody does */
static int
monplayerno(void)
{
  /* Vs monster: the one not digging (curplayer is always 0 unless the
     roles alternate) */
  if (dgstate.monplayer)
    return (1-dgstate.curplayer);
  /* Haunted: whoever's digger is out of lives haunts the other one */
  if (dgstate.haunted) {
    if (getlives(0)==0 && getlives(1)!=0)
      return (0);
    if (getlives(1)==0 && getlives(0)!=0)
      return (1);
  }
  return (-1);
}

/* May the next monster to spawn be taken over by the monster player? */
static bool
canpossess(void)
{
  if (dgstate.monplayer)
    return (MONLIVESUSED ? monlives>0 : getlives(1-dgstate.curplayer)>0);
  return (monplayerno()!=-1 && monlives>0);
}

/* Digger died but the round goes on (monplayerholdsround()): restart the
   monster schedule like a new round would, i.e. the level's monsters all
   come out again with the bonus after them, except that those still alive
   stay and count as already out. */
void monrestartround(bool bonusout)
{
  nextmonster=nmononscr();
  totalmonsters=levof10()+5;
  if (totalmonsters<nextmonster)
    totalmonsters=nextmonster;
  nextmontime=10;
  unbonusflag=!bonusout; /* Not another one */
}

/* Monsters normally freeze while no Digger is alive. In two player vs
   monster and haunted modes the round goes on as long as the monster
   player's monster lives (Digger respawns), so they keep going during
   Digger's death too. */
static bool
monsactive(void)
{
  return (isalive() || monplayerholdsround());
}

/* Does the monster player's live monster keep the round going even with no
   Digger alive (vs monster, haunted)? The alternating vs monster mode can't
   do that, as the players swap roles on every Digger death. */
bool monplayerholdsround(void)
{
  return (((dgstate.monplayer && dgstate.nplayers==1) || dgstate.haunted) &&
          monplayeralive());
}

/* Is the monster player's monster on the field and alive right now? */
bool monplayeralive(void)
{
  return (possessed!=-1 && mondat[possessed].flag &&
          CALL_METHOD(mondat[possessed].mop, isalive));
}

void initmonlives(void)
{
  monlives=dgstate.monplayer ? 3 : (dgstate.haunted ? HAUNTLIVES : 0);
  /* New game: whatever was possessed in the previous one doesn't count */
  possessed=-1;
  hauntover=false;
  hauntrewards=0;
  hauntpending=0;
}

/* Haunted: has the haunting player lost all of their monsters? */
bool monhauntover(void)
{
  /* Once the reward for the last one is paid too */
  return (dgstate.haunted && hauntover && hauntrewards==0);
}

int16_t getmonlives(void)
{
  return monlives;
}

void addmonlife(void)
{
  monlives++;
  sound1up();
}
