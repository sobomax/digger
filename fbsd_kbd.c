#include <sys/fbio.h>
#include <sys/kbio.h>
#include <sys/consio.h>
#include <vgl.h>

#include "def.h"
#include "hardware.h"

#define KBLEN		30
Sint4 kbuffer[KBLEN];
Sint4 klen=0;
bool states[256];

const int quertycodes[48+1]={41, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,\
			  16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 43,\
			  30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 44, 45,\
			  46, 47, 48, 49, 50, 51, 52, 53, 57, 0};
const char chars[48] =    {'`','1','2','3','4','5','6','7','8','9','0','-','=',\
			 'q','w','e','r','t','y','u','i','o','p','[',']','\\',\
			 'a','s','d','f','g','h','j','k','l',';','\'','z','x',\
			 'c','v','b','n','m',',','.','/',' '};

void initkeyb(void)
{
	VGLKeyboardInit(VGL_CODEKEYS);
	memset(states, FALSE, (sizeof states));
}

void restorekeyb(void)
{
	VGLKeyboardEnd();
}

void ProcessKbd(void)
{
	Sint4 result, i;
	bool isasymbol;
	bool state;

	while((result = VGLKeyboardGetCh()) != 0) {

		if(result < 128)
			state = TRUE;
		else {
			state = FALSE;
			result -= 128;
		}

		isasymbol = FALSE;
		for(i=0;quertycodes[i]!=0;i++)
			if(result == quertycodes[i]) {
				result = chars[i];
				isasymbol = TRUE;
				break;
			}

		if (isasymbol == FALSE)
			result+=128;

		states[result] = state;

		if(state == TRUE)
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

Sint4 getkey(void)
{
	Sint4 result;
	
	while(kbhit() != TRUE)
		gethrt();
	result = kbuffer[0];
	memcpy(kbuffer, kbuffer + 1, --klen);

	return(result);
}

bool kbhit(void)
{
	ProcessKbd();

	if (klen > 0)
		return(TRUE);
	else
		return(FALSE);
}

