#!/bin/sh

set -e

DIFF="diff -u"

mv ./production/* ./
for TTYPE in short long
do
  for x in tests/data/*.drf
  do
    DIG_OPTS="/S:0 /E:${x}"
    if [ "${TTYPE}" = "long" ]
    then
      TSIZE=`du -k ${x} | awk '{print $1}'`
      if [ ${TSIZE} -gt 5 ]
      then
	continue
      fi
    else
      DIG_OPTS="/Q ${DIG_OPTS}"
    fi
    TFNAME="`basename ${x}`"
    TRFNAME="${TFNAME}.out"
    echo -n "${TFNAME} (${TTYPE}): "
    SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger ${DIG_OPTS} > "${TRFNAME}" 2>/dev/null
    ${DIFF} "tests/results/${TRFNAME}" "${TRFNAME}"
    echo "PASS"
  done
done
