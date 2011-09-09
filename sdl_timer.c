#include <SDL.h>

#include "def.h"
#ifdef _SDL
#include "sdl_vid.h"
#endif

Uint32 prev = 0;
extern Uint5 ftime;

void inittimer(void)
{
}

Sint5 getlrt(void)
{
	return(0);
}

Uint5 gethrt(void)
{
	Sint32 diff;

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

Sint5 getkips(void)
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

void s0settimer0(Sint4 t0v)
{
}

void s0settimer2(Sint4 t0v)
{
}

void s0timer0(Sint4 t0v)
{
}

void s0timer2(Sint4 t0v)
{
}

void s0soundkillglob(void)
{
}

