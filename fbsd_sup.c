#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "def.h"
#include "hardware.h"

#if !defined(__EMSCRIPTEN__)
void strupr(char *str)
{
	while(*str != 0) {
		*str = toupper(*str);
		str++;
	}
}
#endif

void catcher(int num) {
	fprintf(stderr, "Signal %d catched, exitting\n", num);
	graphicsoff();
	restorekeyb();
	exit(0);
}
	

