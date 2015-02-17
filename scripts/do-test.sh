#!/bin/sh

set -e

uname -a
${CC} --version
sudo sh -c "echo 'deb http://http.debian.net/debian wheezy-backports main' > /etc/apt/sources.list.d/wb.list"
sudo DEBIAN_FRONTEND=noninteractive apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get -t wheezy-backports install libsdl2-dev
make ARCH=LINUX all
make clean
make ARCH=mingw all
