/*
 * Copyright (c) 2002-2017 Maxim Sobolev <sobomax@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/consio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vgl.h>

#include "def.h"
#include "fbsd_kbd.h"
#include "hardware.h"
#include "input.h"
#include "main.h"

#define RIGHTKEY        (98+128)
#define UPKEY           (95+128)
#define LEFTKEY         (97+128)
#define DOWNKEY         (100+128)
#define F1KEY           (59+128)
#define TABKEY          (15+128)
#define ADDKEY          (78+128)
#define SUBKEY          (74+128)
#define F7KEY           (65+128)
#define F8KEY           (66+128)
#define F9KEY           (67+128)
#define F10KEY          (68+128)

int keycodes[NKEYS][5]={{RIGHTKEY,-2,-2,-2,-2},         /* 1 Right */
                     {UPKEY,-2,-2,-2,-2},               /* 1 Up */
                     {LEFTKEY,-2,-2,-2,-2},             /* 1 Left */
                     {DOWNKEY,-2,-2,-2,-2},             /* 1 Down */
                     {F1KEY,-2,-2,-2,-2},               /* 1 Fire */
                     {'s',-2,-2,-2,-2},                 /* 2 Right */
                     {'w',-2,-2,-2,-2},                 /* 2 Up */
                     {'a',-2,-2,-2,-2},                 /* 2 Left */
                     {'z',-2,-2,-2,-2},                 /* 2 Down */
                     {TABKEY,-2,-2,-2,-2},              /* 2 Fire */
                     {'t',-2,-2,-2,-2},                 /* Cheat */
                     {ADDKEY,-2,-2,-2,-2},              /* Accelerate */
                     {SUBKEY,-2,-2,-2,-2},              /* Brake */
                     {F7KEY,-2,-2,-2,-2},               /* Music */
                     {F9KEY,-2,-2,-2,-2},               /* Sound */
                     {F10KEY,-2,-2,-2,-2},              /* Exit */
                     {' ',-2,-2,-2,-2},                 /* Pause */
                     {'n',-2,-2,-2,-2},                 /* Change mode */
                     {F8KEY,-2,-2,-2,-2}};              /* Save DRF */

#define KBLEN		30
int16_t kbuffer[KBLEN];
int16_t klen=0;
bool states[256];

const int quertycodes[48+1]={41, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,\
			  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 43,\
			  30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 44, 45,\
			  46, 47, 48, 49, 50, 51, 52, 53, 57, 0};
const char chars[48] =    {'`','1','2','3','4','5','6','7','8','9','0','-','=',\
			 'q','w','e','r','t','y','u','i','o','p','[',']','\\',\
			 'a','s','d','f','g','h','j','k','l',';','\'','z','x',\
			 'c','v','b','n','m',',','.','/',' '};

#define	LALTKEY	(56+128)
#define	RALTKEY	(93+128)
#define	altpressed	(states[LALTKEY] || states[RALTKEY])

extern bool started, pausef;

void initkeyb(void)
{
	VGLKeyboardInit(VGL_CODEKEYS);
	memset(states, false, (sizeof states));
}

void restorekeyb(void)
{
	VGLKeyboardEnd();
}

static bool UpdateStates(int16_t *result)
{
	int16_t i;
	bool isasymbol, state;
	
	if(*result < 128)
		state = true;
	else {
		state = false;
		*result -= 128;
	}

	isasymbol = false;
	for(i=0;quertycodes[i]!=0;i++)
		if(*result == quertycodes[i]) {
			*result = chars[i];
			isasymbol = true;
			break;
		}

	if (isasymbol == false)
		*result+=128;

	states[*result] = state;
	return state;
}

void ProcessKbd(void)
{
	int16_t result;
	static bool newconsf=false;
	bool state;

	while((result = VGLKeyboardGetCh()) != 0) {

		state=UpdateStates(&result);
		if(newconsf==true && pausef==true) {
                  /* return to game ? */
		  if(state==false) 
		    continue;
		  else newconsf=false;			/* yes */
                }
		
		while(newconsf==false && state==true &&
		      result>=F1KEY && result<=F10KEY && altpressed) {
		    /* Alt-Fn pressed to switch consoles */
		    int activecons=0;
		    int newcons=result-F1KEY+1;
		    ioctl(0, VT_GETACTIVE, &activecons);
		    if(newcons==activecons) /* to another console ? */
			break;
		    
		    newconsf=true;
		    /* do switch */
		    ioctl(0,VT_ACTIVATE,(caddr_t)(long)newcons); 
		    if(started==true && pausef==false) {
			    pausef=true;
			    testpause(); /* force pause if game active */
		    }
		    else {
		        VGLCheckSwitch(); /* game not active - just switch */
		        /* now wait for another keyboard strike */
			result=VGLKeyboardGetCh();
			while(1) {
			    if(result!=0) {
				state=UpdateStates(&result);
				    if(state==true)     /* ignore releases */
					break;	
			        }
		    	    usleep(500);	/* don't waste CPU when idle */
			    result=VGLKeyboardGetCh();
			}
		    }
		    newconsf=false; /* switched back */
		}

		if(state == true)
			continue;

		if(klen == KBLEN) /* Buffer is full, drop some pieces */
			memcpy(kbuffer, kbuffer + 1, --klen);
		kbuffer[klen++] = result;
	}
}

bool GetAsyncKeyState(int key)
{
	ProcessKbd();
	return(states[key]);
}

int16_t getkey(bool scancode)
{
	int16_t result;
	
	while(kbhit() != true)
		gethrt(true);
	result = kbuffer[0];
	memcpy(kbuffer, kbuffer + 1, --klen);

	return(result);
}

bool kbhit(void)
{
	ProcessKbd();

	if (klen > 0)
		return(true);
	else
		return(false);
}

