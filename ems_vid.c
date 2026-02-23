/*
 * ---------------------------------------------------------------------------
 * "THE BEER-WARE LICENSE" (Revision 42, (c) Poul-Henning Kamp): Maxim
 * Sobolev <sobomax@altavista.net> wrote this file. As long as you retain
 * this  notice you can  do whatever you  want with this stuff. If we meet
 * some day, and you think this stuff is worth it, you can buy me a beer in
 * return.
 *
 * Maxim Sobolev
 * ---------------------------------------------------------------------------
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL.h>
#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include "ems_vid.h"

#define SDL_FULLSCREEN (0x1 << 0)

static SDL_Window *ems_window = NULL;
static uint32_t *ems_addflag = NULL;
static int ems_screen_width = 0;
static int ems_screen_height = 0;

static EM_BOOL
fullscreen_change_cb(int event_type, const EmscriptenFullscreenChangeEvent *event,
  void *user_data)
{
	(void)event_type;
	(void)user_data;

	if (ems_addflag == NULL) {
		return (EM_FALSE);
	}
	if (event->isFullscreen) {
		*ems_addflag |= SDL_FULLSCREEN;
	} else {
		*ems_addflag &= ~SDL_FULLSCREEN;
		emscripten_set_canvas_element_size("#canvas", ems_screen_width,
		  ems_screen_height);
		if (ems_window != NULL) {
			SDL_SetWindowSize(ems_window, ems_screen_width, ems_screen_height);
		}
	}
	return (EM_FALSE);
}

static bool
request_browser_fullscreen(bool fullscreen)
{
	EMSCRIPTEN_RESULT result;

	if (fullscreen) {
		EmscriptenFullscreenStrategy strategy = {0};

		strategy.scaleMode = EMSCRIPTEN_FULLSCREEN_SCALE_STRETCH;
		strategy.canvasResolutionScaleMode =
		  EMSCRIPTEN_FULLSCREEN_CANVAS_SCALE_STDDEF;
		strategy.filteringMode = EMSCRIPTEN_FULLSCREEN_FILTERING_DEFAULT;
		result = emscripten_request_fullscreen_strategy("#canvas", 1,
		  &strategy);
	} else {
		result = emscripten_exit_fullscreen();
	}
	if (result != EMSCRIPTEN_RESULT_SUCCESS) {
		fprintf(stderr, "fullscreen request failed: %d\n", result);
		return (false);
	}
	return (true);
}

static bool
browser_is_fullscreen(void)
{
	EmscriptenFullscreenChangeEvent status;

	if (emscripten_get_fullscreen_status(&status) == EMSCRIPTEN_RESULT_SUCCESS) {
		return (status.isFullscreen != 0);
	}
	if (ems_addflag != NULL) {
		return ((*ems_addflag & SDL_FULLSCREEN) != 0);
	}
	return (false);
}

void
ems_vid_init(SDL_Window *window, uint32_t *addflag, int width, int height)
{
	ems_window = window;
	ems_addflag = addflag;
	ems_screen_width = width;
	ems_screen_height = height;
	if (emscripten_set_fullscreenchange_callback(
	    EMSCRIPTEN_EVENT_TARGET_DOCUMENT, NULL, false,
	    fullscreen_change_cb) != EMSCRIPTEN_RESULT_SUCCESS) {
		fprintf(stderr, "failed to install fullscreen callback\n");
	}
}

void
ems_vid_switchmode(void)
{
	request_browser_fullscreen(!browser_is_fullscreen());
}

EMSCRIPTEN_KEEPALIVE void
digger_request_fullscreen(void)
{
	if (!browser_is_fullscreen()) {
		request_browser_fullscreen(true);
	}
}
