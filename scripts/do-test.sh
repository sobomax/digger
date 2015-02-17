#!/bin/sh

set -e

uname -a
${CC} --version
make ARCH=LINUX all
make clean
make ARCH=mingw all
