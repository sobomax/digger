#ifndef SDL_VID_H
#define SDL_VID_H

void doscreenupdate(void);
void switchmode(void);
void sdl_enable_fullscreen(void);

/* Scaling and filter controls */
void sdl_toggle_integer_scaling(void);
void sdl_toggle_linear_filter(void);
int sdl_get_integer_scaling(void);
int sdl_get_linear_filter(void);

/* Scanline/CRT effect controls */
void sdl_toggle_scanlines(void);
void sdl_set_scanline_intensity(int intensity); /* 0-100 */
int sdl_get_scanlines(void);
int sdl_get_scanline_intensity(void);

#if defined(UNIX) && !defined(__EMSCRIPTEN__)
void sdl_set_x11_parent(unsigned int);
#endif

#endif /* SDL_VID_H */
