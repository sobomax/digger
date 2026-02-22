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

/* Bloom and CRT Mask controls */
void sdl_toggle_bloom(void);
int sdl_get_bloom(void);
void sdl_toggle_crt_mask(void);
int sdl_get_crt_mask(void);
void sdl_toggle_lighting(void);
int sdl_get_lighting(void);
void sdl_add_light(int x, int y, int r, int g, int b, int radius);
void sdl_invalidate_light_map(void);

/* Palette fade controls */
void sdl_fade_to_intensity(int inten, int duration_ms);
void sdl_toggle_palette_fade(void);
int sdl_get_palette_fade(void);

/* Frame interpolation controls */
void sdl_toggle_frame_interp(void);
int sdl_get_frame_interp(void);
void sdl_frame_tick_commit(void);
void doscreenupdate_interp(float t);

void sdl_save_settings(void);

#if defined(UNIX) && !defined(__EMSCRIPTEN__) && !defined(__APPLE__)
void sdl_set_x11_parent(unsigned int);
#endif

#endif /* SDL_VID_H */
