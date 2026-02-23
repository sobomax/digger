# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Digger Remastered — a C11 remake of the 1983 Windmill Software arcade game, using SDL2 for cross-platform graphics/audio/input. Supports Linux, FreeBSD, Windows (cross-compile), macOS, and WebAssembly.

## Build Commands

```bash
# Debug build (macOS/Linux)
make ARCH=LINUX BUILD_TYPE=debug

# Production build (optimized, stripped)
make ARCH=LINUX BUILD_TYPE=production

# Clean
make clean

# Full test suite
make do-test

# Run a single integration test (uses .drf replay files)
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./digger /E:tests/data/2test01.drf

# Quick tests
TEST_TYPE=quick sh ./scripts/do-test-run.sh

# Coverage
CI_COVERAGE=1 make do-test-cmake
make coverage-report-html
```

Cross-compile targets: `ARCH=MINGW` (Win32), `ARCH=MINGW64` (Win64), `ARCH=WASM` (Emscripten).

```bash
# WASM build + local preview
make ARCH=WASM
python3 -m http.server 8080   # then open http://localhost:8080/digger.html
```

**WASM shell**: `shell.html` is the custom Emscripten shell template (retro-themed landing page). The build outputs `digger.html` + `digger.js` + `digger.wasm`.

## Architecture

**Game loop** in `main.c` drives everything: initialization, per-frame updates, level transitions.

**Object-oriented C pattern** — game entities use structs with function pointers and a `CALL_METHOD(obj, method, args...)` macro:
- `digger_obj.c` / `digger.c` — player digger logic, movement, firing
- `monster_obj.c` / `monster.c` — monster AI and behavior
- `bullet_obj.c` — fireball/bullet sprites

**Platform abstraction** via `draw_api.h` (graphics interface with function pointers) and SDL2 backends:
- `sdl_vid.c` — SDL2 video, fullscreen, visual effects (bloom, CRT, dynamic lighting)
- `sdl_snd.c` — SDL2 audio driver
- `sdl_kbd.c` — SDL2 keyboard input
- `sdl_timer.c` — SDL2 timing
- FreeBSD native alternatives: `fbsd_vid.c`, `fbsd_kbd.c`, `fbsd_timer.c`

**Game state** lives in `struct gamestate` (`game.h`) — global `dgstate` holds player count, level data (8 levels, 10x15 grid), timing, and mode flags.

**Key subsystems:**
- `drawing.c` / `sprite.c` — sprite rendering and field drawing
- `sound.c` / `soundgen.c` / `newsnd.c` — sound management and wave generation
- `bags.c` — treasure bag physics and collision
- `input.c` / `keyboard.c` — input polling and key configuration
- `record.c` — replay recording/playback (.drf format)
- `scores.c` — scoring and high scores
- `ini.c` — config file parsing (~/.digger.rc on Unix, DIGGER.INI on Windows)
- `settings_menu.c` — in-game settings UI

**Graphics data** is compiled-in: `vgagrafx.c` (VGA), `cgagrafx.c` (CGA), `alpha.c` (alpha blending), `title_gz.c` (title screen, zlib-compressed).

**Map constants** (`def.h`): 15 wide x 10 high grid, max 2 diggers, 6 monsters, 7 bags, directions encoded as 0/2/4/6 (right/up/left/down).

**Shared math utilities** (`digger_math.h`): `MIN`/`MAX`/`ABS` macros, `D_PI`, `lcg_next()` inline LCG helper (shared game and sound RNG), and DSP filter structs (`recfilter`, `PFD`, `bqd_filter`).

## Code Style

- C11 strict (`-std=c11 -Wall -Wextra -pedantic`)
- Block comments only (`/* */`), no `//`
- `snake_case` functions/variables, `UPPER_CASE` macros
- Fixed-width types (`int16_t`, `uint32_t`, etc.) and `bool` from `<stdbool.h>`
- Platform-specific code guarded with `#ifdef LINUX`, `#ifdef MINGW`, etc.
- Shared algorithm helpers placed in headers as `static inline` functions (e.g., `lcg_next()` in `digger_math.h`)
- Debug-only output guarded with `#ifdef DIGGER_DEBUG`; fatal error messages left unconditional

## Dependencies

SDL2 >= 2.0.9, zlib >= 1.3.1, libm, libatomic (fallback on some platforms).

## Security Hardening

The replay parser (`record.c`) is the primary untrusted-input boundary:
- `plp_end` tracks replay buffer bounds; all reader functions check before dereferencing
- Header fields validated: `diggers` (1..2), `startlev` (1..8), `gtime` (>0 in gauntlet)
- Error paths set `escape=true` for graceful termination
- All `sprintf` migrated to `snprintf`; `strcpy` replaced with `snprintf` where input length is unbounded

The `getfield()` function in `monster.c` bounds-checks coordinates and returns -1 (solid wall) for OOB. The `eatfield()` function in `drawing.c` bounds-checks all 4 direction cases.

## Pre-commit Checks

1. Builds succeed on LINUX (both debug and production)
2. Tests pass with `TEST_TYPE=quick`
3. No new compiler warnings with `-Wall -Wextra`
