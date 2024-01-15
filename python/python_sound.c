#include <stdint.h>
#include <stdbool.h>

#include "newsnd.h"

bool wave_device_available = false;

void pausesounddevice(bool pause) {}

void soundinitglob(uint16_t bufsize, uint16_t samprate) {}

void s1setupsound(void) {}

void s1killsound(void) {}

void s1timer2(uint16_t t2, bool mode) {}

void s1soundoff(void) {}

void s1setspkrt2(void) {}

void s1timer0(uint16_t t0) {}
