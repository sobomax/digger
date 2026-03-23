#ifndef SDL_VID_H
#define SDL_VID_H

#if !defined(DIGGER_DISABLE_SDL_X11_WINDOW)
#include <SDL_syswm.h>
#if defined(UNIX) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__) && defined(SDL_VIDEO_DRIVER_X11)
#ifndef HAVE_SDL_X11_WINDOW
#define HAVE_SDL_X11_WINDOW 1
#endif
#endif
#if defined(HAVE_SDL_X11_WINDOW)
#include <X11/Xlib.h>
#endif
#endif

void doscreenupdate(void);
void switchmode(void);
void sdl_enable_fullscreen(void);

#if defined(HAVE_SDL_X11_WINDOW)
void sdl_set_x11_parent(unsigned int);
#endif

#endif /* SDL_VID_H */
