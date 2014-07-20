#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/consio.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vgl.h>

#include "def.h"
#include "hardware.h"

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

#define	F1KEY	(59+128)
#define	F10KEY	(68+128)
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
	ProcessKbd();

	if (klen > 0)
		return(true);
	else
		return(false);
}

