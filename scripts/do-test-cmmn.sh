#!/bin/sh

set -e

uname -a
${CC} --version
sudo apt-add-repository --yes ppa:zoogie/sdl2-snapshots
sudo apt-get update -qq -y
sudo apt-get install -qq -y libsdl2-dev

mkdir debug production
