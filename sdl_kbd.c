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

struct kbent {
    int16_t sym;
    int16_t scancode;
};

static struct kbent kbuffer[KBLEN];
static int16_t klen=0;

static int Handler(void *uptr, SDL_Event *event)
{
	if(event->type == SDL_KEYDOWN) {
		if (klen == KBLEN) {
                        /* Buffer is full, drop some pieces */
                        klen--;
			memcpy(kbuffer, kbuffer + 1, klen * sizeof(struct kbent));
                }
		kbuffer[klen].scancode = event->key.keysym.scancode;
                kbuffer[klen].sym = event->key.keysym.sym;
                klen++;

		/* ALT + Enter handling (fullscreen/windowed operation) */
		if ((event->key.keysym.scancode == SDL_SCANCODE_RETURN ||
                    event->key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
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

int16_t getkey(bool scancode)
{
	int16_t result;
	
	while(kbhit() != true)
		gethrt();
        if (scancode) {
	        result = kbuffer[0].scancode;
        } else {
                result = kbuffer[0].sym;
        }
        klen--;
	memcpy(kbuffer, kbuffer + 1, klen * sizeof(struct kbent));

	return(result);
}

bool kbhit(void)
{
	SDL_PumpEvents();

	if (klen > 0)
		return(true);
	else
		return(false);

}
