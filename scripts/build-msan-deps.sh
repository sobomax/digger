#!/bin/sh

# MSan needs instrumented libraries as well as an instrumented application.
# Build only SDL's dummy/software backends so no uninstrumented display,
# audio, or device libraries enter the headless test process.
set -eu

. "${0%/*}/dep-versions.sub"
SDL_VER=${SDL_VER:-${SDL_VER_DEFAULT}}
ZLIB_VER=${ZLIB_VER:-${ZLIB_VER_DEFAULT}}
SDL_SOURCE_SHA256=${SDL_SOURCE_SHA256:-${SDL_SOURCE_SHA256_DEFAULT}}
ZLIB_SOURCE_SHA256=${ZLIB_SOURCE_SHA256:-${ZLIB_SOURCE_SHA256_DEFAULT}}

prefix=${1:?Usage: build-msan-deps.sh install-prefix work-directory}
work=${2:?Usage: build-msan-deps.sh install-prefix work-directory}
mkdir -p "${prefix}" "${work}"
prefix=`cd "${prefix}" && pwd`
work=`cd "${work}" && pwd`
flags='-fsanitize=memory -fsanitize-memory-track-origins=2 -fno-omit-frame-pointer -fPIC'
ldflags='-fsanitize=memory -pie'
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-4}

curl -fsSL --retry 3 https://www.libsdl.org/release/SDL2-${SDL_VER}.tar.gz \
  -o "${work}/SDL2.tar.gz"
curl -fsSL --retry 3 https://zlib.net/fossils/zlib-${ZLIB_VER}.tar.gz \
  -o "${work}/zlib.tar.gz"
(
  cd "${work}"
  printf '%s  SDL2.tar.gz\n' "${SDL_SOURCE_SHA256}" | sha256sum -c -
  printf '%s  zlib.tar.gz\n' "${ZLIB_SOURCE_SHA256}" | sha256sum -c -
  tar -xzf SDL2.tar.gz
  tar -xzf zlib.tar.gz
)

cmake -S "${work}/zlib-${ZLIB_VER}" -B "${work}/zlib-build" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_C_FLAGS="${flags}" \
  -DCMAKE_EXE_LINKER_FLAGS="${ldflags}" \
  -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_STATIC=ON -DZLIB_BUILD_TESTING=OFF
cmake --build "${work}/zlib-build" --parallel "${jobs}"
cmake --install "${work}/zlib-build"

cmake -S "${work}/SDL2-${SDL_VER}" -B "${work}/sdl-build" \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX="${prefix}" \
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_C_FLAGS="${flags}" \
  -DCMAKE_EXE_LINKER_FLAGS="${ldflags}" \
  -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF -DSDL_TESTS=OFF \
  -DSDL_ASSEMBLY=OFF -DSDL_LIBC=ON \
  -DSDL_DUMMYVIDEO=ON -DSDL_DUMMYAUDIO=ON \
  -DSDL_X11=OFF -DSDL_WAYLAND=OFF -DSDL_KMSDRM=OFF -DSDL_RPI=OFF \
  -DSDL_VIVANTE=OFF -DSDL_OFFSCREEN=OFF -DSDL_OPENGL=OFF -DSDL_OPENGLES=OFF \
  -DSDL_VULKAN=OFF -DSDL_ALSA=OFF -DSDL_PULSEAUDIO=OFF -DSDL_PIPEWIRE=OFF \
  -DSDL_JACK=OFF -DSDL_ESD=OFF -DSDL_ARTS=OFF -DSDL_NAS=OFF -DSDL_OSS=OFF \
  -DSDL_SNDIO=OFF -DSDL_DISKAUDIO=OFF -DSDL_DBUS=OFF -DSDL_IBUS=OFF \
  -DSDL_LIBUDEV=OFF -DSDL_HIDAPI=OFF -DSDL_JOYSTICK=OFF -DSDL_HAPTIC=OFF \
  -DSDL_SENSOR=OFF -DSDL_LIBSAMPLERATE=OFF
cmake --build "${work}/sdl-build" --parallel "${jobs}"
cmake --install "${work}/sdl-build"
