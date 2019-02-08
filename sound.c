/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include "def.h"
#include "sound.h"
#include "hardware.h"
#include "main.h"
#include "digger.h"
#include "input.h"

#if defined _SDL || defined _SDL_SOUND
#include <SDL.h>
#include "sdl_snd.h"
#endif

#if defined _VGL && !defined _SDL_SOUND
#include "fbsd_snd.h"
#endif

static int16_t wavetype=0,musvol=0;
static uint16_t t2val=0,t0val=0;
int16_t spkrmode=0,pulsewidth=1,volume=0;

static int8_t timerclock=0;

bool soundflag=true,musicflag=true;

static void soundlevdoneoff(void);
static void soundlevdoneupdate(void);
static void soundfallupdate(void);
static void soundbreakoff(void);
static void soundbreakupdate(void);
static void soundwobbleupdate(void);
static void soundfireupdate(void);
static void soundexplodeoff(int n);
static void soundexplodeupdate(void);
static void soundbonusupdate(void);
static void soundemoff(void);
static void soundemupdate(void);
static void soundemeraldoff(void);
static void soundemeraldupdate(void);
static void soundgoldoff(void);
static void soundgoldupdate(void);
static void soundeatmoff(void);
static void soundeatmupdate(void);
static void soundddieoff(void);
static void soundddieupdate(void);
static void sound1upoff(void);
static void sound1upupdate(void);
static void musicupdate(void);
static void sett0(bool);
static void setsoundmode(void);
static void s0setupsound(void);
static void s0killsound(void);

void (*setupsound)(void)=s0setupsound;
void (*killsound)(void)=s0killsound;
void (*soundoff)(void)=s0soundoff;
void (*setspkrt2)(void)=s0setspkrt2;
void (*timer0)(uint16_t t0v)=s0timer0;
void (*timer2)(uint16_t t2v, bool mod)=s0timer2;
void (*soundkillglob)(void)=s0soundkillglob;

static bool sndflag=false,soundpausedflag=false;

int32_t randvs;

int16_t randnos(int16_t n)
{
  randvs=randvs*0x15a4e35l+1;
  return (int16_t)((randvs&0x7fffffffl)%n);
}

void sett2val(int16_t t2v, bool mode)
{
  if (sndflag)
    timer2(t2v, mode);
}

static bool soundlevdoneflag=false;

void soundint(void)
{
  timerclock++;
  if (soundflag && !sndflag)
    sndflag=musicflag=true;
  if (!soundflag && sndflag) {
    sndflag=false;
    timer2(40, false);
    setsoundt2();
    soundoff();
  }
  if (sndflag && !soundpausedflag) {
    t0val=0x7d00;
    t2val=40;
    if (musicflag)
      musicupdate();
#ifdef ARM
    else
      soundoff();
#endif
#if !defined(NO_SND_EFFECTS)
    soundemeraldupdate();
    soundwobbleupdate();
    soundddieupdate();
    soundbreakupdate();
    soundgoldupdate();
    soundemupdate();
    soundexplodeupdate();
    soundfireupdate();
    soundeatmupdate();
    soundfallupdate();
    sound1upupdate();
    soundbonusupdate();
#endif
    if (t0val==0x7d00 || t2val!=40)
      setsoundt2();
    else {
      setsoundmode();
      sett0(false);
    }
    sett2val(t2val, false);
  }
  if (soundlevdoneflag)
    soundlevdoneupdate();
}

void soundstop(void)
{
  int i;
  soundfalloff();
  soundwobbleoff();
  for (i=0;i<FIREBALLS;i++)
    soundfireoff(i);
  musicoff();
  soundbonusoff();
  for (i=0;i<FIREBALLS;i++)
    soundexplodeoff(i);
  soundbreakoff();
  soundemoff();
  soundemeraldoff();
  soundgoldoff();
  soundeatmoff();
  soundddieoff();
  sound1upoff();
}

static int16_t nljpointer=0,nljnoteduration=0;

void soundlevdone(void)
{
  int16_t timer=0;
  soundstop();
  if (!sndflag)
    return;
  nljpointer=0;
  nljnoteduration=20;
  soundlevdoneflag=soundpausedflag=true;
  while (soundlevdoneflag && !escape) {
#if defined _SDL || defined _VGL
	if (!wave_device_available)
		soundlevdoneflag=false;
#endif
#if defined _SDL || defined _SDL_SOUND
    gethrt();	/* Let some CPU time go away */
#endif
    if (timerclock==timer)
      continue;
    checkkeyb();
    timer=timerclock;
  }
  soundlevdoneoff();
}

static void soundlevdoneoff(void)
{
  soundlevdoneflag=soundpausedflag=false;
}

static const int16_t newlevjingle[11]={0x8e8,0x712,0x5f2,0x7f0,0x6ac,0x54c,
                        0x712,0x5f2,0x4b8,0x474,0x474};

static void soundlevdoneupdate(void)
{

  if (sndflag) {
    if (nljpointer<11)
      t2val=newlevjingle[nljpointer];
    t0val=t2val+35;
    musvol=50;
    setsoundmode();
    sett0(true);
    sett2val(t2val, true);
    if (nljnoteduration>0)
      nljnoteduration--;
    else {
      nljnoteduration=20;
      nljpointer++;
      if (nljpointer>10)
        soundlevdoneoff();
    }
  }
  else
    soundlevdoneflag=false;
}


static bool soundfallflag=false,soundfallf=false;
static int16_t soundfallvalue,soundfalln=0;

void soundfall(void)
{
  soundfallvalue=1000;
  soundfallflag=true;
}

void soundfalloff(void)
{
  soundfallflag=false;
  soundfalln=0;
}

static void soundfallupdate(void)
{
  if (soundfallflag) {
    if (soundfalln<1) {
      soundfalln++;
      if (soundfallf)
        t2val=soundfallvalue;
    }
    else {
      soundfalln=0;
      if (soundfallf) {
        soundfallvalue+=50;
        soundfallf=false;
      }
      else
        soundfallf=true;
    }
  }
}


static bool soundbreakflag=false;
static int16_t soundbreakduration=0,soundbreakvalue=0;

void soundbreak(void)
{
  soundbreakduration=3;
  if (soundbreakvalue<15000)
    soundbreakvalue=15000;
  soundbreakflag=true;
}

static void soundbreakoff(void)
{
  soundbreakflag=false;
}

static void soundbreakupdate(void)
{
  if (soundbreakflag) {
    if (soundbreakduration!=0) {
      soundbreakduration--;
      t2val=soundbreakvalue;
    }
    else
      soundbreakflag=false;
  }
}


static bool soundwobbleflag=false;
static int16_t soundwobblen=0;

void soundwobble(void)
{
  soundwobbleflag=true;
}

void soundwobbleoff(void)
{
  soundwobbleflag=false;
  soundwobblen=0;
}

static void soundwobbleupdate(void)
{
  if (soundwobbleflag) {
    soundwobblen++;
    if (soundwobblen>63)
      soundwobblen=0;
    switch (soundwobblen) {
      case 0:
        t2val=0x7d0;
        break;
      case 16:
      case 48:
        t2val=0x9c4;
        break;
      case 32:
        t2val=0xbb8;
        break;
    }
  }
}


static bool soundfireflag[FIREBALLS]={false,false},sff[FIREBALLS];
static int16_t soundfirevalue[FIREBALLS],soundfiren[FIREBALLS]={0,0};
static int soundfirew=0;

void soundfire(int n)
{
  soundfirevalue[n]=500;
  soundfireflag[n]=true;
}

void soundfireoff(int n)
{
  soundfireflag[n]=false;
  soundfiren[n]=0;
}

static void soundfireupdate(void)
{
  int n;
  bool f=false;
  for (n=0;n<FIREBALLS;n++) {
    sff[n]=false;
    if (soundfireflag[n]) {
      if (soundfiren[n]==1) {
        soundfiren[n]=0;
        soundfirevalue[n]+=soundfirevalue[n]/55;
        sff[n]=true;
        f=true;
        if (soundfirevalue[n]>30000)
          soundfireoff(n);
      }
      else
        soundfiren[n]++;
    }
  }
  if (f) {
    do {
      n=soundfirew++;
      if (soundfirew==FIREBALLS)
        soundfirew=0;
    } while (!sff[n]);
    t2val=soundfirevalue[n]+randnos(soundfirevalue[n]>>3);
  }
}


static bool soundexplodeflag[FIREBALLS]={false,false},sef[FIREBALLS];
static int16_t soundexplodevalue[FIREBALLS],soundexplodeduration[FIREBALLS];
static int soundexplodew=0;

void soundexplode(int n)
{
  soundexplodevalue[n]=1500;
  soundexplodeduration[n]=10;
  soundexplodeflag[n]=true;
  soundfireoff(n);
}

static void soundexplodeoff(int n)
{
  soundexplodeflag[n]=false;
}

static void soundexplodeupdate(void)
{
  int n;
  bool f=false;
  for (n=0;n<FIREBALLS;n++) {
    sef[n]=false;
    if (soundexplodeflag[n]) {
      if (soundexplodeduration[n]!=0) {
        soundexplodevalue[n]=soundexplodevalue[n]-(soundexplodevalue[n]>>3);
        soundexplodeduration[n]--;
        sef[n]=true;
        f=true;
      }
      else
        soundexplodeflag[n]=false;
    }
  }
  if (f) {
    do {
      n=soundexplodew++;
      if (soundexplodew==FIREBALLS)
        soundexplodew=0;
    } while (!sef[n]);
    t2val=soundexplodevalue[n];
  }
}


static bool soundbonusflag=false;
static int16_t soundbonusn=0;

void soundbonus(void)
{
  soundbonusflag=true;
}

void soundbonusoff(void)
{
  soundbonusflag=false;
  soundbonusn=0;
}

static void soundbonusupdate(void)
{
  if (soundbonusflag) {
    soundbonusn++;
    if (soundbonusn>15)
      soundbonusn=0;
    if (soundbonusn>=0 && soundbonusn<6)
      t2val=0x4ce;
    if (soundbonusn>=8 && soundbonusn<14)
      t2val=0x5e9;
  }
}


static bool soundemflag=false;

void soundem(void)
{
  soundemflag=true;
}

static void soundemoff(void)
{
  soundemflag=false;
}

static void soundemupdate(void)
{
  if (soundemflag) {
    t2val=1000;
    soundemoff();
  }
}


static bool soundemeraldflag=false;
static int16_t soundemeraldduration,emerfreq,soundemeraldn;

static const int16_t emfreqs[8]={0x8e8,0x7f0,0x712,0x6ac,0x5f2,0x54c,0x4b8,0x474};

void soundemerald(int n)
{
  emerfreq=emfreqs[n];
  soundemeraldduration=7;
  soundemeraldn=0;
  soundemeraldflag=true;
}

static void soundemeraldoff(void)
{
  soundemeraldflag=false;
}

static void soundemeraldupdate(void)
{
  if (soundemeraldflag) {
    if (soundemeraldduration!=0) {
      if (soundemeraldn==0 || soundemeraldn==1)
        t2val=emerfreq;
      soundemeraldn++;
      if (soundemeraldn>7) {
        soundemeraldn=0;
        soundemeraldduration--;
      }
    }
    else
      soundemeraldoff();
  }
}


static bool soundgoldflag=false,soundgoldf=false;
static int16_t soundgoldvalue1,soundgoldvalue2,soundgoldduration;

void soundgold(void)
{
  soundgoldvalue1=500;
  soundgoldvalue2=4000;
  soundgoldduration=30;
  soundgoldf=false;
  soundgoldflag=true;
}

static void soundgoldoff(void)
{
  soundgoldflag=false;
}

static void soundgoldupdate(void)
{
  if (soundgoldflag) {
    if (soundgoldduration!=0)
      soundgoldduration--;
    else
      soundgoldflag=false;
    if (soundgoldf) {
      soundgoldf=false;
      t2val=soundgoldvalue1;
    }
    else {
      soundgoldf=true;
      t2val=soundgoldvalue2;
    }
    soundgoldvalue1+=(soundgoldvalue1>>4);
    soundgoldvalue2-=(soundgoldvalue2>>4);
  }
}



static bool soundeatmflag=false;
static int16_t soundeatmvalue,soundeatmduration,soundeatmn;

void soundeatm(void)
{
  soundeatmduration=20;
  soundeatmn=3;
  soundeatmvalue=2000;
  soundeatmflag=true;
}

static void soundeatmoff(void)
{
  soundeatmflag=false;
}

static void soundeatmupdate(void)
{
  if (soundeatmflag) {
    if (soundeatmn!=0) {
      if (soundeatmduration!=0) {
        if ((soundeatmduration%4)==1)
          t2val=soundeatmvalue;
        if ((soundeatmduration%4)==3)
          t2val=soundeatmvalue-(soundeatmvalue>>4);
        soundeatmduration--;
        soundeatmvalue-=(soundeatmvalue>>4);
      }
      else {
        soundeatmduration=20;
        soundeatmn--;
        soundeatmvalue=2000;
      }
    }
    else
      soundeatmflag=false;
  }
}


static bool soundddieflag=false;
static int16_t soundddien,soundddievalue;

void soundddie(void)
{
  soundddien=0;
  soundddievalue=20000;
  soundddieflag=true;
}

static void soundddieoff(void)
{
  soundddieflag=false;
}

static void soundddieupdate(void)
{
  if (soundddieflag) {
    soundddien++;
    if (soundddien==1)
      musicoff();
    if (soundddien>=1 && soundddien<=10)
      soundddievalue=20000-soundddien*1000;
    if (soundddien>10)
      soundddievalue+=500;
    if (soundddievalue>30000)
      soundddieoff();
    t2val=soundddievalue;
  }
}


static bool sound1upflag=false;
static int16_t sound1upduration=0;

void sound1up(void)
{
  sound1upduration=96;
  sound1upflag=true;
}

static void sound1upoff(void)
{
  sound1upflag=false;
}

static void sound1upupdate(void)
{
  if (sound1upflag) {
    if ((sound1upduration/3)%2!=0)
      t2val=(sound1upduration<<2)+600;
    sound1upduration--;
    if (sound1upduration<1)
      sound1upflag=false;
  }
}


static bool musicplaying=false;
static int16_t musicp=0,tuneno=0,noteduration=0,notevalue=0,musicmaxvol=0,
      musicattackrate=0,musicsustainlevel=0,musicdecayrate=0,musicnotewidth=0,
      musicreleaserate=0,musicstage=0,musicn=0,musicdfac=0;

bool sounddiedone = true;

void music(int16_t tune, double dfac)
{
  tuneno=tune;
  musicp=0;
  noteduration=0;

  if (!sndflag)
    return;
  switch (tune) {
    case 0:
      musicmaxvol=50;
      musicattackrate=20;
      musicsustainlevel=20;
      musicdecayrate=10;
      musicreleaserate=4;
      musicdfac = 3.0 * dfac;
      break;
    case 1:
      musicmaxvol=50;
      musicattackrate=50;
      musicsustainlevel=8;
      musicdecayrate=15;
      musicreleaserate=1;
      musicdfac = 6.0 * dfac;
      break;
    case 2:
      musicmaxvol=50;
      musicattackrate=50;
      musicsustainlevel=25;
      musicdecayrate=5;
      musicreleaserate=1;
      musicdfac = 10.0 * dfac;
  }
  musicplaying=true;
  if (tune==2) {
    soundddieoff();
#if defined _SDL || defined _VGL
    if (!wave_device_available)
      return;
#endif
    sounddiedone = false;
  }
}

void musicoff(void)
{
  musicplaying=false;
  musicp=0;
}

static const int16_t bonusjingle[321]={
  0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,
   0xd59,4, 0xbe4,4, 0xa98,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,
  0x11d1,4, 0xd59,2, 0xa98,2, 0xbe4,4, 0xe24,4,0x11d1,4,0x11d1,2,0x11d1,2,
  0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2, 0xd59,4, 0xbe4,4,
   0xa98,4, 0xd59,2, 0xa98,2, 0x8e8,10,0xa00,2, 0xa98,2, 0xbe4,2, 0xd59,4,
   0xa98,4, 0xd59,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,
  0x11d1,2,0x11d1,2, 0xd59,4, 0xbe4,4, 0xa98,4,0x11d1,2,0x11d1,2,0x11d1,4,
  0x11d1,2,0x11d1,2,0x11d1,4, 0xd59,2, 0xa98,2, 0xbe4,4, 0xe24,4,0x11d1,4,
  0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,0x11d1,4,0x11d1,2,0x11d1,2,
   0xd59,4, 0xbe4,4, 0xa98,4, 0xd59,2, 0xa98,2, 0x8e8,10,0xa00,2, 0xa98,2,
   0xbe4,2, 0xd59,4, 0xa98,4, 0xd59,4, 0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2,
   0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4,
   0xa98,4, 0x7f0,4, 0xa98,4, 0xbe4,4, 0xd59,4, 0xe24,4, 0xfdf,4, 0xa98,2,
   0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4,
   0x7f0,4, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4, 0x8e8,4, 0x970,4, 0x8e8,4,
   0x970,4, 0x8e8,4, 0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4,
   0xa98,2, 0xa98,2, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4, 0xa98,4, 0x7f0,4,
   0xa98,4, 0xbe4,4, 0xd59,4, 0xe24,4, 0xfdf,4, 0xa98,2, 0xa98,2, 0xa98,4,
   0xa98,2, 0xa98,2, 0xa98,4, 0xa98,2, 0xa98,2, 0xa98,4, 0x7f0,4, 0xa98,4,
   0x7f0,4, 0xa98,4, 0x7f0,4, 0x8e8,4, 0x970,4, 0x8e8,4, 0x970,4, 0x8e8,4,
  0x7d64};

static const int16_t backgjingle[291]={
   0xfdf,2,0x11d1,2, 0xfdf,2,0x1530,2,0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2,
  0x11d1,2, 0xfdf,2,0x1530,2,0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2, 0xe24,2,
   0xd59,2, 0xe24,2, 0xd59,2, 0xfdf,2, 0xe24,2, 0xfdf,2, 0xe24,2,0x11d1,2,
   0xfdf,2,0x11d1,2, 0xfdf,2,0x1400,2, 0xfdf,4, 0xfdf,2,0x11d1,2, 0xfdf,2,
  0x1530,2,0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2,0x11d1,2, 0xfdf,2,0x1530,2,
  0x1ab2,2,0x1530,2,0x1fbf,4, 0xfdf,2, 0xe24,2, 0xd59,2, 0xe24,2, 0xd59,2,
   0xfdf,2, 0xe24,2, 0xfdf,2, 0xe24,2,0x11d1,2, 0xfdf,2,0x11d1,2, 0xfdf,2,
   0xe24,2, 0xd59,4, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,
  0x1530,4, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,0x1530,4,
   0xa98,2, 0x970,2, 0x8e8,2, 0x970,2, 0x8e8,2, 0xa98,2, 0x970,2, 0xa98,2,
   0x970,2, 0xbe4,2, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2, 0xa98,4, 0xa98,2,
   0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,0x1530,4, 0xa98,2, 0xbe4,2,
   0xa98,2, 0xd59,2,0x11d1,2, 0xd59,2,0x1530,4, 0xa98,2, 0x970,2, 0x8e8,2,
   0x970,2, 0x8e8,2, 0xa98,2, 0x970,2, 0xa98,2, 0x970,2, 0xbe4,2, 0xa98,2,
   0xbe4,2, 0xa98,2, 0xd59,2, 0xa98,4, 0x7f0,2, 0x8e8,2, 0xa98,2, 0xd59,2,
  0x11d1,2, 0xd59,2,0x1530,4, 0xa98,2, 0xbe4,2, 0xa98,2, 0xd59,2,0x11d1,2,
   0xd59,2,0x1530,4, 0xa98,2, 0x970,2, 0x8e8,2, 0x970,2, 0x8e8,2, 0xa98,2,
   0x970,2, 0xa98,2, 0x970,2, 0xbe4,2, 0xa98,2, 0xbe4,2, 0xd59,2, 0xbe4,2,
   0xa98,4,0x7d64};

static const int16_t dirge[]={
  0x7d00, 2,0x11d1, 6,0x11d1, 4,0x11d1, 2,0x11d1, 6, 0xefb, 4, 0xfdf, 2,
   0xfdf, 4,0x11d1, 2,0x11d1, 4,0x12e0, 2,0x11d1,12,0x7d00,16,0x7d00,16,
  0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,0x7d00,16,
  0x7d00,16,0x7d00,16,0x7d00,16,0x7d64};

static void musicupdate(void)
{
  if (!musicplaying)
    return;
  if (noteduration!=0)
    noteduration--;
  else {
    musicstage=musicn=0;
    switch (tuneno) {
      case 0:
        noteduration=bonusjingle[musicp+1]*musicdfac;
        musicnotewidth=noteduration-musicdfac;
        notevalue=bonusjingle[musicp];
        musicp+=2;
        if (bonusjingle[musicp]==0x7d64)
          musicp=0;
        break;
      case 1:
        noteduration=backgjingle[musicp+1]*musicdfac;
        musicnotewidth=musicdfac * 2;
        notevalue=backgjingle[musicp];
        musicp+=2;
        if (backgjingle[musicp]==0x7d64)
          musicp=0;
        break;
      case 2:
        noteduration=dirge[musicp+1]*musicdfac;
        musicnotewidth=noteduration-musicdfac;
        notevalue=dirge[musicp];
	if (musicp > 0 && notevalue==0x7d00)
	  sounddiedone = true;
        musicp+=2;
        if (dirge[musicp]==0x7d64)
          musicp=0;
        break;
    }
  }
  musicn++;
  wavetype=1;
  t0val=notevalue;
  if (musicn>=musicnotewidth)
    musicstage=2;
  switch(musicstage) {
    case 0:
      if (musvol+musicattackrate>=musicmaxvol) {
        musicstage=1;
        musvol=musicmaxvol;
        break;
      }
      musvol+=musicattackrate;
      break;
    case 1:
      if (musvol-musicdecayrate<=musicsustainlevel) {
        musvol=musicsustainlevel;
        break;
      }
      musvol-=musicdecayrate;
      break;
    case 2:
      if (musvol-musicreleaserate<=1) {
        musvol=1;
        break;
      }
      musvol-=musicreleaserate;
  }
  if (musvol==1)
    t0val=0x7d00;
}


void soundpause(void)
{
  soundpausedflag=true;
#if defined _SDL || defined _SDL_SOUND
  SDL_PauseAudio(1);
#endif
}

void soundpauseoff(void)
{
  soundpausedflag=false;
#if defined _SDL || defined _SDL_SOUND
  SDL_PauseAudio(0);
#endif
}

static void sett0(bool mode)
{
  if (sndflag) {
    if (!mode)
      timer2(t2val, mode);
    if (t0val<1000 && (wavetype==1 || wavetype==2))
      t0val=1000;
    if (musvol<1)
      musvol=1;
    if (musvol>50)
      musvol=50;
    pulsewidth=musvol*volume;
    timer0(t0val);
    setsoundmode();
  }
}

static bool soundt0flag=false;

void setsoundt2(void)
{
  if (soundt0flag) {
    spkrmode=0;
    soundt0flag=false;
    setspkrt2();
  }
}

void setsoundmode(void)
{
  spkrmode=wavetype;
  if (!soundt0flag && sndflag) {
    soundt0flag=true;
    setspkrt2();
  }
}

void initsound(void)
{
  timer2(40, false);
  setspkrt2();
  timer0(0);
  wavetype=2;
  t0val=12000;
  musvol=8;
  t2val=40;
  soundt0flag=true;
  sndflag=true;
  spkrmode=0;
  setsoundt2();
  soundstop();
  setupsound();
  timer0(0x4000);
  randvs=getlrt();
}

static void s0killsound(void)
{
  setsoundt2();
  timer2(40, false);
}

static void s0setupsound(void)
{
  inittimer();
  curtime=0;
}
