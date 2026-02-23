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
#include "game.h"
#include "hardware.h"
#include "input.h"
#ifdef _SDL
#include "sdl_vid.h"
#endif
#if defined(DIGGER_DEBUG)
#include "digger_log.h"
#endif

static double next_tick_time_ms = 0.0;
static double next_render_time_ms = 0.0;
static double render_interval_ms = 0.0;
static double cached_tick_duration_ms = 0.0;
static double perf_counter_to_ms = 0.0;

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

/* Precision sleep: SDL_Delay for the bulk, then busy-wait the last 2 ms.
 * macOS timer granularity is 1-5 ms; sleeping right up to the deadline
 * causes frequent overshoots and visible stuttering. */
static void precision_wait_until(double target_ms) {
  double now_ms;
  double sleep_ms;

  for (;;) {
    now_ms = (double)SDL_GetPerformanceCounter() * perf_counter_to_ms;
    if (now_ms >= target_ms)
      return;

    sleep_ms = target_ms - now_ms;
    if (sleep_ms > 2.0) {
      /* Sleep conservatively, leaving 2 ms headroom for busy-wait */
      SDL_Delay((uint32_t)(sleep_ms - 2.0));
    }
    /* Last 2 ms: spin (yields via loop overhead, no SDL_Delay) */

    input_poll_async();
  }
}

void inittimer(void) {
  next_tick_time_ms = 0.0;
  next_render_time_ms = 0.0;
  cached_tick_duration_ms = 0.0;
  if (SDL_WasInit(SDL_INIT_TIMER) == 0) {
    SDL_InitSubSystem(SDL_INIT_TIMER);
  }
  uint64_t freq = SDL_GetPerformanceFrequency();
  if (freq == 0) {
    perf_counter_to_ms = 1.0;
  } else {
    perf_counter_to_ms = 1000.0 / (double)freq;
  }
  render_interval_ms = detect_refresh_interval();
#if defined(DIGGER_DEBUG)
  fprintf(digger_log, "inittimer: ftime = %u, refresh ≈ %.2fms\n",
          dgstate.ftime, render_interval_ms);
#endif
}

void gethrt(bool minsleep) {
  double tick_duration_ms;
  double now_ms;

  if (minsleep) {
    /* Non-frame callers (sound wait, keyboard poll, settings menu):
     * just refresh screen + yield CPU.  Do NOT advance the tick timer
     * or commit interpolation frames — those belong to the real
     * game-tick path only.
     * Light map must still be invalidated so dynamic lights (fireballs,
     * explosions) don't accumulate during death animation. */
    sdl_invalidate_light_map();
    input_poll_async();
    doscreenupdate();
    SDL_Delay(10);
    return;
  }

  /* Mark light map stale once per real game tick so it rebuilds exactly once */
  sdl_invalidate_light_map();

  if (dgstate.ftime <= 1) {
    input_poll_async();
    doscreenupdate();
    return;
  }

  tick_duration_ms = (double)dgstate.ftime / 1000.0;
  now_ms = (double)SDL_GetPerformanceCounter() * perf_counter_to_ms;
  input_poll_async();

  if (cached_tick_duration_ms != tick_duration_ms) {
    cached_tick_duration_ms = tick_duration_ms;
    next_tick_time_ms = now_ms + tick_duration_ms;
  } else if (next_tick_time_ms == 0.0) {
    next_tick_time_ms = now_ms + tick_duration_ms;
  }

  if (sdl_get_frame_interp()) {
    /* Interpolation ON: capture tick frame, blend between renders */
    sdl_frame_tick_commit();
    ensure_render_schedule(now_ms);

    while (1) {
      now_ms = (double)SDL_GetPerformanceCounter() * perf_counter_to_ms;

      if (now_ms >= next_tick_time_ms)
        break;

      if (now_ms >= next_render_time_ms) {
        double last_tick_start_ms = next_tick_time_ms - tick_duration_ms;
        float t = (float)((now_ms - last_tick_start_ms) / tick_duration_ms);
        doscreenupdate_interp(t);
        next_render_time_ms += render_interval_ms;
        input_poll_async();
        continue;
      }

      {
        double next_event_ms = next_render_time_ms;
        if (next_tick_time_ms < next_event_ms)
          next_event_ms = next_tick_time_ms;
        precision_wait_until(next_event_ms);
      }
    }
  } else {
    /* Interpolation OFF: render once, then precision-wait for next tick */
    doscreenupdate();
    precision_wait_until(next_tick_time_ms);
    now_ms = (double)SDL_GetPerformanceCounter() * perf_counter_to_ms;
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
