#include <ctype.h>
#include "def.h"
#include "hardware.h"

void strupr(char *str)
{
	while(*str != 0) {
		*str = toupper(*str);
		str++;
	}
}

void catcher(int num) {
	fprintf(stderr, "Signal %d catched, exitting\n", num);
	graphicsoff();
	restorekeyb();
	exit(0);
}
	

