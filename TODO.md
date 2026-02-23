# Digger Remastered TODO

Generated from brainstorm roundtable review (2026-02-23).
Verified against current codebase state (2026-02-23).

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

- [ ] **Fix NO_SND_FILTER inconsistency**
  - `GNUmakefile:8` has `-DNO_SND_FILTER` commented out
  - `CMakeLists.txt` doesn't mention it at all
  - Document the flag or remove from GNUmakefile

- [ ] **Fix `do-test-run.sh` for local use**
  - Script does `mv ./production/* ./` assuming CI directory layout
  - Fails when running `make do-test` locally
  - Add check: if binary already exists in `.`, skip the move

- [ ] **Remove `-Wextra -pedantic` gap** in GNUmakefile
  - `CMakeLists.txt` debug mode has `-Wextra`
  - `GNUmakefile` only has `-Wall`
  - Add `-Wextra` to GNUmakefile for consistency

## P3 — Security Hardening (medium effort)

- [ ] **Add bounds checking to field array access**
  - `field[y*MWIDTH+x]` used in `drawing.c`, `bags.c`, `monster.c` without bounds checks
  - Grid is 15x10 — out-of-range x/y could corrupt memory
  - Especially relevant for replay parser: crafted `.drf` could feed invalid positions

- [ ] **Harden replay file parser** in `record.c`
  - Validate input records before feeding to game logic
  - Important for WASM build where users may load untrusted `.drf` files

- [ ] **Fix undefined behavior in `ini.c:123`**
  - `strcpy(dest, def)` where `dest == def` is possible (see FIXME comment)
  - `strcpy` with overlapping buffers is UB per C standard
  - Use `memmove()` or restructure

- [ ] **Migrate `sprintf` to `snprintf`** — 6 remaining calls
  - `main.c`: 2 calls (lines 887-888)
  - `record.c`: 4 calls (line 379, 382, 385, 387)
  - Currently safe (controlled inputs) but fragile against future changes

## P4 — Code Cleanup (small effort, nice to have)

- [ ] **Extract duplicated CGA init code**
  - Identical ~10-line blocks in `parsecmd()` (main.c) and `inir()` (main.c)
  - Extract to `init_cga_mode()` function

- [ ] **Unify duplicate RNG implementations**
  - `randno()` in `main.c` and `randnos()` in `sound.c` — identical LCG algorithm
  - Separate state is correct (thread safety), but share the algorithm
  - Extract `lcg_next(uint32_t *state)` helper

- [ ] **Clean up `#if defined(INTDRF) || 1` in `digger.c:83,94`**
  - The `|| 1` makes it unconditionally compiled — remove the conditional or the `|| 1`

- [ ] **Rename `kludge` variable** in `record.c:22`, `record.h:18`
  - Used for replay format backward compatibility (`monster.c:357-366`)
  - Rename to `replay_compat_mode` or similar descriptive name

- [ ] **Guard FreeBSD debug prints**
  - `fbsd_vid.c:434` and many `fbsd_timer.c` stubs use `FIXME()` macro
  - These are unconditional `fprintf(stderr, ...)` in all build types
  - Guard with `#ifdef DIGGER_DEBUG` or remove

## P5 — Product / Features (medium-large effort)

- [ ] **Polish the WASM web version**
  - Currently deploys to S3 — this is the primary distribution channel
  - Ensure CRT effects / bloom are the default experience
  - Touch screen support for mobile browsers
  - Proper landing page with instructions

- [x] **Settings menu** (`settings_menu.c`)
  - 448-line functional implementation with 14 menu items
  - Sound, speed, graphics toggles (bloom, CRT, lighting, scanlines, etc.)

- [ ] **Remove obsolete DOS functions**
  - README roadmap item: `s0setupsound()` and similar stubs
  - FreeBSD stubs in `fbsd_snd.c`, `fbsd_timer.c` are all no-ops

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
