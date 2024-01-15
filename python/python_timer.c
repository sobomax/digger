#include "def.h"
#include "digger_math.h"
#include "sprite.h"
#include "game.h"

struct timer_stats {
	uint64_t inittimer;
	uint64_t gethrt;
	uint64_t getkips;
	uint64_t s0soundoff;
	uint64_t s0setspkrt2;
	uint64_t s0settimer0;
	uint64_t s0settimer2;
	uint64_t s0timer0;
	uint64_t s0timer2;
	uint64_t s0soundkillglob;
} timer_stats = {0};

#define TIMERKNOB(n, r, ...) n(__VA_ARGS__) \
{ \
	timer_stats.n += 1; \
	r; \
}

void TIMERKNOB(inittimer, return, void)
int32_t TIMERKNOB(getkips, return 0, void)
void TIMERKNOB(s0soundoff, return, void)
void TIMERKNOB(s0setspkrt2, return, void)
void TIMERKNOB(s0settimer0, return, int16_t t0v)
void TIMERKNOB(s0settimer2, return, int16_t t0v)
void TIMERKNOB(s0timer0, return, int16_t t0v)
void TIMERKNOB(s0timer2, return, uint16_t t2v, bool mode)
void TIMERKNOB(s0soundkillglob, return, void)
//void inittimer(void) {}

void gethrt(bool)
{
	timer_stats.gethrt += 1;
    doscreenupdate();
}

#if 0
int32_t getkips(void)
{
	FIXME("getkips called");
	return(0);
}

void s0soundoff(void)
{
	FIXME("s0soundoff called");
}

void s0setspkrt2(void)
{
	FIXME("s0setspkrt2 called");
}

void s0settimer0(int16_t t0v)
{
	FIXME("s0settimer0 called");
}

void s0settimer2(int16_t t0v)
{
	FIXME("s0settimer2 called");
}

void s0timer0(int16_t t0v)
{
	FIXME("s0timer0 called");
}

void s0timer2(uint16_t t2v, bool mode)
{
	FIXME("s0timer2 called");
}

void s0soundkillglob(void)
{
/* No-op */
}
#endif

