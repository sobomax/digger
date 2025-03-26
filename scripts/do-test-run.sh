#!/bin/sh

set -e

DIFF="diff -u"
TEST_TYPES=${TEST_TYPES:-"quick short long xlong"}

run_test() {
  SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 ./digger ${3} > "${4}" 2>/dev/null
  echo -n "${1} (${2}): "
  ${DIFF} "tests/results/${4}" "${4}"
  echo "PASS"
}

wait_pids() {
  for pid in ${PIDS}
  do
    wait "${pid}"
  done
  PIDS=""
}

mv ./production/* ./
for TTYPE in ${TEST_TYPES}
do
  for x in tests/data/*.drf
  do
    DIG_OPTS="/E:${x}"
    DIG_OPT_FSPD="/S:0"
    DIG_OPT_HSPD="/S:20"
    TFNAME="`basename ${x}`"
    TRFNAME="${TFNAME}.out"
    TSIZE=`du -k ${x} | awk '{print $1}'`
    if [ "${TTYPE}" = "long" -o "${TTYPE}" = "xlong" ]
    then
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
	if [ ${frames} -gt 8000 ]
        then
          continue
	fi
        if [ ${frames} -gt 7000 ]
        then
          DIG_OPTS="${DIG_OPT_HSPD} ${DIG_OPTS}"
        fi
      fi
    else
      if [ "${TTYPE}" = "quick" -a ${TSIZE} -gt 15 ]
      then
        continue
      fi
      DIG_OPTS="/Q ${DIG_OPT_FSPD} ${DIG_OPTS}"
    fi
    run_test "${TFNAME}" "${TTYPE}" "${DIG_OPTS}" "${TRFNAME}" &
    PIDS="${!} ${PIDS}"
    test `echo ${PIDS} | wc -w` -gt 5 && wait_pids
  done
done
wait_pids

if [ ! -z "${CI_COVERAGE}" ]
then
  mkdir digger_lcov
  lcov --directory . --capture --output-file digger_lcov/digger.info \
   --gcov-tool ${GITHUB_WORKSPACE}/scripts/gen-test-coverage.sh
fi
