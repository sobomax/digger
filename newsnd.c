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

static uint16_t t0rate;

#if !defined(newsnd_test)
extern int16_t spkrmode,pulsewidth;
#else
static int16_t spkrmode=0,pulsewidth=1;
#endif

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
#include <math.h>
#include "soundgen.h"

static struct sgen_state *ssp;
static unsigned int intmod;

int16_t getsample(void)
{

  if ((sgen_getstep(ssp) + 1) % intmod == 0)
    soundint();
  return (sgen_getsample(ssp));
}

void soundinitglob(uint16_t bufsize,uint16_t samprate)
{

  ssp = sgen_ctor(samprate, 2);
  assert(ssp != NULL);
  intmod = round(samprate / 72.8);
#if !defined(newsnd_test)
  setsounddevice(samprate,bufsize);
#endif
}

void s1setupsound(void)
{
#if !defined(newsnd_test)
  inittimer();
  soundint();
  initsounddevice();
#endif
}

void s1killsound(void)
{
#if !defined(newsnd_test)
  setsoundt2();
  timer2(40, false);
  pausesounddevice(true);
#endif
}

/* WARNING: Read only code ahead. Unless you're seriously into how the PC
   speaker and Digger's original low-level sound routines work, you shouldn't
   try to mess with, or even understand, the following. I don't understand most
   of it myself, and I wrote it. */

#define T0_BND 0
#define T2_BND 1

void s1timer2(uint16_t t2, bool mode)
{
  double rphase;

  if (t2 > 40 && t2 < 0x4000) {
    rphase = sgen_getphase(ssp, T2_BND);
    if (!mode) {
      sgen_setband(ssp, T2_BND, PIT_FREQ / t2, 1.0);
    } else {
      double frq;

      frq = (double)(PIT_FREQ / t2) - (double)(PIT_FREQ / t0rate);
      sgen_setband_mod(ssp, T2_BND, frq, 1.0, 0.0);
      if (sgen_setmuteband(ssp, T2_BND, 0))
        rphase = 0.0;
    }
    sgen_setphase(ssp, T2_BND, rphase);
  } else {
    sgen_setband(ssp, T2_BND, 0.0, 0.0);
  }
}

void s1soundoff(void)
{

  sgen_setmuteband(ssp, 0, 1);
  sgen_setmuteband(ssp, 1, 1);
}

void s1setspkrt2(void)
{

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

void s1timer0(uint16_t t0)
{
  double rphase;

  if (t0 > 40 && t0 < 0x4000) {
    rphase = sgen_getphase(ssp, 0);
    sgen_setband(ssp, 0, PIT_FREQ / t0, (pulsewidth - 1) / 49.0);
    sgen_setphase(ssp, 0, rphase);
  } else {
    sgen_setband(ssp, 0, 0.0, 0.0);
  }

  t0rate=t0;
}
