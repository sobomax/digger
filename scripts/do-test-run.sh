#!/bin/sh

set -e

for x in tests/data/*.drf
do
  echo -n `basename ${x}`": "
  SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger_production /S:0 /E:${x}
done
