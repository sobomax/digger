# Digger Remastered TODO

Generated from brainstorm roundtable review (2026-02-23).
Verified against current codebase state (2026-02-23). Last reviewed: 2026-02-23. Code review: 2026-02-23 (3-agent team review of last 5 commits).

---

## P0 — Trivial Fixes (do first)

- [x] **Bump ZLIB_VER default to 1.3.1** in `GNUmakefile:21`
- [x] **Delete dead CI configs** (`.travis.yml`, `appveyor.yml`, README badges)
- [x] **Standardize C standard to C11** (`CMakeLists.txt`, `CLAUDE.md`)
- [x] **Fix macOS strip command** in `GNUmakefile` (conditional on `uname -s`)
- [x] **Give Digger a real version number** — `def.h` + `CMakeLists.txt` now `2.0.0-dev`

## P1 — Performance (small effort, real impact)

- [x] **`vgawrite()` — eliminate malloc per character** in `sdl_vid.c`
  - Replaced `malloc`/`free` with `static uint8_t copy[576]`
- [x] **`vgagetpix()` — eliminate surface alloc per pixel** in `sdl_vid.c`
  - Read `screen16->pixels` directly instead of creating/freeing SDL_Surface
- [x] **Cap `flashywait()` visual iterations** in `scores.c`
  - Removed inner `volume` loop — one `gpal`+`gflush` per outer iteration

## P1.5 — Rendering Bug Fixes

- [x] **Fix light map accumulation during death animation** in `sdl_timer.c`
  - `gethrt(minsleep=true)` didn't invalidate light map
  - During death (`sounddiedone=false`), fireball lights accumulated endlessly in `light_map`
  - Emerald glow froze, bloom didn't dissipate
  - Fix: call `sdl_invalidate_light_map()` in minsleep path too

## P2 — Build System Hygiene (small effort)

- [x] **Fix NO_SND_FILTER / NO_SND_EFFECTS inconsistency**
  - Removed commented-out flags from `GNUmakefile:8`, added documenting comment
  - Added `OPTION(NO_SND_FILTER)` and `OPTION(NO_SND_EFFECTS)` to `CMakeLists.txt`
  - Both build systems now default to filters/effects ON, with documented opt-out

- [x] **Fix `do-test-run.sh` for local use**
  - Guarded `mv ./production/* ./` with `if [ -d ./production ]`
  - Script now works both in CI (with production dir) and locally (without)

- [x] **Remove `-Wextra -pedantic` gap** in GNUmakefile
  - Added `-Wextra` to non-production (debug) CFLAGS in GNUmakefile
  - Also fixed `CMakeLists.txt` C standard from `-std=c99` to `-std=c11`

## P3 — Security Hardening (medium effort)

- [x] **Add bounds checking to field array access**
  - `getfield()` in `monster.c` now bounds-checks and returns -1 (solid) for OOB
  - `eatfield()` in `drawing.c` now bounds-checks all 4 direction cases
  - `drawfield()` look-aheads at `drawing.c:133-137` already guarded (`x < 14`, `y < 9`)

- [x] **Harden replay file parser** in `record.c`
  - Added `plp_end` buffer boundary tracking (set after load, cleared after free/error)
  - Header validation: `diggers` (1..2), `startlev` (>=1), `gtime` (>0 in gauntlet)
  - Bounds-checked `playgetdir()`, `playgetrand()`, `playskipeol()`
  - All error paths use `escape=true` for graceful termination
  - Remaining: level map data still copied raw without character validation
  - Remaining: `rlleft` digit-parsing in `playgetdir()` has no overflow cap (int)
  - Remaining: `bonusscore` parsed via `atoi()` without range validation

- [x] **Fix undefined behavior in `ini.c`**
  - `GetINIString()` now uses `memmove()` instead of `strncpy()` — safe for overlapping buffers

- [x] **Migrate `sprintf` to `snprintf`** — all 6 calls migrated
  - `main.c:887-888`: `snprintf(kbuf/vbuf, sizeof(...), ...)`
  - `record.c:391-400`: `snprintf(nambuf, sizeof(nambuf), ...)` + `strncat` for ".drf"
  - No `sprintf` calls remain in the codebase

- [x] **Fix assert-only `strcpy` guard** in `record.c`
  - Replaced `assert` + `strcpy` with `snprintf(rname, sizeof(rname), "%s", name)`

## P4 — Code Cleanup (small effort, nice to have)

- [x] **Extract duplicated CGA init code**
  - Extracted `init_cga_mode()` helper in `main.c`, called from both `parsecmd()` and `inir()`

- [x] **Unify duplicate RNG implementations**
  - `randno()` in `main.c` and `randnos()` in `sound.c` — identical LCG
  - Extracted `lcg_next(int32_t *state)` inline in `digger_math.h`
  - Both callers now use `lcg_next()`, separate state preserved

- [x] **Clean up `#if defined(INTDRF) || 1` in `digger.c`**
  - Removed the dead `#if` guards — code is now unconditional

- [x] **Rename `kludge` variable**
  - Renamed to `replay_compat_mode` in `record.c`, `record.h`, `monster.c`

- [x] **Guard FreeBSD debug prints**
  - `FIXME()` macro in `fbsd_sup.h` now conditional on `DIGGER_DEBUG`
  - Debug `fprintf` calls in `fbsd_vid.c` (doscreenupdate, vgaputi) guarded with `#ifdef DIGGER_DEBUG`
  - Fatal error messages (exit paths) left unconditional — those are user-facing

## P5 — Product / Features (medium-large effort)

- [ ] **Polish the WASM web version**
  - Currently deploys to S3 — this is the primary distribution channel
  - Ensure CRT effects / bloom are the default experience
  - Touch screen support for mobile browsers
  - Proper landing page with instructions

- [x] **Settings menu** (`settings_menu.c`)
  - 448-line functional implementation with 14 menu items
  - Sound, speed, graphics toggles (bloom, CRT, lighting, scanlines, etc.)

- [ ] **Remove obsolete DOS functions** — 11 FIXME stubs remain
  - `fbsd_snd.c`: 3 stubs (`initsounddevice`, `setsounddevice`, `killsounddevice`)
  - `fbsd_timer.c`: 8 stubs (`inittimer`, `getkips`, `s0soundoff`, `s0setspkrt2`, etc.)
  - `sound.c:794`: `s0setupsound()` wrapper calls `inittimer()`
  - All are FreeBSD VGL-only no-ops, dead code in SDL2 builds

- [x] **Independent framerate from game speed** (README roadmap v2.0)
  - Frame interpolation fully implemented (`sdl_frame_tick_commit`, `doscreenupdate_interp`)
  - Togglable via settings menu

---

## Not Planned (cut from roadmap)

These were discussed and deemed premature or unnecessary:

- **Network play** — requires decoupling input from game ticks; save for after single-player is polished
- **Headless/VNC mode** — no clear user demand
- **MMOG / deathmatch / v3.0 features** — aspirational, not actionable now

---

## Architecture Notes (from review)

**What's solid:**
- OO-in-C pattern (CALL_METHOD macro) is appropriate for this scale
- Global `dgstate` is pragmatic for a single-screen arcade game
- `draw_api.h` at 19 lines is the right abstraction thickness
- PFD+IIR timer in `sdl_timer.c` is well-designed
- Replay-based test suite (20+ `.drf` files with expected output diffing) is excellent
- Sprite cache in `ch2bmap()` (256 entries per plane) is correct

**What needs attention:**
- Build system inconsistencies (P2 items above)
- Input validation at system boundaries (P3 items above)

---

## Code Review Findings (2026-02-23)

3-agent team review of commits `5e3efb4..1f2dd1d` (5 commits, 18 files, +502/-115 lines).

### Verified Correct

- **`plp_end` bounds tracking** — correctly initialized after load, checked in all 3 reader functions, cleared on all exit paths (normal, error, out_0)
- **`getfield()` OOB return** — returning -1 (0xFFFF all-bits-set) is safe; callers check specific bit patterns and all-bits-set acts as impassable wall
- **`eatfield()` bounds checks** — all 4 direction cases guarded after coordinate adjustment, before `field[]` write
- **`lcg_next()` behavioral equivalence** — uint32_t arithmetic matches original signed multiplication; `& 0x7FFFFFFF` mask preserves behavior
- **`init_cga_mode()` extraction** — all 10 function pointers + `ginit()` + `gpal(0)` calls present; `ddap` parameter shadow is benign (only `-Wshadow`)
- **`memmove()` in `ini.c`** — correctly handles overlap case; length calculation and null-termination are correct
- **`snprintf` migrations** — all 6 former `sprintf` calls use `sizeof()` for buffer size; `strncat` for ".drf" is correct
- **`recname()` fix** — `snprintf(rname, sizeof(rname), "%s", name)` replaces `assert` + `strcpy` safely
- **INTDRF cleanup** — no remaining references; removal is clean
- **DIGGER_DEBUG guards** — all FreeBSD debug `fprintf` and `FIXME()` properly conditional; fatal messages remain unconditional (correct)

### Remaining Items (low priority)

- **`rlleft` overflow** (`record.c:265`): digit-parsing loop `rlleft=rlleft*10+(digit)` has no overflow cap; crafted replay with long digit string could overflow `int`. Low risk (replay files are not a high-value attack surface).
- **`bonusscore` via `atoi()`** (`record.c:139`): parsed without range validation. Impact is cosmetic only (score display).
- **`drawfield()` magic numbers** (`drawing.c:133-137`): look-ahead guards use hardcoded `14` and `9` instead of `MWIDTH-1` and `MHEIGHT-1`. Safe (values are correct for 15x10 grid) but inconsistent with `MWIDTH`/`MHEIGHT` usage elsewhere.
- **Level map data** (`record.c`): replay level data is still copied raw without character validation (pre-existing).
