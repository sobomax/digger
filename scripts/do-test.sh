#!/bin/sh

set -e

uname -a
${CC} --version
sudo apt-get DEBIAN_FRONTEND=noninteractive install libsdl2-dev
make ARCH=LINUX all
make clean
make ARCH=mingw all
