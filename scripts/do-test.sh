#!/bin/sh

set -e

uname -a
${CC} --version
sudo DEBIAN_FRONTEND=noninteractive apt-get install libsdl2-dev
make ARCH=LINUX all
make clean
make ARCH=mingw all
