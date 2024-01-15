#include <stdbool.h>
#include <stdint.h>

#include "input.h"
#include "python_kbd.h"

struct digger_controls digger_controls = {0};
int keycodes[NKEYS][5] = {0};

void initkeyb(void)
{
    digger_controls = (struct digger_controls){0};
}

int16_t getkey(bool scancode)
{
    return -1;
}

void restorekeyb(void) {}
bool kbhit(void) 
{
    static bool state;
    state = !state;
    return state;
}