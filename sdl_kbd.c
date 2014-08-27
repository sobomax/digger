/*
 * ---------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42, (c) Poul-Henning Kamp): Maxim
 * Sobolev <sobomax@altavista.net> wrote this file. As long as you retain
 * this  notice you can  do whatever you  want with this stuff. If we meet
 * some day, and you think this stuff is worth it, you can buy me a beer in
 * return.
 * 
 * Maxim Sobolev
 * --------------------------------------------------------------------------- 
 */

#include <SDL.h>

#include "def.h"
#include "hardware.h"
#include "sdl_vid.h"

#define KBLEN		30
int16_t kbuffer[KBLEN];
int16_t klen=0;

static int Handler(void *uptr, SDL_Event *event)
{
	if(event->type == SDL_KEYDOWN) {
		if(klen == KBLEN) /* Buffer is full, drop some pieces */
			memcpy(kbuffer, kbuffer + 1, --klen);
		kbuffer[klen++] = event->key.keysym.sym;

		/* ALT + Enter handling (fullscreen/windowed operation) */
		if((event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER) &&
		    ((event->key.keysym.mod & KMOD_ALT) != 0))
			switchmode();
	}
	if(event->type == SDL_QUIT)
		exit(0);

	return(1);
}

bool GetAsyncKeyState(int key)
{
	const uint8_t *keys;
	
	SDL_PumpEvents();
	keys = SDL_GetKeyboardState(NULL);
	if (keys[key] == SDL_PRESSED )
		return(true);
	else
		return(false);
}

void initkeyb(void)
{
	SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE);
	SDL_EventState(SDL_MOUSEBUTTONDOWN, SDL_IGNORE);
	SDL_EventState(SDL_MOUSEBUTTONUP, SDL_IGNORE);
	
	SDL_SetEventFilter(Handler, NULL);
}

void restorekeyb(void)
{
}

int16_t getkey(void)
{
	int16_t result;
	
	while(kbhit() != true)
		gethrt();
	result = kbuffer[0];
	memcpy(kbuffer, kbuffer + 1, --klen);

	return(result);
}

bool kbhit(void)
{
	SDL_PumpEvents();
	doscreenupdate();

	if (klen > 0)
		return(true);
	else
		return(false);

}
