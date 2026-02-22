/*
 * Settings menu for Digger Remastered
 * Displayed before game starts
 */

#include <stdio.h>
#include <string.h>

#include "def.h"
#include "digger.h"
#include "draw_api.h"
#include "drawing.h"
#include "game.h"
#include "hardware.h"
#include "input.h"
#include "settings_menu.h"
#include "sound.h"

#ifdef _SDL
#include "sdl_vid.h"
#include <SDL.h>
#endif

/* Menu items */
enum {
  MENU_SPEED,
  MENU_SOUND_LEVEL,
  MENU_MUSIC,
  MENU_INTEGER_SCALE,
  MENU_LINEAR_FILTER,
  MENU_SCANLINES,
  MENU_SCANLINE_INTENSITY,
  MENU_BLOOM,
  MENU_CRT_MASK,
  MENU_LIGHTING,
  MENU_PALETTE_FADE,
  MENU_FRAME_INTERP,
  MENU_START,
  MENU_EXIT,
  MENU_ITEM_COUNT
};

static const char *menu_labels[] = {
    "SPEED",      "VOLUME",     "MUSIC",      "INT SCALE",
    "BILINEAR",   "SCANLINES",  "SCAN LVL",   "BLOOM",
    "CRT MASK",   "LIGHTING",   "PAL FADE",   "FRM INTERP",
    "START GAME", "EXIT"};

static int current_item = 0;

/* Speed presets - value * 2000 = microseconds per frame */
static const struct {
  const char *name;
  int value;
} speed_presets[] = {{"TURBO", 7},
                     {"1983", 8},
                     {"RELAXED", 14},
                     {"SLOW", 25},
                     {"V.SLOW", 50}};
#define NUM_SPEED_PRESETS 5

static int current_speed_preset = 2; /* Default: RELAXED */

static void draw_toggle(struct digger_draw_api *ddap, bool val, int16_t x,
                        int16_t y) {
  if (val)
    outtext(ddap, "ON ", x, y, 2);
  else
    outtext(ddap, "OFF", x, y, 2);
}

#define MENU_Y_START 2
#define MENU_Y_SPACING 13
#define MENU_LABEL_X 32
#define MENU_VAL_X 168

static void draw_menu(struct digger_draw_api *ddap) {
  int i;
  char buf[20];

  ddap->gclear();

  for (i = 0; i < MENU_ITEM_COUNT; i++) {
    int y = MENU_Y_START + i * MENU_Y_SPACING;
    int color = (i == current_item) ? 3 : 1;

    if (i == current_item)
      outtext(ddap, ">", 16, y, 3);
    else
      erasetext(ddap, 1, 16, y, 0);

    outtext(ddap, menu_labels[i], MENU_LABEL_X, y, color);

    switch (i) {
    case MENU_SPEED:
      snprintf(buf, sizeof(buf), "< %-7s >",
               speed_presets[current_speed_preset].name);
      outtext(ddap, buf, MENU_VAL_X, y, 2);
      break;

    case MENU_SOUND_LEVEL:
      snprintf(buf, sizeof(buf), "< %3d%%    >", (volume * 100) / 255);
      outtext(ddap, buf, MENU_VAL_X, y, 2);
      break;

    case MENU_MUSIC:
      draw_toggle(ddap, musicflag, MENU_VAL_X, y);
      break;

    case MENU_INTEGER_SCALE:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_integer_scaling(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_LINEAR_FILTER:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_linear_filter(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_SCANLINES:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_scanlines(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_SCANLINE_INTENSITY:
#ifdef _SDL
      snprintf(buf, sizeof(buf), "< %3d%%    >",
               sdl_get_scanline_intensity());
      outtext(ddap, buf, MENU_VAL_X, y, 2);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_BLOOM:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_bloom(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_CRT_MASK:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_crt_mask(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_LIGHTING:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_lighting(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_PALETTE_FADE:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_palette_fade(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    case MENU_FRAME_INTERP:
#ifdef _SDL
      draw_toggle(ddap, sdl_get_frame_interp(), MENU_VAL_X, y);
#else
      outtext(ddap, "N/A", MENU_VAL_X, y, 1);
#endif
      break;

    default:
      break;
    }
  }

  outtext(ddap, "ARROWS/ENTER/ESC", 68, 186, 1);
}

static void handle_left(void) {
  switch (current_item) {
  case MENU_SPEED:
    if (current_speed_preset > 0) {
      current_speed_preset--;
      dgstate.ftime = speed_presets[current_speed_preset].value * 2000l;
    }
    break;

  case MENU_SCANLINE_INTENSITY:
#ifdef _SDL
  {
    int intensity = sdl_get_scanline_intensity();
    sdl_set_scanline_intensity(intensity - 10);
  }
#endif
  break;

  case MENU_SOUND_LEVEL:
    if (volume >= 15)
      volume -= 15;
    else
      volume = 0;
    break;

  case MENU_MUSIC:
    if (musicflag) {
      musicflag = false;
      musicoff();
    }
    break;

  default:
    break;
  }
}

static void handle_right(void) {
  switch (current_item) {
  case MENU_SPEED:
    if (current_speed_preset < NUM_SPEED_PRESETS - 1) {
      current_speed_preset++;
      dgstate.ftime = speed_presets[current_speed_preset].value * 2000l;
    }
    break;

  case MENU_SCANLINE_INTENSITY:
#ifdef _SDL
  {
    int intensity = sdl_get_scanline_intensity();
    sdl_set_scanline_intensity(intensity + 10);
  }
#endif
  break;

  case MENU_SOUND_LEVEL:
    if (volume <= 240)
      volume += 15;
    else
      volume = 255;
    break;

  case MENU_MUSIC:
    if (!musicflag)
      musicflag = true;
    break;

  default:
    break;
  }
}

static int handle_enter(void) {
  switch (current_item) {
  case MENU_INTEGER_SCALE:
#ifdef _SDL
    sdl_toggle_integer_scaling();
#endif
    break;

  case MENU_LINEAR_FILTER:
#ifdef _SDL
    sdl_toggle_linear_filter();
#endif
    break;

  case MENU_SCANLINES:
#ifdef _SDL
    sdl_toggle_scanlines();
#endif
    break;

  case MENU_BLOOM:
#ifdef _SDL
    sdl_toggle_bloom();
#endif
    break;

  case MENU_CRT_MASK:
#ifdef _SDL
    sdl_toggle_crt_mask();
#endif
    break;

  case MENU_LIGHTING:
#ifdef _SDL
    sdl_toggle_lighting();
#endif
    break;

  case MENU_PALETTE_FADE:
#ifdef _SDL
    sdl_toggle_palette_fade();
#endif
    break;

  case MENU_FRAME_INTERP:
#ifdef _SDL
    sdl_toggle_frame_interp();
#endif
    break;

  case MENU_MUSIC:
    musicflag = !musicflag;
    if (!musicflag)
      musicoff();
    break;

  case MENU_START:
#ifdef _SDL
    sdl_save_settings();
#endif
    return 1; /* Start game */

  case MENU_EXIT:
    return -1; /* Exit */

  default:
    break;
  }
  return 0; /* Continue menu */
}

int show_settings_menu(struct digger_draw_api *ddap) {
  int result = 0;
#ifdef _SDL
  const uint8_t *state;
  bool key_up = false, key_down = false, key_left = false, key_right = false;
  bool key_enter = false, key_escape = false;
#else
  int16_t key;
#endif

  /* Initialize speed preset from current ftime */
  int i;
  int current_speed = (int)(dgstate.ftime / 2000l);
  current_speed_preset = 2; /* Default to RELAXED */
  for (i = 0; i < NUM_SPEED_PRESETS; i++) {
    if (speed_presets[i].value == current_speed) {
      current_speed_preset = i;
      break;
    }
  }

  current_item = MENU_START; /* Default to START GAME */

#ifdef _SDL
  state = SDL_GetKeyboardState(NULL);
#endif

  while (result == 0) {
    draw_menu(ddap);
    ddap->gflush();

    /* Process timing without consuming keyboard */
    gethrt(true);

#ifdef _SDL
    /* Pump SDL events ( Handler will fill key buffer for game, but we use
     * direct state for menu ) */
    SDL_PumpEvents();
    state = SDL_GetKeyboardState(NULL);

    /* Check key states with simple debouncing */
    if (state[SDL_SCANCODE_UP] && !key_up) {
      key_up = true;
      if (current_item > 0)
        current_item--;
    } else if (!state[SDL_SCANCODE_UP]) {
      key_up = false;
    }

    if (state[SDL_SCANCODE_DOWN] && !key_down) {
      key_down = true;
      if (current_item < MENU_ITEM_COUNT - 1)
        current_item++;
    } else if (!state[SDL_SCANCODE_DOWN]) {
      key_down = false;
    }

    if (state[SDL_SCANCODE_LEFT] && !key_left) {
      key_left = true;
      handle_left();
    } else if (!state[SDL_SCANCODE_LEFT]) {
      key_left = false;
    }

    if (state[SDL_SCANCODE_RIGHT] && !key_right) {
      key_right = true;
      handle_right();
    } else if (!state[SDL_SCANCODE_RIGHT]) {
      key_right = false;
    }

    if ((state[SDL_SCANCODE_RETURN] || state[SDL_SCANCODE_KP_ENTER] ||
         state[SDL_SCANCODE_SPACE]) &&
        !key_enter) {
      key_enter = true;
      result = handle_enter();
    } else if (!(state[SDL_SCANCODE_RETURN] || state[SDL_SCANCODE_KP_ENTER] ||
                 state[SDL_SCANCODE_SPACE])) {
      key_enter = false;
    }

    if (state[SDL_SCANCODE_ESCAPE] && !key_escape) {
      key_escape = true;
      result = -1; /* Exit */
    } else if (!state[SDL_SCANCODE_ESCAPE]) {
      key_escape = false;
    }
#else
    /* Non-SDL fallback using arrow key scancodes */
    if (kbhit()) {
      key = getkey(true); /* Get scancode */
      if (key == 0x48) {  /* Up */
        if (current_item > 0)
          current_item--;
      } else if (key == 0x50) { /* Down */
        if (current_item < MENU_ITEM_COUNT - 1)
          current_item++;
      } else if (key == 0x4B) { /* Left */
        handle_left();
      } else if (key == 0x4D) { /* Right */
        handle_right();
      } else if (key == 0x1C) { /* Enter */
        result = handle_enter();
      } else if (key == 0x01) { /* Escape */
        result = -1;
      }
    }
#endif
  }

  ddap->gclear();
  ddap->gflush();
  return (result == 1) ? 1 : 0;
}
