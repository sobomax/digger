#!/bin/sh

set -e

uname -a
${CC} --version
sudo apt-add-repository --yes ppa:zoogie/sdl2-snapshots
sudo apt-get update -qq
sudo apt-get remove -qq -y mingw32
sudo apt-get install -qq libsdl2-dev mingw-w64
mkdir deps
cd deps
wget https://www.libsdl.org/release/SDL2-devel-2.0.3-mingw.tar.gz
tar xfz SDL2-devel-2.0.3-mingw.tar.gz
wget http://zlib.net/zlib128-dll.zip
mkdir zlib-1.2.8
cd zlib-1.2.8
unzip ../zlib128-dll.zip
cd ../..

make ARCH=LINUX all
make ARCH=MINGW clean
make ARCH=MINGW MINGW_DEPS_ROOT=`pwd`/deps all
