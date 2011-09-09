#include <SDL.h>

#include "def.h"
#ifdef _SDL
#include "sdl_vid.h"
#endif

uint32_t prev = 0;
extern uint32_t ftime;

void inittimer(void)
{
}

int32_t getlrt(void)
{
	return(0);
}

uint32_t gethrt(void)
{
	int32_t diff;

	doscreenupdate();

	/* Speed controlling stuff */
	if (prev == 0) {
		prev = SDL_GetTicks();
	} else {
		diff = (ftime/1000 - (SDL_GetTicks() - prev));
		if (diff > 0) {
			SDL_Delay(diff);
		}
		prev = SDL_GetTicks();
	}
	return(0);
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

void s0settimer0(int16_t t0v)
{
}

void s0settimer2(int16_t t0v)
{
}

void s0timer0(int16_t t0v)
{
}

void s0timer2(int16_t t0v)
{
}

void s0soundkillglob(void)
{
}

