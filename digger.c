/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <assert.h>
#include <stdlib.h>

#include "def.h"
#include "digger_types.h"
#include "sprite.h"
#include "input.h"
#include "hardware.h"
#include "bullet_obj.h"
#include "digger_obj.h"
#include "digger.h"
#include "draw_api.h"
#include "drawing.h"
#include "main.h"
#include "sound.h"
#include "monster.h"
#include "scores.h"
#include "bags.h"
#include "game.h"

static struct {
  struct digger pub;
  struct digger_obj dob;
  struct bullet_obj bob;
} digdat[DIGGERS];

static int16_t startbonustimeleft=0,bonustimeleft;

static int16_t emmask=0;

static int8_t emfield[MSIZE];

bool bonusvisible=false,bonusmode=false,digvisible;

static void updatedigger(struct digger_draw_api *, int n);
static void updatefire(struct digger_draw_api *, int n);
static void diggerdie(struct digger_draw_api *, int n);
static void initbonusmode(struct digger_draw_api *);
static void endbonusmode(struct digger_draw_api *);
static bool getfirepflag(int n);
static void drawdig(int n);

const struct digger *getdigdat(int n)
{
  assert(n < DIGGERS);
  return &digdat[n].pub;
}

void initdigger(void)
{
  int dig;
  int16_t dir, x, y;

  for (dig=dgstate.curplayer;dig<dgstate.diggers+dgstate.curplayer;dig++) {
    if (digdat[dig].pub.lives==0)
      continue;
    digdat[dig].pub.v=9;
    digdat[dig].pub.mdir=4;
    digdat[dig].pub.h=(dgstate.diggers==1) ? 7 : (8-dig*2);
    x = digdat[dig].pub.h * 20 + 12;
    dir = (dig == 0) ? DIR_RIGHT : DIR_LEFT;
    digdat[dig].pub.rx=0;
    digdat[dig].pub.ry=0;
    digdat[dig].pub.bagtime=0;
    digdat[dig].pub.dead=false; /* alive !=> !dead but dead => !alive */
    digdat[dig].pub.invin=false;
    digdat[dig].pub.ivt=0;
    digdat[dig].pub.deathstage=1;
    y = digdat[dig].pub.v * 18 + 18;
    digger_obj_init(&digdat[dig].dob, dig - dgstate.curplayer, dir, x, y);
    CALL_METHOD(&digdat[dig].dob, put);
    digdat[dig].pub.notfiring=true;
    digdat[dig].pub.emocttime=0;
    digdat[dig].bob.expsn=0;
    digdat[dig].pub.firepressed=false;
    digdat[dig].pub.rechargetime=0;
    digdat[dig].pub.emn=0;
    digdat[dig].pub.msc=1;
  }
  digvisible=true;
  bonusvisible=bonusmode=false;
}

#if defined(INTDRF) || 1
static uint32_t frame;
#endif

uint32_t
getframe(void)
{

  return (frame);
}

void newframe(void)
{

  gethrt(sounddiedone ? false : true);
  checkkeyb();

#if defined(INTDRF) || 1
  frame++;
#endif

}

void drawdig(int n)
{
  CALL_METHOD(&digdat[n].dob, animate);
  if (digdat[n].pub.invin) {
    digdat[n].pub.ivt--;
    if (digdat[n].pub.ivt==0)
      digdat[n].pub.invin=false;
    else
      if (digdat[n].pub.ivt%10<5)
        erasespr(FIRSTDIGGER+n-dgstate.curplayer);
  }
}

void
dodigger(struct digger_draw_api *ddap)
{
  int n;
  int16_t tdir;

  newframe();
  if (dgstate.gauntlet) {
    drawlives(ddap);
    if (dgstate.cgtime<dgstate.ftime)
      dgstate.timeout=true;
    dgstate.cgtime-=dgstate.ftime;
  }
  for (n=dgstate.curplayer;n<dgstate.diggers+dgstate.curplayer;n++) {
    if (digdat[n].bob.expsn!=0)
      drawexplosion(n);
    else
      updatefire(ddap, n);
    if (digvisible) {
      if (digdat[n].dob.alive)
        if (digdat[n].pub.bagtime!=0) {
          tdir = digdat[n].dob.dir;
          digdat[n].dob.dir = digdat[n].pub.mdir;
          drawdig(n);
          digdat[n].dob.dir = tdir;
          incpenalty();
          digdat[n].pub.bagtime--;
        }
        else
          updatedigger(ddap, n);
      else
        diggerdie(ddap, n);
    }
    if (digdat[n].pub.emocttime>0)
      digdat[n].pub.emocttime--;
  }
  if (bonusmode && isalive()) {
    if (bonustimeleft!=0) {
      bonustimeleft--;
      if (startbonustimeleft!=0 || bonustimeleft<20) {
        startbonustimeleft--;
        if (bonustimeleft&1) {
          ddap->ginten(0);
          soundbonus();
        }
        else {
          ddap->ginten(1);
          soundbonus();
        }
        if (startbonustimeleft==0) {
          music(0, 1.0);
          soundbonusoff();
          ddap->ginten(1);
        }
      }
    }
    else {
      endbonusmode(ddap);
      soundbonusoff();
      music(1, 1.0);
    }
  }
  if (bonusmode && !isalive()) {
    endbonusmode(ddap);
    soundbonusoff();
    music(1, 1.0);
  }
}

static void
updatefire(struct digger_draw_api *ddap, int n)
{
  int16_t pix=0, fx, fy;
  int clfirst[TYPES],clcoll[SPRITES],i;
  bool clflag;
  if (digdat[n].pub.notfiring) {
    if (digdat[n].pub.rechargetime!=0) {
      digdat[n].pub.rechargetime--;
      if (digdat[n].pub.rechargetime == 0) {
        CALL_METHOD(&digdat[n].dob, recharge);
      }
    } else {
      if (getfirepflag(n-dgstate.curplayer)) {
        if (digdat[n].dob.alive) {
          CALL_METHOD(&digdat[n].dob, discharge);
          digdat[n].pub.rechargetime=levof10()*3+60;
          digdat[n].pub.notfiring=false;
          switch (digdat[n].dob.dir) {
            case DIR_RIGHT:
              fx = digdat[n].dob.x + 8;
              fy = digdat[n].dob.y + 4;
              break;
            case DIR_UP:
              fx = digdat[n].dob.x + 4;
              fy = digdat[n].dob.y;
              break;
            case DIR_LEFT:
              fx = digdat[n].dob.x;
              fy = digdat[n].dob.y + 4;
              break;
            case DIR_DOWN:
              fx = digdat[n].dob.x + 4;
              fy = digdat[n].dob.y + 8;
              break;
            default:
              abort();
          }
          bullet_obj_init(&digdat[n].bob, n - dgstate.curplayer, digdat[n].dob.dir, fx, fy);
          CALL_METHOD(&digdat[n].bob, put);
        }
      }
    }
  }
  else {
    switch (digdat[n].bob.dir) {
      case DIR_RIGHT:
        digdat[n].bob.x+=8;
        pix=ddap->ggetpix(digdat[n].bob.x,digdat[n].bob.y+4)|
            ddap->ggetpix(digdat[n].bob.x+4,digdat[n].bob.y+4);
        break;
      case DIR_UP:
        digdat[n].bob.y-=7;
        pix=0;
        for (i=0;i<7;i++)
          pix|=ddap->ggetpix(digdat[n].bob.x+4,digdat[n].bob.y+i);
        pix&=0xc0;
        break;
      case DIR_LEFT:
        digdat[n].bob.x-=8;
        pix=ddap->ggetpix(digdat[n].bob.x,digdat[n].bob.y+4)|
            ddap->ggetpix(digdat[n].bob.x+4,digdat[n].bob.y+4);
        break;
      case DIR_DOWN:
        digdat[n].bob.y+=7;
        pix=0;
        for (i=0;i<7;i++)
          pix|=ddap->ggetpix(digdat[n].bob.x,digdat[n].bob.y+i);
        pix&=0x3;
        break;       
    }
    CALL_METHOD(&digdat[n].bob, animate);
    for (i=0;i<TYPES;i++)
      clfirst[i]=first[i];
    for (i=0;i<SPRITES;i++)
      clcoll[i]=coll[i];
    incpenalty();
    i=clfirst[2];
    while (i!=-1) {
      killmon(i-FIRSTMONSTER);
      scorekill(ddap, n);
      CALL_METHOD(&digdat[n].bob, explode);
      i=clcoll[i];
    }
    i=clfirst[4];
    while (i!=-1) {
      if (i-FIRSTDIGGER+dgstate.curplayer!=n && !digdat[i-FIRSTDIGGER+dgstate.curplayer].pub.invin
          && digdat[i-FIRSTDIGGER+dgstate.curplayer].dob.alive) {
        killdigger(i-FIRSTDIGGER+dgstate.curplayer,3,0);
        CALL_METHOD(&digdat[n].bob, explode);
      }
      i=clcoll[i];
    }
    if (clfirst[0]!=-1 || clfirst[1]!=-1 || clfirst[2]!=-1 || clfirst[3]!=-1 ||
        clfirst[4]!=-1)
      clflag=true;
    else
      clflag=false;
    if (clfirst[0]!=-1 || clfirst[1]!=-1 || clfirst[3]!=-1) {
      CALL_METHOD(&digdat[n].bob, explode);
      i=clfirst[3];
      while (i!=-1) {
        if (digdat[i-FIRSTFIREBALL+dgstate.curplayer].bob.expsn==0) {
          CALL_METHOD(&digdat[i-FIRSTFIREBALL+dgstate.curplayer].bob, explode);
        }
        i=clcoll[i];
      }
    }
    switch (digdat[n].bob.dir) {
      case DIR_RIGHT:
        if (digdat[n].bob.x>296) {
          CALL_METHOD(&digdat[n].bob, explode);
        } else {
          if (pix!=0 && !clflag) {
            digdat[n].bob.x-=8;
            CALL_METHOD(&digdat[n].bob, animate);
            CALL_METHOD(&digdat[n].bob, explode);
          }
        }
        break;
      case DIR_UP:
        if (digdat[n].bob.y<15) {
          CALL_METHOD(&digdat[n].bob, explode);
        } else {
          if (pix!=0 && !clflag) {
            digdat[n].bob.y+=7;
            CALL_METHOD(&digdat[n].bob, animate);
            CALL_METHOD(&digdat[n].bob, explode);
          }
        }
        break;
      case DIR_LEFT:
        if (digdat[n].bob.x<16) {
          CALL_METHOD(&digdat[n].bob, explode);
        } else {
          if (pix!=0 && !clflag) {
            digdat[n].bob.x+=8;
            CALL_METHOD(&digdat[n].bob, animate);
            CALL_METHOD(&digdat[n].bob, explode);
          }
        }
        break;
      case DIR_DOWN:
        if (digdat[n].bob.y>183) {
          CALL_METHOD(&digdat[n].bob, explode);
        } else {
          if (pix!=0 && !clflag) {
            digdat[n].bob.y-=7;
            CALL_METHOD(&digdat[n].bob, animate);
            CALL_METHOD(&digdat[n].bob, explode);
          }
        }
    }
  }
}

void erasediggers(void)
{
  int i;
  for (i=0;i<dgstate.diggers;i++)
    erasespr(FIRSTDIGGER+i);
  digvisible=false;
}

void drawexplosion(int n)
{

  if (digdat[n].bob.expsn < 4) {
    CALL_METHOD(&digdat[n].bob, animate);
    incpenalty();
  } else {
    killfire(n);
  }
}

void killfire(int n)
{
  if (!digdat[n].pub.notfiring) {
    digdat[n].pub.notfiring=true;
    CALL_METHOD(&digdat[n].bob, remove);
  }
}

static void
updatedigger(struct digger_draw_api *ddap, int n)
{
  int16_t dir,ddir,diggerox,diggeroy,nmon;
  bool push=true,bagf;
  int clfirst[TYPES],clcoll[SPRITES],i;
  readdirect(n-dgstate.curplayer);
  dir=getdirect(n-dgstate.curplayer);
  if (dir==DIR_RIGHT || dir==DIR_UP || dir==DIR_LEFT || dir==DIR_DOWN)
    ddir=dir;
  else
    ddir=DIR_NONE;
  if (digdat[n].pub.rx==0 && (ddir==DIR_UP || ddir==DIR_DOWN))
    digdat[n].dob.dir=digdat[n].pub.mdir=ddir;
  if (digdat[n].pub.ry==0 && (ddir==DIR_RIGHT || ddir==DIR_LEFT))
    digdat[n].dob.dir=digdat[n].pub.mdir=ddir;
  if (dir==DIR_NONE)
    digdat[n].pub.mdir=DIR_NONE;
  else
    digdat[n].pub.mdir=digdat[n].dob.dir;
  if ((digdat[n].dob.x==292 && digdat[n].pub.mdir==DIR_RIGHT) ||
      (digdat[n].dob.x==12 && digdat[n].pub.mdir==DIR_LEFT) ||
      (digdat[n].dob.y==180 && digdat[n].pub.mdir==DIR_DOWN) ||
      (digdat[n].dob.y==18 && digdat[n].pub.mdir==DIR_UP))
    digdat[n].pub.mdir=DIR_NONE;
  diggerox=digdat[n].dob.x;
  diggeroy=digdat[n].dob.y;
  if (digdat[n].pub.mdir!=DIR_NONE)
    eatfield(diggerox,diggeroy,digdat[n].pub.mdir);
  switch (digdat[n].pub.mdir) {
    case DIR_RIGHT:
      drawrightblob(digdat[n].dob.x,digdat[n].dob.y);
      digdat[n].dob.x+=4;
      break;
    case DIR_UP:
      drawtopblob(digdat[n].dob.x,digdat[n].dob.y);
      digdat[n].dob.y-=3;
      break;
    case DIR_LEFT:
      drawleftblob(digdat[n].dob.x,digdat[n].dob.y);
      digdat[n].dob.x-=4;
      break;
    case DIR_DOWN:
      drawbottomblob(digdat[n].dob.x,digdat[n].dob.y);
      digdat[n].dob.y+=3;
      break;
  }
  if (hitemerald((digdat[n].dob.x-12)/20,(digdat[n].dob.y-18)/18,
                 (digdat[n].dob.x-12)%20,(digdat[n].dob.y-18)%18,
                 digdat[n].pub.mdir)) {
    if (digdat[n].pub.emocttime==0)
      digdat[n].pub.emn=0;
    scoreemerald(ddap, n);
    soundem();
    soundemerald(digdat[n].pub.emn);

    digdat[n].pub.emn++;
    if (digdat[n].pub.emn==8) {
      digdat[n].pub.emn=0;
      scoreoctave(ddap, n);
    }
    digdat[n].pub.emocttime=9;
  }
  drawdig(n);
  for (i=0;i<TYPES;i++)
    clfirst[i]=first[i];
  for (i=0;i<SPRITES;i++)
    clcoll[i]=coll[i];
  incpenalty();

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
    if (digdat[n].pub.mdir==DIR_RIGHT || digdat[n].pub.mdir==DIR_LEFT) {
      push=pushbags(ddap, digdat[n].pub.mdir,clfirst,clcoll);
      digdat[n].pub.bagtime++;
    }
    else
      if (!pushudbags(ddap, clfirst,clcoll))
        push=false;
    if (!push) { /* Strange, push not completely defined */
      digdat[n].dob.x=diggerox;
      digdat[n].dob.y=diggeroy;
      digdat[n].dob.dir = digdat[n].pub.mdir;
      drawdig(n);
      incpenalty();
      digdat[n].dob.dir=reversedir(digdat[n].pub.mdir);
    }
  }
  if (clfirst[2]!=-1 && bonusmode && digdat[n].dob.alive)
    for (nmon=killmonsters(clfirst,clcoll);nmon!=0;nmon--) {
      soundeatm();
      sceatm(ddap, n);
    }
  if (clfirst[0]!=-1) {
    scorebonus(ddap, n);
    initbonusmode(ddap);
  }
  digdat[n].pub.h=(digdat[n].dob.x-12)/20;
  digdat[n].pub.rx=(digdat[n].dob.x-12)%20;
  digdat[n].pub.v=(digdat[n].dob.y-18)/18;
  digdat[n].pub.ry=(digdat[n].dob.y-18)%18;
}

void sceatm(struct digger_draw_api *ddap, int n)
{
  scoreeatm(ddap, n,digdat[n].pub.msc);
  digdat[n].pub.msc<<=1;
}

static int16_t deatharc[7]={3,5,6,6,5,3,0};

static void
diggerdie(struct digger_draw_api *ddap, int n)
{
  int clfirst[TYPES],clcoll[SPRITES],i;
  bool alldead;
  switch (digdat[n].pub.deathstage) {
    case 1:
      if (bagy(digdat[n].pub.deathbag)+6>digdat[n].dob.y)
        digdat[n].dob.y=bagy(digdat[n].pub.deathbag)+6;
      drawdigger(n-dgstate.curplayer,15,digdat[n].dob.x,digdat[n].dob.y,false);
      incpenalty();
      if (getbagdir(digdat[n].pub.deathbag)+1==0) {
        soundddie();
        digdat[n].pub.deathtime=5;
        digdat[n].pub.deathstage=2;
        digdat[n].pub.deathani=0;
        digdat[n].dob.y-=6;
      }
      break;
    case 2:
      if (digdat[n].pub.deathtime!=0) {
        digdat[n].pub.deathtime--;
        break;
      }
      if (digdat[n].pub.deathani==0)
        music(2, (dgstate.diggers > 1) ? 0.7 : 1.0);
      drawdigger(n-dgstate.curplayer,14-digdat[n].pub.deathani,digdat[n].dob.x,digdat[n].dob.y,
                 false);
      for (i=0;i<TYPES;i++)
        clfirst[i]=first[i];
      for (i=0;i<SPRITES;i++)
        clcoll[i]=coll[i];
      incpenalty();
      if (digdat[n].pub.deathani==0 && clfirst[2]!=-1)
        killmonsters(clfirst,clcoll);
      if (digdat[n].pub.deathani<4) {
        digdat[n].pub.deathani++;
        digdat[n].pub.deathtime=2;
      }
      else {
        digdat[n].pub.deathstage=4;
        if (musicflag || dgstate.diggers>1)
          digdat[n].pub.deathtime=60;
        else
          digdat[n].pub.deathtime=10;
      }
      break;
    case 3:
      digdat[n].pub.deathstage=5;
      digdat[n].pub.deathani=0;
      digdat[n].pub.deathtime=0;
      break;
    case 5:
      if (digdat[n].pub.deathani>=0 && digdat[n].pub.deathani<=6) {
        drawdigger(n-dgstate.curplayer,15,digdat[n].dob.x,
                   digdat[n].dob.y-deatharc[digdat[n].pub.deathani],false);
        if (digdat[n].pub.deathani==6 && !isalive())
          musicoff();
        incpenalty();
        digdat[n].pub.deathani++;
        if (digdat[n].pub.deathani==1)
          soundddie();
        if (digdat[n].pub.deathani==7) {
          digdat[n].pub.deathtime=5;
          digdat[n].pub.deathani=0;
          digdat[n].pub.deathstage=2;
        }
      }
      break;
    case 4:
      if (digdat[n].pub.deathtime!=0)
        digdat[n].pub.deathtime--;
      else {
	if (dgstate.diggers == 1 && !sounddiedone) {
	    frame -= 1;
	    break;
	}
        digdat[n].pub.dead=true;
        alldead=true;
        for (i=0;i<dgstate.diggers;i++)
          if (!digdat[i].pub.dead) {
            alldead=false;
            break;
          }
        if (alldead)
          setdead(true);
        else
          if (isalive() && digdat[n].pub.lives>0) {
            if (!dgstate.gauntlet)
              digdat[n].pub.lives--;
            drawlives(ddap);
            if (digdat[n].pub.lives>0) {
              digdat[n].pub.v=9;
              digdat[n].pub.mdir=4;
              digdat[n].pub.h=(dgstate.diggers==1) ? 7 : (8-n*2);
              digdat[n].dob.x=digdat[n].pub.h*20+12;
              digdat[n].dob.dir=(n==0) ? DIR_RIGHT : DIR_LEFT;
              digdat[n].pub.rx=0;
              digdat[n].pub.ry=0;
              digdat[n].pub.bagtime=0;
              digdat[n].dob.alive=true;
              digdat[n].pub.dead=false;
              digdat[n].pub.invin=true;
              digdat[n].pub.ivt=50;
              digdat[n].pub.deathstage=1;
              digdat[n].dob.y=digdat[n].pub.v*18+18;
              erasespr(n+FIRSTDIGGER-dgstate.curplayer);
              CALL_METHOD(&digdat[n].dob, put);
              digdat[n].pub.notfiring=true;
              digdat[n].pub.emocttime=0;
              digdat[n].pub.firepressed=false;
              digdat[n].bob.expsn=0;
              digdat[n].pub.rechargetime=0;
              digdat[n].pub.emn=0;
              digdat[n].pub.msc=1;
            }
            clearfire(n);
            if (bonusmode)
              music(0, 1.0);
            else
              music(1, 1.0);
          }
      }
  }
}

void createbonus(void)
{
  bonusvisible=true;
  drawbonus(292,18);
}

static void
initbonusmode(struct digger_draw_api *ddap)
{
  int i;
  bonusmode=true;
  erasebonus(ddap);
  ddap->ginten(1);
  bonustimeleft=250-levof10()*20;
  startbonustimeleft=20;
  for (i=0;i<dgstate.diggers;i++)
    digdat[i].pub.msc=1;
}

static void
endbonusmode(struct digger_draw_api *ddap)
{
  bonusmode=false;
  ddap->ginten(0);
}

void
erasebonus(struct digger_draw_api *ddap)
{
  if (bonusvisible) {
    bonusvisible=false;
    erasespr(FIRSTBONUS);
  }
  ddap->ginten(0);
}

int16_t reversedir(int16_t dir)
{
  switch (dir) {
    case DIR_RIGHT: return DIR_LEFT;
    case DIR_LEFT: return DIR_RIGHT;
    case DIR_UP: return DIR_DOWN;
    case DIR_DOWN: return DIR_UP;
  }
  return dir;
}

bool checkdiggerunderbag(int16_t h,int16_t v)
{
  int n;
  for (n=dgstate.curplayer;n<dgstate.diggers+dgstate.curplayer;n++)
    if (digdat[n].dob.alive)
      if (digdat[n].pub.mdir==DIR_UP || digdat[n].pub.mdir==DIR_DOWN)
        if ((digdat[n].dob.x-12)/20==h)
          if ((digdat[n].dob.y-18)/18==v || (digdat[n].dob.y-18)/18+1==v)
            return true;
  return false;
}

void killdigger(int n,int16_t stage,int16_t bag)
{
  if (digdat[n].pub.invin)
    return;
  if (digdat[n].pub.deathstage<2 || digdat[n].pub.deathstage>4) {
    digdat[n].dob.alive=false;
    digdat[n].pub.deathstage=stage;
    digdat[n].pub.deathbag=bag;
  }
}

void makeemfield(void)
{
  int16_t x,y;
  emmask=1<<dgstate.curplayer;
  for (x=0;x<MWIDTH;x++)
    for (y=0;y<MHEIGHT;y++)
      if (getlevch(x,y,levplan())=='C')
        emfield[y*MWIDTH+x]|=emmask;
      else
        emfield[y*MWIDTH+x]&=~emmask;
}

void drawemeralds(void)
{
  int16_t x,y,n=0;
  emmask=1<<dgstate.curplayer;
  for (x=0;x<MWIDTH;x++)
    for (y=0;y<MHEIGHT;y++)
      if (emfield[y*MWIDTH+x]&emmask) {
        n += 1;
        drawemerald(x*20+12,y*18+21);
        if ((n % 4) == 0)
          gethrt(false);
      }
}

static const int16_t embox[8]={8,12,12,9,16,12,6,9};

bool hitemerald(int16_t x,int16_t y,int16_t rx,int16_t ry,int16_t dir)
{
  bool hit=false;
  int16_t r;
  if (dir!=DIR_RIGHT && dir!=DIR_UP && dir!=DIR_LEFT && dir!=DIR_DOWN)
    return hit;
  if (dir==DIR_RIGHT && rx!=0)
    x++;
  if (dir==DIR_DOWN && ry!=0)
    y++;
  if (dir==DIR_RIGHT || dir==DIR_LEFT)
    r=rx;
  else
    r=ry;
  if (emfield[y*MWIDTH+x]&emmask) {
    if (r==embox[dir]) {
      drawemerald(x*20+12,y*18+21);
      incpenalty();
    }
    if (r==embox[dir+1]) {
      eraseemerald(x*20+12,y*18+21);
      incpenalty();
      hit=true;
      emfield[y*MWIDTH+x]&=~emmask;
    }
  }
  return hit;
}

int16_t countem(void)
{
  int16_t x,y,n=0;
  for (x=0;x<MWIDTH;x++)
    for (y=0;y<MHEIGHT;y++)
      if (emfield[y*MWIDTH+x]&emmask)
        n++;
  return n;
}

void killemerald(int16_t x,int16_t y)
{
  if (emfield[(y+1)*MWIDTH+x]&emmask) {
    emfield[(y+1)*MWIDTH+x]&=~emmask;
    eraseemerald(x*20+12,(y+1)*18+21);
  }
}

static bool
getfirepflag(int n)
{
  return n==0 ? firepflag : fire2pflag;
}

int diggerx(int n)
{
  return digdat[n].dob.x;
}

int diggery(int n)
{
  return digdat[n].dob.y;
}

bool digalive(int n)
{
  return digdat[n].dob.alive;
}

void digresettime(int n)
{
  digdat[n].pub.bagtime=0;
}

bool isalive(void)
{
  int i;
  for (i=dgstate.curplayer;i<dgstate.diggers+dgstate.curplayer;i++)
    if (digdat[i].dob.alive)
      return true;
  return false;
}

int getlives(int pl)
{
  return digdat[pl].pub.lives;
}

void addlife(int pl)
{
  digdat[pl].pub.lives++;
  sound1up();
}

void initlives(void)
{
  int i;
  for (i=0;i<dgstate.diggers+dgstate.nplayers-1;i++)
    digdat[i].pub.lives=3;
}

void declife(int pl)
{
  if (!dgstate.gauntlet)
    digdat[pl].pub.lives--;
}
