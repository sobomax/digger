#include <stdlib.h>
#include "def.h"
#include "sprite.h"
#include "hardware.h"
#include "draw_api.h"

bool retrflag=true;

bool sprrdrwf[SPRITES+1];
bool sprrecf[SPRITES+1];
bool sprenf[SPRITES];
int16_t sprch[SPRITES+1];
uint8_t *sprmov[SPRITES];
int16_t sprx[SPRITES+1];
int16_t spry[SPRITES+1];
int16_t sprwid[SPRITES+1];
int16_t sprhei[SPRITES+1];
int16_t sprbwid[SPRITES];
int16_t sprbhei[SPRITES];
int16_t sprnch[SPRITES];
int16_t sprnwid[SPRITES];
int16_t sprnhei[SPRITES];
int16_t sprnbwid[SPRITES];
int16_t sprnbhei[SPRITES];

void clearrdrwf(void);
void clearrecf(void);
void setrdrwflgs(int16_t n);
bool collide(int16_t bx,int16_t si);
bool bcollide(int16_t bx,int16_t si);
void putims(void);
void putis(void);
void bcollides(int bx);

static struct digger_draw_api dda_static = {
  .ginit = &vgainit,
  .gclear = &vgaclear,
  .gpal = &vgapal,
  .ginten = &vgainten,
  .gputi = &vgaputi,
  .ggeti = &vgageti,
  .gputim = &vgaputim,
  .ggetpix = &vgagetpix,
  .gtitle = &vgatitle,
  .gwrite = &vgawrite
};

struct digger_draw_api *ddap = &dda_static;

void setretr(bool f)
{
  retrflag=f;
}

void createspr(int16_t n,int16_t ch,uint8_t *mov,int16_t wid,int16_t hei,int16_t bwid,
               int16_t bhei)
{
  sprnch[n]=sprch[n]=ch;
  sprmov[n]=mov;
  sprnwid[n]=sprwid[n]=wid;
  sprnhei[n]=sprhei[n]=hei;
  sprnbwid[n]=sprbwid[n]=bwid;
  sprnbhei[n]=sprbhei[n]=bhei;
  sprenf[n]=false;
}

void movedrawspr(int16_t n,int16_t x,int16_t y)
{
  sprx[n]=x&-4;
  spry[n]=y;
  sprch[n]=sprnch[n];
  sprwid[n]=sprnwid[n];
  sprhei[n]=sprnhei[n];
  sprbwid[n]=sprnbwid[n];
  sprbhei[n]=sprnbhei[n];
  clearrdrwf();
  setrdrwflgs(n);
  putis();
  ddap->ggeti(sprx[n],spry[n],sprmov[n],sprwid[n],sprhei[n]);
  sprenf[n]=true;
  sprrdrwf[n]=true;
  putims();
}

void erasespr(int16_t n)
{
  if (!sprenf[n])
    return;
  ddap->gputi(sprx[n],spry[n],sprmov[n],sprwid[n],sprhei[n]);
  sprenf[n]=false;
  clearrdrwf();
  setrdrwflgs(n);
  putims();
}

void drawspr(int16_t n,int16_t x,int16_t y)
{
  int16_t t1,t2,t3,t4;
  x&=-4;
  clearrdrwf();
  setrdrwflgs(n);
  t1=sprx[n];
  t2=spry[n];
  t3=sprwid[n];
  t4=sprhei[n];
  sprx[n]=x;
  spry[n]=y;
  sprwid[n]=sprnwid[n];
  sprhei[n]=sprnhei[n];
  clearrecf();
  setrdrwflgs(n);
  sprhei[n]=t4;
  sprwid[n]=t3;
  spry[n]=t2;
  sprx[n]=t1;
  sprrdrwf[n]=true;
  putis();
  sprenf[n]=true;
  sprx[n]=x;
  spry[n]=y;
  sprch[n]=sprnch[n];
  sprwid[n]=sprnwid[n];
  sprhei[n]=sprnhei[n];
  sprbwid[n]=sprnbwid[n];
  sprbhei[n]=sprnbhei[n];
  ddap->ggeti(sprx[n],spry[n],sprmov[n],sprwid[n],sprhei[n]);
  putims();
  bcollides(n);
}

void initspr(int16_t n,int16_t ch,int16_t wid,int16_t hei,int16_t bwid,int16_t bhei)
{
  sprnch[n]=ch;
  sprnwid[n]=wid;
  sprnhei[n]=hei;
  sprnbwid[n]=bwid;
  sprnbhei[n]=bhei;
}

void initmiscspr(int16_t x,int16_t y,int16_t wid,int16_t hei)
{
  sprx[SPRITES]=x;
  spry[SPRITES]=y;
  sprwid[SPRITES]=wid;
  sprhei[SPRITES]=hei;
  clearrdrwf();
  setrdrwflgs(SPRITES);
  putis();
}

void getis(void)
{
  int16_t i;
  for (i=0;i<SPRITES;i++)
    if (sprrdrwf[i])
      ddap->ggeti(sprx[i],spry[i],sprmov[i],sprwid[i],sprhei[i]);
  putims();
}

void drawmiscspr(int16_t x,int16_t y,int16_t ch,int16_t wid,int16_t hei)
{
  sprx[SPRITES]=x&-4;
  spry[SPRITES]=y;
  sprch[SPRITES]=ch;
  sprwid[SPRITES]=wid;
  sprhei[SPRITES]=hei;
  ddap->gputim(sprx[SPRITES],spry[SPRITES],sprch[SPRITES],sprwid[SPRITES],
         sprhei[SPRITES]);
}

void clearrdrwf(void)
{
  int16_t i;
  clearrecf();
  for (i=0;i<SPRITES+1;i++)
    sprrdrwf[i]=false;
}

void clearrecf(void)
{
  int16_t i;
  for (i=0;i<SPRITES+1;i++)
    sprrecf[i]=false;
}

void setrdrwflgs(int16_t n)
{
  int16_t i;
  if (!sprrecf[n]) {
    sprrecf[n]=true;
    for (i=0;i<SPRITES;i++)
      if (sprenf[i] && i!=n) {
        if (collide(i,n)) {
          sprrdrwf[i]=true;
          setrdrwflgs(i);
        }
      }
  }
}

bool collide(int16_t bx,int16_t si)
{
  if (sprx[bx]>=sprx[si]) {
    if (sprx[bx]>(sprwid[si]<<2)+sprx[si]-1)
      return false;
  }
  else
    if (sprx[si]>(sprwid[bx]<<2)+sprx[bx]-1)
      return false;
  if (spry[bx]>=spry[si]) {
    if (spry[bx]<=sprhei[si]+spry[si]-1)
      return true;
    return false;
  }
  if (spry[si]<=sprhei[bx]+spry[bx]-1)
    return true;
  return false;
}

bool bcollide(int16_t bx,int16_t si)
{
  if (sprx[bx]>=sprx[si]) {
    if (sprx[bx]+sprbwid[bx]>(sprwid[si]<<2)+sprx[si]-sprbwid[si]-1)
      return false;
  }
  else
    if (sprx[si]+sprbwid[si]>(sprwid[bx]<<2)+sprx[bx]-sprbwid[bx]-1)
      return false;
  if (spry[bx]>=spry[si]) {
    if (spry[bx]+sprbhei[bx]<=sprhei[si]+spry[si]-sprbhei[si]-1)
      return true;
    return false;
  }
  if (spry[si]+sprbhei[si]<=sprhei[bx]+spry[bx]-sprbhei[bx]-1)
    return true;
  return false;
}

void putims(void)
{
  int i;
  for (i=0;i<SPRITES;i++)
    if (sprrdrwf[i])
      ddap->gputim(sprx[i],spry[i],sprch[i],sprwid[i],sprhei[i]);
}

void putis(void)
{
  int i;
  for (i=0;i<SPRITES;i++)
    if (sprrdrwf[i])
      ddap->gputi(sprx[i],spry[i],sprmov[i],sprwid[i],sprhei[i]);
}

int first[TYPES],coll[SPRITES];
int firstt[TYPES]={FIRSTBONUS,FIRSTBAG,FIRSTMONSTER,FIRSTFIREBALL,FIRSTDIGGER};
int lastt[TYPES]={LASTBONUS,LASTBAG,LASTMONSTER,LASTFIREBALL,LASTDIGGER};

void bcollides(int spr)
{
  int spc,next,i;
  for (next=0;next<TYPES;next++)
    first[next]=-1;
  for (next=0;next<SPRITES;next++)
    coll[next]=-1;
  for (i=0;i<TYPES;i++) {
    next=-1;
    for (spc=firstt[i];spc<lastt[i];spc++)
      if (sprenf[spc] && spc!=spr)
        if (bcollide(spr,spc)) {
          if (next==-1)
            first[i]=next=spc;
          else
            coll[next=(coll[next]=spc)]=-1;
	}
  }
}
