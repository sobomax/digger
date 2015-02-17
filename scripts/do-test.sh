#!/bin/sh

set -e

uname -a
${CC} --version
sudo sh -c "echo 'deb http://http.debian.net/debian wheezy-backports main' > /etc/apt/sources.list.d/wb.list"
gpg --keyserver pgpkeys.mit.edu --recv-key 8B48AD6246925553
gpg -a --export 8B48AD6246925553 | sudo apt-key add -
sudo DEBIAN_FRONTEND=noninteractive apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get -t wheezy-backports install libpulse-dev libsdl2-dev
make ARCH=LINUX all
make clean
make ARCH=mingw all
