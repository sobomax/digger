#ifndef EMS_VID_H
#define EMS_VID_H

#include <stdint.h>

#include <SDL.h>

void ems_vid_init(SDL_Window *window, uint32_t *addflag, int width, int height);
void ems_vid_switchmode(void);

#endif /* EMS_VID_H */
