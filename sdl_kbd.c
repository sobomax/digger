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
#include "input.h"
#include "sdl_kbd.h"
#include "sdl_vid.h"

#define KBLEN		30

int keycodes[NKEYS][5]={{SDL_SCANCODE_RIGHT,-2,-2,-2,-2}, /* 1 Right */
                     {SDL_SCANCODE_UP,-2,-2,-2,-2},       /* 1 Up */
                     {SDL_SCANCODE_LEFT,-2,-2,-2,-2},     /* 1 Left */
                     {SDL_SCANCODE_DOWN,-2,-2,-2,-2},     /* 1 Down */
                     {SDL_SCANCODE_F1,-2,-2,-2,-2},       /* 1 Fire */
                     {SDL_SCANCODE_S,-2,-2,-2,-2},        /* 2 Right */
                     {SDL_SCANCODE_W,-2,-2,-2,-2},        /* 2 Up */
                     {SDL_SCANCODE_A,-2,-2,-2,-2},        /* 2 Left */
                     {SDL_SCANCODE_Z,-2,-2,-2,-2},        /* 2 Down */
                     {SDL_SCANCODE_TAB,-2,-2,-2,-2},      /* 2 Fire */
                     {SDL_SCANCODE_T,-2,-2,-2,-2},        /* Cheat */
                     {SDL_SCANCODE_KP_PLUS,-2,-2,-2,-2},  /* Accelerate */
                     {SDL_SCANCODE_KP_MINUS,-2,-2,-2,-2}, /* Brake */
                     {SDL_SCANCODE_F7,-2,-2,-2,-2},       /* Music */
                     {SDL_SCANCODE_F9,-2,-2,-2,-2},       /* Sound */
                     {SDL_SCANCODE_F10,-2,-2,-2,-2},      /* Exit */
                     {SDL_SCANCODE_SPACE,-2,-2,-2,-2},    /* Pause */
                     {SDL_SCANCODE_N,-2,-2,-2,-2},        /* Change mode */
                     {SDL_SCANCODE_F8,-2,-2,-2,-2}};      /* Save DRF */

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
			memmove(kbuffer, kbuffer + 1, klen * sizeof(struct kbent));
                }
		/*
		 * Ignore Alt, so that Alt-Enter does not start the game or
		 * unpause it.
		 */
		if (event->key.keysym.scancode == SDL_SCANCODE_RALT ||
		    event->key.keysym.scancode == SDL_SCANCODE_LALT)
			goto out;
		/* ALT + Enter handling (fullscreen/windowed operation) */
		if ((event->key.keysym.scancode == SDL_SCANCODE_RETURN ||
                    event->key.keysym.scancode == SDL_SCANCODE_KP_ENTER) &&
		    ((event->key.keysym.mod & KMOD_ALT) != 0)) {
			switchmode();
			goto out;
		}
                kbuffer[klen].scancode = event->key.keysym.scancode;
                kbuffer[klen].sym = event->key.keysym.sym;
                klen++;

	}
out:
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
		gethrt(true);
        if (scancode) {
	        result = kbuffer[0].scancode;
        } else {
                result = kbuffer[0].sym;
        }
        klen--;
	memmove(kbuffer, kbuffer + 1, klen * sizeof(struct kbent));

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
