#include <math.h>
#if defined(DIGGER_DEBUG)
#include <stdio.h>
#endif

#if defined(_SDL)
#include <SDL.h>
#endif

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

#define SDL_Delay(x) emscripten_sleep(x)
#endif

#include "def.h"
#include "digger_math.h"
#include "game.h"
#include "hardware.h"
#include "input.h"
#ifdef _SDL
#include "sdl_vid.h"
#endif
#if defined(DIGGER_DEBUG)
#include "digger_log.h"
#endif

/* Ensure doscreenupdate is declared if not in headers */
void doscreenupdate(void);

static struct PFD phase_detector;
static struct recfilter *loop_error;

static double next_tick_time_ms = 0.0;
static double next_render_time_ms = 0.0;
static double render_interval_ms = 0.0;
static double cached_tick_duration_ms = 0.0;

static double detect_refresh_interval(void) {
  SDL_DisplayMode mode;

  if (SDL_WasInit(SDL_INIT_VIDEO) != 0 &&
      SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.refresh_rate > 0)
    return 1000.0 / (double)mode.refresh_rate;

  return 1000.0 / 60.0;
}

static void ensure_render_schedule(double now_ms) {
  if (render_interval_ms <= 0.0)
    render_interval_ms = detect_refresh_interval();

  if (next_render_time_ms == 0.0)
    next_render_time_ms = now_ms;

  /* If we fell far behind (window dragged, debugger, etc), jump to "now" */
  if (now_ms - next_render_time_ms > (render_interval_ms * 4.0))
    next_render_time_ms = now_ms;
}

void inittimer(void) {
  double tfreq;

  tfreq = 1000000.0 / dgstate.ftime;
  loop_error = recfilter_init(tfreq, 0.1);
  PFD_init(&phase_detector, 0.0);
  next_tick_time_ms = 0.0;
  next_render_time_ms = 0.0;
  cached_tick_duration_ms = 0.0;
  render_interval_ms = detect_refresh_interval();
#if defined(DIGGER_DEBUG)
  fprintf(digger_log, "inittimer: ftime = %u, refresh ≈ %.2fms\n",
          dgstate.ftime, render_interval_ms);
#endif
}

void gethrt(bool minsleep) {
  double tick_duration_ms;
  double now_ms;

  if (dgstate.ftime <= 1) {
    input_poll_async();
    doscreenupdate();
    if (minsleep)
      SDL_Delay(10);
    return;
  }

  tick_duration_ms = (double)dgstate.ftime / 1000.0;
  now_ms = (double)SDL_GetTicks();
  input_poll_async();

  if (cached_tick_duration_ms != tick_duration_ms) {
    cached_tick_duration_ms = tick_duration_ms;
    next_tick_time_ms = now_ms + tick_duration_ms;
  } else if (next_tick_time_ms == 0.0) {
    next_tick_time_ms = now_ms + tick_duration_ms;
  }

  ensure_render_schedule(now_ms);

  while (1) {
    input_poll_async();
    now_ms = (double)SDL_GetTicks();

    if (now_ms >= next_tick_time_ms)
      break;

    if (now_ms >= next_render_time_ms) {
      doscreenupdate();
      next_render_time_ms += render_interval_ms;
      continue;
    }

    double next_event_ms = next_render_time_ms;
    if (next_tick_time_ms < next_event_ms)
      next_event_ms = next_tick_time_ms;
    double sleep_ms = next_event_ms - now_ms;

    if (sleep_ms > 1.0) {
      SDL_Delay((uint32_t)sleep_ms);
    } else {
      SDL_Delay(minsleep ? 1u : 0u);
    }
  }

  if (now_ms - next_tick_time_ms > 500.0)
    next_tick_time_ms = now_ms;

  next_tick_time_ms += tick_duration_ms;
}

int32_t getkips(void) { return (1); }

void s0soundoff(void) {}

void s0setspkrt2(void) {}

void s0settimer0(uint16_t t0v) {}

void s0settimer2(uint16_t t0v, bool mode) {}

void s0timer0(uint16_t t0v) {}

void s0timer2(uint16_t t0v, bool mode) {}

void s0soundkillglob(void) {}
