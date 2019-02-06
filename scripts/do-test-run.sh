#!/bin/sh

set -e

DIFF="diff -u"
TEST_TYPES=${TEST_TYPES:-"short long xlong"}

mv ./production/* ./
for TTYPE in ${TEST_TYPES}
do
  for x in tests/data/*.drf
  do
    DIG_OPTS="/E:${x}"
    DIG_OPT_FSPD="/S:0"
    TFNAME="`basename ${x}`"
    TRFNAME="${TFNAME}.out"
    if [ "${TTYPE}" = "long" -o "${TTYPE}" = "xlong" ]
    then
      TSIZE=`du -k ${x} | awk '{print $1}'`
      if [ ${TSIZE} -gt 15 ]
      then
	continue
      fi
      if [ "${TTYPE}" = "long" ]
      then
	DIG_OPTS="${DIG_OPT_FSPD} ${DIG_OPTS}"
      fi
      if [ "${TTYPE}" = "xlong" ]
      then
        eval `cat tests/results/${TRFNAME}`
	if [ ${frames} -gt 7000 ]
        then
          continue
	fi
      fi
    else
      DIG_OPTS="/Q ${DIG_OPT_FSPD} ${DIG_OPTS}"
    fi
    echo -n "${TFNAME} (${TTYPE}): "
    SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger ${DIG_OPTS} > "${TRFNAME}" 2>/dev/null
    ${DIFF} "tests/results/${TRFNAME}" "${TRFNAME}"
    echo "PASS"
  done
done
