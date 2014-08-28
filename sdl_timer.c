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
static struct recfilter loop_error;

extern uint32_t ftime;

void inittimer(void)
{

    recfilter_init(&loop_error, 0.96, 0.0, 0);
    PFD_init(&phase_detector, 0.0);
}

int32_t getlrt(void)
{
	return(0);
}

uint32_t gethrt(void)
{
    uint32_t add_delay;
    double eval, clk_rl, tfreq;

    tfreq = 1000000.0 / ftime;
    clk_rl = (double)SDL_GetTicks() * tfreq / 1000.0;
    eval = PFD_get_error(&phase_detector, clk_rl);
    recfilter_apply(&loop_error, sigmoid(eval));
    add_delay = freqoff_to_period(tfreq, 0.1, loop_error.lastval) * 1000;
#if defined(DIGGER_DEBUG) 
    fprintf(digger_log, "clk_rl = %f, add_delay = %d, eval = %f, loop_error.lastval = %f\n", clk_rl, add_delay, eval, loop_error.lastval);
#endif

    doscreenupdate();
    SDL_Delay(add_delay);
    return (0);
}

int32_t getkips(void)
{
	return(1);
}

void s0initint8(void)
{
}

void s0restoreint8(void)
{
}

void s1initint8(void)
{
}

void s1restoreint8(void)
{
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

void s0settimer2(uint16_t t0v)
{
}

void s0timer0(uint16_t t0v)
{
}

void s0timer2(uint16_t t0v)
{
}

void s0soundkillglob(void)
{
}

