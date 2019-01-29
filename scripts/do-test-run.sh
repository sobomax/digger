#!/bin/sh

set -e

mv ./production/* ./
for x in tests/data/*.drf
do
  echo -n `basename ${x}`": "
  SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger /Q /S:0 /E:${x}
done
