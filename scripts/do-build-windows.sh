#!/bin/sh

set -e

SDL_VER=${SDL_VER:-2.32.10}
ZLIB_VER=${ZLIB_VER:-1.3.1}
MGW64_PREF=${MGW64_PREF:-x86_64-w64-mingw32}

mkdir -p deps
cd deps

if [ ! -d "SDL2-${SDL_VER}" ]; then
    echo "=== Download SDL2 ==================================================="
    curl -fsSL \
        "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-devel-${SDL_VER}-mingw.tar.gz" \
        | tar xz
else
    echo "=== Reuse cached SDL2 ==============================================="
fi

if [ ! -d "zlib-${ZLIB_VER}" ]; then
    echo "=== Download zlib ==================================================="
    curl -fsSL \
        "https://github.com/madler/zlib/releases/download/v${ZLIB_VER}/zlib-${ZLIB_VER}.tar.gz" \
        | tar xz
else
    echo "=== Reuse cached zlib source ========================================"
fi

if [ ! -f "zlib-${ZLIB_VER}/${MGW64_PREF}/zlib1.dll" ] || \
   [ ! -f "zlib-${ZLIB_VER}/${MGW64_PREF}/libz.a" ] || \
   [ ! -f "zlib-${ZLIB_VER}/${MGW64_PREF}/libz.dll.a" ]; then
    echo "=== Build zlib ======================================================"
    # In MSYS2 MINGW64, the cross-compiler is available as x86_64-w64-mingw32-gcc
    # but helper binutils tools are available without the prefix. Pass them
    # explicitly to avoid "No such file or directory" errors from zlib's makefile.
    make -C "zlib-${ZLIB_VER}" PREFIX="${MGW64_PREF}-" AR=ar RANLIB=ranlib \
        RC=windres STRIP=strip \
        -f win32/Makefile.gcc
    mkdir -p "zlib-${ZLIB_VER}/${MGW64_PREF}"
    mv "zlib-${ZLIB_VER}/"*.dll "zlib-${ZLIB_VER}/"*.a "zlib-${ZLIB_VER}/${MGW64_PREF}/"
else
    echo "=== Reuse cached zlib build ========================================="
fi

cd ..

DEPS_ROOT="$(pwd)/deps"

echo "=== Build Digger (debug) ============================================"
make ARCH=MINGW64 BUILD_TYPE=debug MGW64_PREF="${MGW64_PREF}" \
    SDL_VER="${SDL_VER}" ZLIB_VER="${ZLIB_VER}" \
    MINGW_DEPS_ROOT="${DEPS_ROOT}" clean all
cp digger.exe digger_debug.exe

echo "=== Build Digger (production) ======================================="
make ARCH=MINGW64 BUILD_TYPE=production MGW64_PREF="${MGW64_PREF}" \
    SDL_VER="${SDL_VER}" ZLIB_VER="${ZLIB_VER}" \
    MINGW_DEPS_ROOT="${DEPS_ROOT}" clean all

echo "=== Copy runtime DLLs ==============================================="
cp "deps/SDL2-${SDL_VER}/${MGW64_PREF}/bin/SDL2.dll" .
cp "deps/zlib-${ZLIB_VER}/${MGW64_PREF}/zlib1.dll" .

echo "=== Create distribution ZIP ========================================="
zip -j digger-win64.zip digger.exe digger_debug.exe digger.txt SDL2.dll zlib1.dll
