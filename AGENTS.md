# AGENTS.md - Digger Remastered Development Guide

## Project Overview

Digger Remastered is a C99 game project using SDL2. The codebase follows a traditional Unix structure with CMake and GNU Makefile build systems. Target architecture is controlled via `ARCH` variable (LINUX, MINGW, MINGW64, FREEBSD, WASM).

## Build Commands

```bash
# Debug build (default)
make ARCH=LINUX BUILD_TYPE=debug

# Production build (stripped, optimized)
make ARCH=LINUX BUILD_TYPE=production

# CMake-based builds
cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles"
cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles"

# Full test suite (builds all targets and runs integration tests)
make do-test

# CMake-based test suite
make do-test-cmake

# Coverage report
make coverage-report
make coverage-report-html

# Clean build artifacts
make clean
```

## Running Tests

Integration tests use `.drf` replay files in `tests/data/`:

```bash
# Run a single test file
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./digger /E:tests/data/2test01.drf

# Run all quick tests
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger /Q /S:0 /E:tests/data/2test01.drf

# Run specific test types: quick, short, long, xlong
TEST_TYPE=short ./production/digger

# Coverage mode
CI_COVERAGE=1 ./scripts/do-test-run.sh
```

## Code Style Guidelines

### C Standard and Compiler Flags

- Use C99 (`-std=c99`) for portable code
- Enable all warnings: `-Wall -Wformat=2 -Wextra -pedantic`
- Use fixed-width integer types (`int16_t`, `uint32_t`, `int8_t`, etc.)
- Use `bool` from `<stdbool.h>` for boolean values

### Naming Conventions

- **Functions**: `snake_case` (e.g., `initdigger`, `drawemeralds`)
- **Constants/Macros**: `UPPER_SNAKE_CASE` (e.g., `DIR_RIGHT`, `MAX_DIGGERS`)
- **Struct members**: `snake_case` (e.g., `h`, `v`, `rx`, `ry`)
- **Global variables**: `snake_case` with prefix where appropriate (e.g., `digdat`, `dgstate`)
- **Type names**: `snake_case_t` or `struct name` pattern (e.g., `struct digger_obj`)

### File Organization

- Header files (`.h`) contain declarations only
- Source files (`.c`) contain implementations
- Group includes: system headers, project headers, local headers
- Alphabetical ordering within include groups
- Use include guards in headers: `#ifndef __FILENAME_H / #define __FILENAME_H`

### Comments and Documentation

- Use `/* ... */` block comments, not `//` line comments
- Copyright headers required for new files (BSD 2-clause template in `digger_types.h`)
- Document public API functions with doc comments
- Comment non-obvious logic, especially platform-specific code

### Error Handling

- Return error codes for recoverable errors
- Use `abort()` or exit for unrecoverable conditions in init
- Check all SDL/ZLIB function return values
- Validate input parameters at function entry

### Object-Oriented Patterns

The codebase uses a pseudo-OO pattern via function pointers:

```c
struct digger_obj {
  int (*animate)(struct digger_obj *);
  int (*put)(struct digger_obj *);
  // ...
};

#define CALL_METHOD(obj, method, args...) (obj)->method(obj, ## args)
```

Use `CALL_METHOD(&obj, animate)` for method calls. Define methods as static where possible.

### Memory Management

- Use `malloc`/`free` for dynamic allocation
- Check all `malloc` returns for NULL
- Free resources in reverse order of allocation
- Use `exit()` sparingly; prefer graceful error recovery

### Portability

- Use SDL2 for platform abstraction (graphics, input, audio)
- Platform-specific code guarded with `#ifdef LINUX`, `#ifdef MINGW`, etc.
- Avoid POSIX-only features on Windows builds
- Test on multiple architectures (LINUX, MINGW64) before committing

## Build Variants

| ARCH     | Platform   | Notes                           |
|----------|------------|---------------------------------|
| LINUX    | Linux      | Default, uses pkg-config        |
| FREEBSD  | FreeBSD    | Uses pkg-config                 |
| MINGW    | Windows 32 | Cross-compile, SDL2 from deps   |
| MINGW64  | Windows 64 | Cross-compile, SDL2 from deps   |
| WASM     | WebAssembly | Uses emcc                       |

## Dependencies

- SDL2 >= 2.0.9 (required)
- zlib >= 1.2.11 (required)
- libatomic (fallback for atomics on some platforms)
- math library (`-lm`)

## Common Tasks

```bash
# Build for Windows cross-compile
make ARCH=MINGW64 BUILD_TYPE=production MINGW_DEPS_ROOT=/path/to/deps

# Rebuild after header changes
make clean && make

# Run single integration test with verbose output
SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy ./digger /E:tests/data/ramon.drf

# Generate code coverage
CI_COVERAGE=1 make do-test-cmake
```

## Pre-commit Checks

Before committing:
1. Build succeeds on LINUX (debug and production)
2. Tests pass: `TEST_TYPE=quick sh ./scripts/do-test-run.sh`
3. No new compiler warnings with `-Wall -Wextra`
4. Code formatted consistently with project style
