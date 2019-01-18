#!/bin/sh

set -e

mv ./production/* ./
for x in tests/data/*.drf
do
  echo -n `basename ${x}`": "
  SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger /S:0 /E:${x}
done
