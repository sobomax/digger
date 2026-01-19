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

/* Global variables */
extern bool soundflag;
extern int16_t volume;

/* Menu items */
enum {
  MENU_SPEED,
  MENU_SOUND_LEVEL,
  MENU_MUSIC,
  MENU_INTEGER_SCALE,
  MENU_LINEAR_FILTER,
  MENU_SCANLINES,
  MENU_SCANLINE_INTENSITY,
  MENU_START,
  MENU_EXIT,
  MENU_ITEM_COUNT
};

static const char *menu_labels[] = {
    "GAME SPEED", "SOUND LEVEL", "MUSIC", "INTEGER SCALING", "LINEAR FILTER",
    "SCANLINES",  "SCANLINE LEVEL", "START GAME",      "EXIT"};

static int current_item = 0;

/* Speed presets */
static const struct {
  const char *name;
  int value;
} speed_presets[] = {{"VERY FAST", 5},
                     {"FAST", 10},
                     {"NORMAL", 20},
                     {"SLOW", 40},
                     {"VERY SLOW", 80}};
#define NUM_SPEED_PRESETS 5

static int current_speed_preset = 2; /* Default: NORMAL */

static void draw_menu(struct digger_draw_api *ddap) {
  int i;
  int y_start = 10;
  int y_spacing = 12;
  char buf[32];

  /* Clear screen */
  ddap->gclear();

  /* Title */
  outtext(ddap, "SETTINGS", 120, 15, 3);

  /* Draw menu items */
  for (i = 0; i < MENU_ITEM_COUNT; i++) {
    int y = y_start + i * y_spacing;
    int color =
        (i == current_item) ? 3 : 1; /* Yellow if selected, blue otherwise */

    /* Draw selection indicator */
    if (i == current_item) {
      outtext(ddap, ">", 20, y, 3);
    } else {
      erasetext(ddap, 1, 20, y, 0);
    }

    /* Draw label */
    outtext(ddap, menu_labels[i], 36, y, color);

    /* Draw value */
    switch (i) {
     case MENU_SPEED:
       snprintf(buf, sizeof(buf), "< %s >",
                speed_presets[current_speed_preset].name);
       outtext(ddap, buf, 180, y, 2);
       break;

     case MENU_SOUND_LEVEL:
       snprintf(buf, sizeof(buf), "< %3d%% >", (volume * 100) / 255);
       outtext(ddap, buf, 180, y, 2);
       break;

    case MENU_MUSIC:
       if (musicflag)
         outtext(ddap, "ON ", 200, y, 2);
       else
         outtext(ddap, "OFF", 200, y, 2);
       outtext(ddap, ": ", 230, y, 2);
       break;

    case MENU_INTEGER_SCALE:
#ifdef _SDL
      if (sdl_get_integer_scaling())
        outtext(ddap, "ON ", 200, y, 2);
      else
        outtext(ddap, "OFF", 200, y, 2);
      outtext(ddap, ": ", 230, y, 2);
#else
      outtext(ddap, "N/A", 200, y, 1);
#endif
      break;

     case MENU_LINEAR_FILTER:
#ifdef _SDL
        if (sdl_get_linear_filter())
          outtext(ddap, "ON ", 200, y, 2);
        else
          outtext(ddap, "OFF", 200, y, 2);
        outtext(ddap, ": ", 230, y, 2);
#else
        outtext(ddap, "N/A", 200, y, 1);
#endif
        break;

    case MENU_SCANLINES:
#ifdef _SDL
      if (sdl_get_scanlines())
        outtext(ddap, "ON ", 200, y, 2);
      else
        outtext(ddap, "OFF", 200, y, 2);
      outtext(ddap, ": ", 230, y, 2);
#else
      outtext(ddap, "N/A", 200, y, 1);
#endif
      break;

    case MENU_SCANLINE_INTENSITY:
#ifdef _SDL
      snprintf(buf, sizeof(buf), "< %3d%% >", sdl_get_scanline_intensity());
      outtext(ddap, buf, 180, y, 2);
#else
      outtext(ddap, "N/A", 200, y, 1);
#endif
      break;

    default:
      break;
    }
  }

  /* Instructions */
  outtext(ddap, "ARROWS TO SELECT", 68, 170, 1);
  outtext(ddap, "ENTER TO CONFIRM", 68, 184, 1);
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
     if (volume > 15)
       volume -= 15;
     break;

   case MENU_MUSIC:
     musicflag = !musicflag;
     if (!musicflag)
       musicoff();
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
     if (volume < 255 - 15)
       volume += 15;
     break;

   case MENU_MUSIC:
     musicflag = !musicflag;
     if (!musicflag)
       musicoff();
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

  case MENU_MUSIC:
    musicflag = !musicflag;
    if (!musicflag)
      musicoff();
    break;

   case MENU_START:
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
  current_speed_preset = 2; /* Default to NORMAL */
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
    /* Pump SDL events ( Handler will fill key buffer for game, but we use direct state for menu ) */
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
         state[SDL_SCANCODE_SPACE]) && !key_enter) {
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
      if (key == 0x48) { /* Up */
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
