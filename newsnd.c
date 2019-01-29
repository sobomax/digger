/* Digger Remastered
   Copyright (c) Andrew Jenner 1998-2004 */

#include <stdlib.h>
#include "def.h"
#include "device.h"
#include "hardware.h"
#include "digger.h"
#if !defined(newsnd_test)
#include "sound.h"
#endif
#include "newsnd.h"

#define PIT_FREQ 0x1234ddul

/* The function which empties the circular buffer should get samples from
   buffer[firsts] and then do firsts=(firsts+1)&(size-1); This function is
   responsible for incrementing first samprate times per second (on average)
   (if it's a little out, the sound will simply run too fast or too slow). It
   must not take more than (last-firsts-1)&(size-1) samples at once, or the
   sound will break up.

   If DMA is used, doubling the buffer so the data is always continguous, and
   giving half of the buffer at once to the DMA driver may be a good idea. */

static int rate;
static uint16_t t0rate,t2rate,t2new,t0v,t2v;
static int16_t i8pulse=0;
static bool t2f=false,t2sw,i8flag=false;
static uint8_t lut[257];
static uint16_t pwlut[51];
static uint16_t timercount = 0;

#if !defined(newsnd_test)
extern int16_t spkrmode,pulsewidth;
#else
static int16_t spkrmode=0,timerrate=0x7d0,pulsewidth=1;
#endif

static uint8_t getsampleX(void);

/* Initialise circular buffer and PC speaker emulator

   bufsize = buffer size in samples
   samprate = play rate in Hz

   samprate is directly proportional to the sound quality. This should be the
   highest value the hardware can support without slowing down the program too
   much. Ensure 0x1234<samprate<=0x1234dd and that samprate is a factor of
   0x1234dd (or you won't get the rate you want). For example, a value of
   44100 equates to about 44192Hz (a .2% difference - negligable unless you're
   trying to harmonize with a computer running at a different rate, or another
   musical instrument...)

   The lag time is bufsize/samprate seconds. This should be the smallest value
   which does not make the sound break up. There may also be DMA considerations
   to take into account. bufsize should also be a power of 2.
*/

#include <assert.h>
#include "soundgen.h"

static struct sgen_state *ssp;

int16_t getsample(void)
{

  getsampleX();
  return (sgen_getsample(ssp));
}

void soundinitglob(uint16_t bufsize,uint16_t samprate)
{
  int i;

  ssp = sgen_ctor(samprate, 2);
  assert(ssp != NULL);
#if !defined(newsnd_test)
  setsounddevice(samprate,bufsize);
#endif
  rate=(int)(PIT_FREQ/(uint32_t)samprate);
  t2sw=false;     /* As it should be left */
  for (i=0;i<=rate;i++) {
    //j = ((MIN_SAMP - MAX_SAMP) / 2) + (i * (MAX_SAMP - MIN_SAMP)) / rate;
    //lut[i] = (j >= 0) ? (uint8_t)j : (uint8_t)(-j);
    lut[i]=(uint8_t)(MIN_SAMP+(i*(MAX_SAMP-MIN_SAMP))/rate);
  }
  for (i=1;i<=50;i++)
    pwlut[i]=(16+i*18)>>2; /* Counted timer ticks in original */
}

void s1setupsound(void)
{
#if !defined(newsnd_test)
  inittimer();
  curtime=0;
  startint8();
  soundint();
  initsounddevice();
#endif
}

void s1killsound(void)
{
#if !defined(newsnd_test)
  setsoundt2();
  timer2(40, false);
  stopint8();
  killsounddevice();
#endif
}

/* This function is called regularly by the Digger engine to keep the circular
   buffer filled. */

void s1fillbuffer(void)
{
}

/* WARNING: Read only code ahead. Unless you're seriously into how the PC
   speaker and Digger's original low-level sound routines work, you shouldn't
   try to mess with, or even understand, the following. I don't understand most
   of it myself, and I wrote it. */

void s1settimer2(uint16_t t2, bool mode)
{

  if (t2 > 40 && t2 < 0x4000) {
    if (!mode) {
      sgen_setband(ssp, 1, PIT_FREQ / t2, 1.0);
    } else {
      double frq;

      frq = (double)(PIT_FREQ / t2) - (double)(PIT_FREQ / t0rate);
      sgen_setband_mod(ssp, 1, frq, 0.0, 1.0);
      sgen_setmuteband(ssp, 1, 0);
    }
  } else {
    sgen_setband(ssp, 1, 0.0, 0.0);
  }

  if (t2==40)
    t2=rate;   /* Otherwise aliasing would cause noise artifacts */
  t2>>=1;
  t2v=t2new=t2;
}

void s1soundoff(void)
{
  t2sw=false;
  sgen_setmuteband(ssp, 0, 1);
  sgen_setmuteband(ssp, 1, 1);
}

void s1setspkrt2(void)
{
  t2sw=true;
  if (spkrmode == 0) {
      sgen_setmuteband(ssp, 0, 1);
      sgen_setmuteband(ssp, 1, 0);
  } else if (spkrmode == 1) {
      sgen_setmuteband(ssp, 0, 0);
      sgen_setmuteband(ssp, 1, 1);
  } else {
      sgen_setmuteband(ssp, 0, 1);
      sgen_setmuteband(ssp, 1, 1);
  }
}

void s1settimer0(uint16_t t0)
{
  s1timer0(t0);
  t0v=t0;
}

void s1timer0(uint16_t t0)
{

  if (t0 > 40 && t0 < 0x4000) {
    sgen_setband(ssp, 0, PIT_FREQ / t0, (pulsewidth - 1) / 49.0);
  } else {
    sgen_setband(ssp, 0, 0.0, 0.0);
  }

  t0rate=t0;
}

void s1timer2(uint16_t t2, bool mode)
{

  s1settimer2(t2, mode);
  t2rate=t2;
}

static bool addcarry(uint16_t *dest,uint16_t add)
{
  *dest+=add;
  if (*dest<add)
    return true;
  return false;
}

static bool subcarry(uint16_t *dest,uint16_t sub)
{
  *dest-=sub;
  if (*dest>=(uint16_t)(-sub))
    return true;
  return false;
}

/* This function is the workhorse.
   It emulates the functionality of:
    * the 8253 Programmable Interval Timer
    * the PC speaker hardware
    * the IRQ0 timer interrupt which Digger reprograms
   It averages the speaker values over the entire time interval to get the
   sample.
   Despite its complexity, it runs pretty fast, since most of the time, it
   doesn't actually do very much, and when it does stuff, it uses look-up
   tables.
   There are probably fencepost errors but I challenge anyone to detect these
   audibly.
   Some would just calculate each bit separately and add them up, but there
   are 1,193,181 bits to add up per second, so you'd need a fast PC. This may
   be a little more complicated, but its much faster.
*/

static uint8_t getsampleX(void)
{
  bool f=false,t2sw0;
  uint16_t spkrt2=0,noi8=0,complicate=0,not2=0;

  if (subcarry(&t2v,rate)) {
    not2=t2v+rate; /* Amount of time that went by before change */
    if (t2f) {
      spkrt2=-t2v; /* MIN_SAMPs at beginning */
      t2rate=t2new;
      if (t2rate==(rate>>1))
        t2v=t2rate;
    }
    else                  /* MIN_SAMPs at end */
      spkrt2=t2v+rate;
    t2v+=t2rate;
    if (t2rate==(rate>>1))
      t2v=t2rate;
    else
      t2f=!t2f;
    complicate|=1;
  }

  if (subcarry(&t0v,rate)) { /* Effectively using mode 2 here */
    i8flag=true;
    noi8=t0v+rate; /* Amount of time that went by before interrupt */
    t0v+=t0rate;
    complicate|=2;
  }

  t2sw0=t2sw;

  if (i8flag && i8pulse<=0) {
    f=true;
    if (spkrmode!=0) {
      if (spkrmode!=1)
        t2sw=!t2sw;
      else {
        i8pulse=pwlut[pulsewidth];
        t2sw=true;
        f=false;
      }
    }
  }

  if (i8pulse>0) {
    complicate|=4;
    i8pulse-=rate;
    if (i8pulse<=0) {
      complicate|=8;
      t2sw=false;
      i8flag=true;
      f=true;
    }
  }

  if (f) {
    if (addcarry(&timercount,timerrate)) {
#if !defined(newsnd_test)
      soundint(); /* Update music and sound effects 72.8 Hz */
#endif
      timercount-=0x4000;
    }
    i8flag=false;
  }

  if (!(complicate&1) && t2f)
    return MIN_SAMP;

  /* 12 unique cases, no break statements!
     No more than about 6 of these lines are executed on any single call. */

  switch (complicate) {
    case 2: /* Int8 happened */
      if (t2sw!=t2sw0) {
        if (t2sw) /* <==> !t2sw0 */
          return lut[rate-noi8];
        return lut[noi8];
      }
      /* Fall through */
    case 0: /* Nothing happened! */
      if (!t2sw)
        return MIN_SAMP;
      /* Fall through */
    case 4: /* Int8 is pulsing => t2sw */
      return MAX_SAMP;
    case 1: /* The t2 wave changed */
      if (!t2sw)
        return MIN_SAMP;
      /* Fall through */
    case 5: /* The t2 wave changed and Int8 is pulsing => t2sw */
      return lut[spkrt2];
    case 3: /* Int8 happened and t2 wave changed */
      if (!t2sw0 && !t2sw)
        return MIN_SAMP;    /* both parts are off */
      if (t2sw0 && t2sw)
        return lut[spkrt2]; /* both parts are on */
      if (not2<noi8)  /* t2 happened first */
        if (t2sw0) /* "on" part is before i8 */
          if (t2f)
            return lut[spkrt2]; /* MIN_SAMPs at end */
          else
            return lut[spkrt2-(rate-noi8)]; /* MIN_SAMPs at beginning */
        else      /* "on" part is after i8 => constant */
          if (t2f)
            return MIN_SAMP; /* MIN_SAMPs at end */
          else
            return lut[rate-noi8]; /* MIN_SAMPs at beginning */
      else /* i8 happened first */
        if (t2sw0) /* "on" part is before i8 => constant */
          if (t2f)
            return MIN_SAMP; /* MIN_SAMPs at beginning */
          else
            return lut[noi8]; /* MIN_SAMPs at end */
        else       /* "on" part is after i8 */
          if (t2f)
            return lut[spkrt2]; /* MIN_SAMPs at beginning */
          else
            return lut[spkrt2-noi8]; /* MIN_SAMPs at end */
    case 6: /* The Int8 pulse started */
      if (t2sw0)
        return MAX_SAMP;
      return lut[rate-noi8];
    case 7: /* The Int8 pulse started and the t2 wave changed */
      if (t2sw0)
        return lut[spkrt2];
      if (not2<noi8)  /* t2 happened first */
        if (t2f)
          return MIN_SAMP; /* MIN_SAMPs at end */
        else
          return lut[rate-noi8]; /* MIN_SAMPs at beginning */
      else /* i8 happened first */
        if (t2f)
          return lut[spkrt2]; /* MIN_SAMPs at beginning */
        else
          return lut[spkrt2-noi8]; /* MIN_SAMPs at end */
    case 12: /* The Int8 pulse stopped */
      if (t2sw)
        return MAX_SAMP;
      return lut[i8pulse+rate];
    case 13: /* The Int8 pulse stopped and the t2 wave changed */
      if (t2sw)
        return lut[spkrt2];
      if (not2<i8pulse+rate) /* t2 happened first */
        if (t2f)
          return lut[spkrt2+i8pulse]; /* MIN_SAMPs at beginning */
        else
          return lut[spkrt2];         /* MIN_SAMPs at end */
      else /* i8pulse ended first */
        if (t2f)
          return MIN_SAMP; /* MIN_SAMPs at beginning */
        else
          return lut[i8pulse+rate];
    case 14: /* The Int8 pulse started and stopped in the same sample */
      if (t2sw0)
        if (t2sw)
          return MAX_SAMP;
        else
          return lut[noi8+i8pulse+rate];
      else
        if (t2sw)
          return lut[rate-noi8];
        else
          return lut[i8pulse+rate];
    case 15: /* Everything happened at once */
      if (not2<noi8) /* First subcase: t2 happens before pulse */
        if (t2f) /* MIN_SAMPs at beginning */
          if (t2sw0)
            if (t2sw)
              return lut[spkrt2];
            else
              return lut[spkrt2+noi8+i8pulse];
          else
            if (t2sw)
              return lut[rate-noi8];
            else
              return lut[i8pulse+rate];
        else /* MIN_SAMPs at end */
          if (t2sw0) /* No need to test t2sw */
            return lut[spkrt2];
          else
            return MIN_SAMP;
      else
        if (not2<rate+noi8+i8pulse) /* Subcase 2: t2 happens during pulse */
          if (t2f) /* MIN_SAMPs at beginning */
            if (t2sw) /* No need to test t2sw0 */
              return lut[spkrt2];
            else
              return lut[spkrt2+noi8+i8pulse];
          else /* MIN_SAMPs at end */
            if (t2sw0) /* No need to test t2sw */
              return lut[spkrt2];
            else
              return lut[spkrt2-noi8];
        else /* Third subcase: t2 happens after pulse */
          if (t2f) /* MIN_SAMPs at beginning */
            if (t2sw) /* No need to test t2sw0 */
              return lut[spkrt2];
            else
              return MIN_SAMP;
          else /* MIN_SAMPs at end */
            if (t2sw0)
              if (t2sw)
                return lut[spkrt2];
              else
                return lut[noi8+i8pulse+rate];
            else
              if (t2sw)
                return lut[spkrt2-noi8];
              else
                return lut[i8pulse+rate];
  }
  return MIN_SAMP; /* This should never happen */
}

#if defined(newsnd_test)
int
newsnd_test(void)
{
  int i, ps, ntrans;
  uint8_t *s;
  FILE *of;

  s = malloc(62799 * 1000);

  soundinitglob(1024, 62799);
  s1settimer2(210, false);
  t2rate = timerrate = 6834;
  t2sw = true;
  pulsewidth = 6;

  for (i = 0; i < 62799 * 1000; i++) {
    s[i] = getsampleX();
  }
  of = fopen("newsnd_test.out", "w");
  assert(of != NULL);
  assert(fwrite(s, 62799 * 1000, 1, of) == 1);
  assert(fclose(of) == 0);
}
#endif
