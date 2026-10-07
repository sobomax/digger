#!/bin/sh

set -e

DIFF="diff -u"
TEST_TYPES=${TEST_TYPES:-"quick short long xlong"}
DIGGER_BIN=${DIGGER_BIN:-}

# Run as many tests at once as there are CPUs
MAXJOBS=${TEST_JOBS:-`getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4`}
JOBDIR=`mktemp -d`
NSTARTED=0

# run_test name type options resultfile: the output goes to type-name.out,
# so that the same recording can run in several test types at once, and its
# DRF and eDRF (with the same result) too
run_test() {
  SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 \
    "${DIGGER_BIN}" ${3} > "${2}-${1}.out" 2>/dev/null && rc=0 || rc=$?
  if [ "${rc}" -ne 0 ]
  then
    echo "${1} (${2}): FAIL (exit status ${rc})"
    return 1
  fi
  ${DIFF} "tests/results/${4}" "${2}-${1}.out" || return 1
  echo "${1} (${2}): PASS"
}

# Start a test in the background, it leaves a .done file behind when done
start_test() {
  NSTARTED=$((NSTARTED + 1))
  (
    if run_test "$@"; then st=ok; else st=fail; fi
    echo "${st}" > "${JOBDIR}/${NSTARTED}.tmp"
    mv "${JOBDIR}/${NSTARTED}.tmp" "${JOBDIR}/${NSTARTED}.done"
  ) &
}

# Wait for fewer than $1 tests to be running
wait_jobs() {
  while [ $((NSTARTED - `ls "${JOBDIR}" | grep -c '\.done$'`)) -ge "$1" ]
  do
    sleep 0.1
  done
}

if [ -z "${DIGGER_BIN}" ]
then
  if [ -d ./production ]
  then
    mv ./production/* ./
    DIGGER_BIN=./digger
  elif ! DIGGER_BIN=`command -v digger`
  then
    echo "digger binary not found; set DIGGER_BIN or provide production/digger" >&2
    exit 1
  fi
fi
for TTYPE in ${TEST_TYPES}
do
  for x in tests/data/*.drf tests/data/*.edrf
  do
    DIG_OPTS="/E:${x}"
    DIG_OPT_FSPD="/S:0"
    DIG_OPT_HSPD="/S:20"
    TFNAME="`basename ${x}`"
    TRFNAME="${TFNAME%.*}.out" # Both the DRF's and the eDRF's
    # An eDRF converted from a DRF goes by the size of the latter, so that
    # both run in the same test types
    TSIZEF="${x}"
    case "${x}" in
    *.edrf)
      test -f "${x%.edrf}.drf" && TSIZEF="${x%.edrf}.drf"
      ;;
    esac
    # Size in KB (the file's length, du(1) varies with the file system)
    TSIZE=$(( (`wc -c < ${TSIZEF}` + 1023) / 1024 ))
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
    wait_jobs "${MAXJOBS}"
    start_test "${TFNAME}" "${TTYPE}" "${DIG_OPTS}" "${TRFNAME}"
  done
done
wait_jobs 1
wait
NFAILED=`cat "${JOBDIR}"/*.done 2>/dev/null | grep -c fail || true`
rm -rf "${JOBDIR}"
if [ "${NFAILED}" -ne 0 ]
then
  echo "${NFAILED} test(s) FAILED" >&2
  exit 1
fi

# The two Digger recordings, replayed over NetSim on localhost
if [ -z "${NO_NETSIM_TESTS}" ]
then
  DIGGER_BIN="${DIGGER_BIN}" sh ./scripts/do-test-netsim.sh
fi

if [ ! -z "${CI_COVERAGE}" ]
then
  mkdir digger_lcov
  lcov --directory . --capture --output-file digger_lcov/digger.info \
   --gcov-tool ${GITHUB_WORKSPACE}/scripts/gen-test-coverage.sh
fi
