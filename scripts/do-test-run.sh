#!/bin/sh

set -e

DIFF="diff -u"

mv ./production/* ./
for x in tests/data/*.drf
do
  TFNAME="`basename ${x}`"
  TRFNAME="${TFNAME}.out"
  echo -n "${TFNAME}: "
  SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger /Q /S:0 /E:${x} > "${TRFNAME}" 2>/dev/null
  ${DIFF} "tests/results/${TRFNAME}" "${TRFNAME}"
  echo "PASS"
done
