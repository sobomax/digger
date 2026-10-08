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

# fetch name sha256 url...: the tarball, from the first of the urls that
# has it as it should be (by its checksum), a few rounds of them if need
# be. What came instead is told, as it may well be a page of the server's
# in its place (one turning a CI runner away, say) rather than an error.
fetch() {
  name="${1}"
  sum="${2}"
  shift 2
  for round in 1 2 3
  do
    for url in "$@"
    do
      if curl -fsSL --retry 3 "${url}" -o "${work}/${name}" && \
         printf '%s  %s\n' "${sum}" "${work}/${name}" | sha256sum -c --status -
      then
        echo "${name}: OK, from ${url}"
        return 0
      fi
      if [ -f "${work}/${name}" ]
      then
        echo "${name}: not the one from ${url}: `wc -c < "${work}/${name}"`" \
          "bytes, starting with \"`head -c 64 "${work}/${name}" | \
          tr -c '[:print:]' '.'`\"" >&2
      fi
    done
    sleep $((round * 10))
  done
  echo "${name}: no good copy from any of $*" >&2
  return 1
}

# Each from its project's own site, or its release on GitHub, the same
fetch SDL2.tar.gz "${SDL_SOURCE_SHA256}" \
  https://www.libsdl.org/release/SDL2-${SDL_VER}.tar.gz \
  https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VER}/SDL2-${SDL_VER}.tar.gz
fetch zlib.tar.gz "${ZLIB_SOURCE_SHA256}" \
  https://zlib.net/fossils/zlib-${ZLIB_VER}.tar.gz \
  https://github.com/madler/zlib/releases/download/v${ZLIB_VER}/zlib-${ZLIB_VER}.tar.gz
(
  cd "${work}"
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
