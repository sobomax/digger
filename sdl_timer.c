#include <math.h>
#if defined(DIGGER_DEBUG)
#include <stdio.h>
#endif

#include <SDL.h>

#include "def.h"
#include "digger_math.h"
#ifdef _SDL
#include "sdl_vid.h"
#endif
#if defined(DIGGER_DEBUG)
#include "digger_log.h"
#endif

static struct PFD phase_detector;
static struct recfilter *loop_error;

extern uint32_t ftime;

void inittimer(void)
{
    double tfreq;

    tfreq = 1000000.0 / ftime;
    loop_error = recfilter_init(tfreq, 0.1);
    PFD_init(&phase_detector, 0.0);
#if defined(DIGGER_DEBUG)
    fprintf(digger_log, "inittimer: ftime = %u\n", ftime);
#endif
}

int32_t getlrt(void)
{
	return(0);
}

uint32_t gethrt(void)
{
    uint32_t add_delay;
    double eval, clk_rl, tfreq, add_delay_d, filterval;
    static double cum_error = 0.0;

    if (ftime <= 1) {
        doscreenupdate();
        return (0);
    }
    tfreq = 1000000.0 / ftime;
    clk_rl = (double)SDL_GetTicks() * tfreq / 1000.0;
    eval = PFD_get_error(&phase_detector, clk_rl);
    if (eval != 0) {
        filterval = recfilter_apply(loop_error, sigmoid(eval));
    } else {
        filterval = recfilter_getlast(loop_error);
    }
    add_delay_d = (freqoff_to_period(tfreq, 1.0, filterval) * 1000.0) + cum_error;
    add_delay = round(add_delay_d);
    cum_error = add_delay_d - (double)add_delay;
#if defined(DIGGER_DEBUG) 
    fprintf(digger_log, "clk_rl = %f, add_delay = %d, eval = %f, filterval = %f, cum_error = %f\n",
      clk_rl, add_delay, eval, filterval, cum_error);
#endif

    doscreenupdate();
    SDL_Delay(add_delay);
    return (0);
}

int32_t getkips(void)
{
	return(1);
}

void s0soundoff(void)
{
}

void s0setspkrt2(void)
{
}

void s0settimer0(uint16_t t0v)
{
}

void s0settimer2(uint16_t t0v, bool mode)
{
}

void s0timer0(uint16_t t0v)
{
}

void s0timer2(uint16_t t0v, bool mode)
{
}

void s0soundkillglob(void)
{
}

