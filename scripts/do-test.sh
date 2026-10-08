#!/bin/sh

set -e

. "${0%/*}/dep-versions.sub"
SDL_VER=${SDL_VER:-${SDL_VER_DEFAULT}}
ZLIB_VER=${ZLIB_VER:-${ZLIB_VER_DEFAULT}}

if [ "${CC}" != "clang" ]
then
  #sudo apt-get remove -qq -y mingw32
  sudo apt-get install -qq -y mingw-w64
  mkdir deps
  cd deps
  wget https://www.libsdl.org/release/SDL2-devel-${SDL_VER}-mingw.tar.gz
  tar xfz SDL2-devel-${SDL_VER}-mingw.tar.gz
  ZLIB_SVER=`echo ${ZLIB_VER} | sed 's|[.]||g'`
  wget http://zlib.net/zlib${ZLIB_SVER}.zip
  unzip zlib${ZLIB_SVER}.zip
  make -C zlib-${ZLIB_VER} PREFIX="${MGW64_PREF}-" -f win32/Makefile.gcc
  mkdir "zlib-${ZLIB_VER}/${MGW64_PREF}"
  mv zlib-${ZLIB_VER}/*.dll zlib-${ZLIB_VER}/*.a "zlib-${ZLIB_VER}/${MGW64_PREF}"
  make -C zlib-${ZLIB_VER} PREFIX="${MGW_PREF}-" -f win32/Makefile.gcc clean all
  cd ..
fi

for build_type in debug production
do
  make ARCH=LINUX BUILD_TYPE=${build_type} DIGGER_INSTRUMENTATION=ON clean all
  mv digger *.gcno *.o ${build_type}/
  if [ "${CC}" != "clang" ]
  then
    make ARCH=MINGW BUILD_TYPE=${build_type} DIGGER_INSTRUMENTATION=ON MINGW_DEPS_ROOT=`pwd`/deps clean all
    make ARCH=MINGW64 BUILD_TYPE=${build_type} DIGGER_INSTRUMENTATION=ON MINGW_DEPS_ROOT=`pwd`/deps clean all
  fi
done
