#!/bin/sh

# NetSim replay tests: every two Digger eDRF recording is played over NetSim
# by two digger instances talking to each other on localhost, each one
# sending its own player's recorded controls. Both have to receive the
# recorded controls of the other player, pass the recording's state
# checkpoints and end the game the same way, also with packets being lost.

set -e

DIGGER_BIN=${DIGGER_BIN:-./digger}
NETSIM_TIMEOUT=${NETSIM_TIMEOUT:-120}

# Packet loss variants, as environment settings for both peers
LOSSES="none DIGGER_NETSIM_RX_DROP_EVERY=7 DIGGER_NETSIM_TX_DROP_EVERY=5"

# A port of its own, as other runs may share the network (e.g. those for
# the other platforms of a multi-platform docker build, which passes
# TARGETPLATFORM): from the platform's name, so the same one every time
PLATFORM=${PLATFORM:-${TARGETPLATFORM:-`uname -s`/`uname -m`}}
if command -v md5sum >/dev/null 2>&1; then MD5=md5sum; else MD5=md5; fi
PORT=$((1024 + 0x`printf '%s' "${PLATFORM}" | ${MD5} | cut -c1-4` % 64512))

TMPD=`mktemp -d`
trap 'rm -rf "${TMPD}"' EXIT

# run_peer name env options...: start a peer in the background with a
# watchdog, its exit status ends up in ${TMPD}/name.rc
run_peer() {
  peer="${1}"
  peerenv="${2}"
  shift 2
  mkdir -p "${TMPD}/${peer}"
  (
    env HOME="${TMPD}/${peer}" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
      DIGGER_CI_RUN=1 ${peerenv} "${DIGGER_BIN}" /Q /S:0 "$@" \
      > "${TMPD}/${peer}.out" 2> "${TMPD}/${peer}.err" &
    pid=$!
    ( sleep "${NETSIM_TIMEOUT}"; kill "${pid}" 2>/dev/null ) &
    wpid=$!
    rc=0
    wait "${pid}" || rc=$?
    kill "${wpid}" 2>/dev/null || true
    echo "${rc}" > "${TMPD}/${peer}.rc"
  ) &
}

# check_peer name expected: exit status 0 and the expected score and level
check_peer() {
  rc=`cat "${TMPD}/${1}.rc"`
  got=`grep '^score=' "${TMPD}/${1}.out" | sed 's| frames=.*||'`
  if [ "${rc}" -ne 0 -o "${got}" != "${2}" ]
  then
    echo "    ${1}: FAIL (exit status ${rc}, got \"${got}\", expected" \
      "\"${2}\")"
    grep 'eDRF:' "${TMPD}/${1}.err" | sed 's|^|      |' || true
    return 1
  fi
}

NFAILED=0
for rec in tests/data/*.edrf
do
  # Only two Digger games can be played over NetSim
  sed -n 3p "${rec}" | grep -q '^M2\(I[0-9]*\)*$' || continue
  name=`basename "${rec}"`
  # The result, but the frame count (NetSim has frames that aren't ticks)
  expected=`sed 's| frames=.*||' "tests/results/${name%.edrf}.out"`
  for loss in ${LOSSES}
  do
    penv="DIGGER_NETSIM_REPLAY=${PWD}/${rec}"
    test "${loss}" != none && penv="${penv} ${loss}"
    run_peer alice "${penv}" /N:alice-bob@:${PORT}
    sleep 1
    run_peer bob "${penv} DIGGER_NETSIM_REPLAY_START=1" \
      /N:bob@127.0.0.1:${PORT}-alice
    wait
    if check_peer alice "${expected}" && check_peer bob "${expected}"
    then
      echo "${name} (netsim, loss: ${loss}): PASS"
    else
      echo "${name} (netsim, loss: ${loss}): FAIL"
      NFAILED=$((NFAILED + 1))
    fi
    rm -rf "${TMPD}"/alice* "${TMPD}"/bob*
  done
done

if [ "${NFAILED}" -ne 0 ]
then
  echo "${NFAILED} NetSim test(s) FAILED" >&2
  exit 1
fi
