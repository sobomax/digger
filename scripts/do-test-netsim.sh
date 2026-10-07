#!/bin/sh

# NetSim replay tests: every two Digger eDRF recording is played over NetSim
# by two digger instances talking to each other on localhost, each one
# sending its own player's recorded controls. Both have to receive the
# recorded controls of the other player, pass the recording's state
# checkpoints and end the game the same way, also with packets being lost.
# So does a game of one quit half way through by either player (Q), which
# both have to leave on the same frame.

set -e

DIGGER_BIN=${DIGGER_BIN:-./digger}
# Run from elsewhere too, see quit_rec()
case "${DIGGER_BIN}" in
/*) ;;
*) DIGGER_BIN="${PWD}/${DIGGER_BIN}" ;;
esac
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
  got=`tr -d '\r' < "${TMPD}/${1}.out" | grep '^score='`
  if [ "${rc}" -ne 0 -o "${got}" != "${2}" ]
  then
    echo "    ${1}: FAIL (exit status ${rc}, got \"${got}\", expected" \
      "\"${2}\")"
    grep 'eDRF:' "${TMPD}/${1}.err" | sed 's|^|      |' || true
    return 1
  fi
}

# quit_rec rec slot out: rec, quit by player slot+1 at its middle checkpoint,
# as a recording of its own in ${TMPD}, named out (played back, for its E
# and Z); its result. The files are given to digger relative to ${TMPD}:
# MSYS would mangle the absolute names in the options.
quit_rec() {
  nckpt=`grep -c '^C ' "${1}"`
  awk -v k=$((nckpt / 2)) '{ print } /^C / { if (++c == k) exit }' \
    "${1}" > "${TMPD}/cut.edrf"
  echo "Q ${2}" >> "${TMPD}/cut.edrf"
  (
    cd "${TMPD}"
    # It ends before its game does (status 4)
    env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "${DIGGER_BIN}" \
      /Q /S:0 /R:"${3}" /E:cut.edrf > /dev/null 2>&1 || true
    env SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy DIGGER_CI_RUN=1 \
      "${DIGGER_BIN}" /Q /S:0 /E:"${3}" 2> /dev/null | tr -d '\r'
  )
}

# replay_test rec expected label: rec replayed over NetSim, with each loss
replay_test() {
  for loss in ${LOSSES}
  do
    penv="DIGGER_NETSIM_REPLAY=${1}"
    test "${loss}" != none && penv="${penv} ${loss}"
    run_peer alice "${penv}" /N:alice-bob@:${PORT}
    sleep 1
    run_peer bob "${penv} DIGGER_NETSIM_REPLAY_START=1" \
      /N:bob@127.0.0.1:${PORT}-alice
    wait
    if check_peer alice "${2}" && check_peer bob "${2}"
    then
      echo "${3} (netsim, loss: ${loss}): PASS"
    else
      echo "${3} (netsim, loss: ${loss}): FAIL"
      NFAILED=$((NFAILED + 1))
    fi
    rm -rf "${TMPD}"/alice* "${TMPD}"/bob*
  done
}

NFAILED=0
for rec in tests/data/*.edrf
do
  # Only two Digger games can be played over NetSim
  sed -n 3p "${rec}" | grep -q '^M2\(I[0-9]*\)*$' || continue
  name=`basename "${rec}"`
  # The same result, to the tick
  replay_test "${PWD}/${rec}" "`cat "tests/results/${name%.edrf}.out"`" \
    "${name}"
  for slot in 0 1
  do
    expected=`quit_rec "${rec}" ${slot} quit${slot}.edrf`
    replay_test "${TMPD}/quit${slot}.edrf" "${expected}" \
      "${name} quit by player $((slot + 1))"
  done
done

if [ "${NFAILED}" -ne 0 ]
then
  echo "${NFAILED} NetSim test(s) FAILED" >&2
  exit 1
fi
