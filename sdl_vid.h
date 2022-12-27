#ifndef SDL_VID_H
#define SDL_VID_H

void doscreenupdate(void);
void switchmode(void);
void sdl_enable_fullscreen(void);

#if defined(UNIX) && !defined(__EMSCRIPTEN__)
void sdl_set_x11_parent(unsigned int);
#endif

#endif /* SDL_VID_H */
