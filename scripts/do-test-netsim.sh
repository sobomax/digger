#!/bin/sh

# NetSim replay tests: every two Digger eDRF recording is played over NetSim
# by two digger instances talking to each other on localhost, each one
# sending its own player's recorded controls. Both have to receive the
# recorded controls of the other player, pass the recording's state
# checkpoints and end the game the same way, also with packets being lost.
# So does a game of one quit half way through (Q) by either player, or by
# both on the same frame, which both have to leave on the same frame.

set -e

DIGGER_BIN=${DIGGER_BIN:-./digger}
# Run from elsewhere too, see quit_rec()
case "${DIGGER_BIN}" in
/*) ;;
*) DIGGER_BIN="${PWD}/${DIGGER_BIN}" ;;
esac
NETSIM_TIMEOUT=${NETSIM_TIMEOUT:-120}
# For a failure: the last lines to show of each peer's log (its stderr,
# with the NetSim protocol's debug log in it), and a directory to keep
# all of the run's files in
NETSIM_TAIL=${NETSIM_TAIL:-30}
NETSIM_KEEP=${NETSIM_KEEP:-}

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
# watchdog, its exit status ends up in ${TMPD}/name.rc (and name.timeout
# is there if the watchdog had to stop it)
run_peer() {
  peer="${1}"
  peerenv="${2}"
  shift 2
  mkdir -p "${TMPD}/${peer}"
  (
    # In a directory of its own, for the DIGGER.log of Windows
    cd "${TMPD}/${peer}"
    env HOME="${TMPD}/${peer}" SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy \
      DIGGER_CI_RUN=1 DIGGER_NETSIM_DEBUG=1 ${peerenv} "${DIGGER_BIN}" \
      /Q /S:0 "$@" > "${TMPD}/${peer}.out" 2> "${TMPD}/${peer}.err" &
    pid=$!
    ( sleep "${NETSIM_TIMEOUT}"; kill "${pid}" 2>/dev/null && \
      touch "${TMPD}/${peer}.timeout" ) &
    wpid=$!
    rc=0
    wait "${pid}" || rc=$?
    kill "${wpid}" 2>/dev/null || true
    echo "${rc}" > "${TMPD}/${peer}.rc"
  ) &
}

# check_peer name expected player: exit status 0, the expected result, as
# the player expected; what it had to say if not
check_peer() {
  rc=`cat "${TMPD}/${1}.rc"`
  got=`tr -d '\r' < "${TMPD}/${1}.out" | grep '^score=' || true`
  # Its log: stderr, and on Windows its DIGGER.log
  cat "${TMPD}/${1}.err" "${TMPD}/${1}"/DIGGER.log 2>/dev/null | \
    tr -d '\r' | grep -v '^GetINIString: ' > "${TMPD}/${1}.log" || true
  # The player it was, as the session went (if NetSim's debug log has it)
  pl=`sed -n 's|.*session connected local_player=\([0-9]\).*|\1|p' \
    "${TMPD}/${1}.log" | head -1`
  if [ "${rc}" -ne 0 -o "${got}" != "${2}" ] ||
     [ -n "${pl}" -a "${pl}" != "${3}" ]
  then
    echo "    ${1} (player ${pl:-?} of ${3}): FAIL (exit status ${rc}," \
      "got \"${got}\", expected \"${2}\")"
    test -f "${TMPD}/${1}.timeout" && \
      echo "      stopped after ${NETSIM_TIMEOUT}s"
    tail -n "${NETSIM_TAIL}" "${TMPD}/${1}.log" | sed 's|^|      |'
    return 1
  fi
}

# quit_rec rec slot out: rec, quit early on (see below) by player slot+1,
# as a recording of its own in ${TMPD}, named out (played back, for its E
# and Z); its result. The files are given to digger relative to ${TMPD}:
# MSYS would mangle the absolute names in the options.
quit_rec() {
  # Not too far in: the 4th checkpoint (tick 1024), or the middle one
  ckpt=$((`grep -c '^C ' "${1}"` / 2))
  test "${ckpt}" -gt 4 && ckpt=4
  awk -v k=${ckpt} '{ print } /^C / { if (++c == k) exit }' \
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

# replay_test rec_alice rec_bob expected label: the recordings replayed over
# NetSim, by alice (player 2, see check_peer()) and bob (player 1, as he
# starts the game), with each loss
replay_test() {
  for loss in ${LOSSES}
  do
    lenv=""
    test "${loss}" != none && lenv="${loss}"
    run_peer alice "DIGGER_NETSIM_REPLAY=${1} ${lenv}" /N:alice-bob@:${PORT}
    sleep 1
    run_peer bob "DIGGER_NETSIM_REPLAY=${2} DIGGER_NETSIM_REPLAY_START=1 \
      ${lenv}" /N:bob@127.0.0.1:${PORT}-alice
    wait
    # Both of them, whichever fails
    ok=true
    check_peer alice "${3}" 2 || ok=false
    check_peer bob "${3}" 1 || ok=false
    if [ "${ok}" = "true" ]
    then
      echo "${4} (netsim, loss: ${loss}): PASS"
    else
      echo "${4} (netsim, loss: ${loss}): FAIL"
      NFAILED=$((NFAILED + 1))
      if [ -n "${NETSIM_KEEP}" ]
      then
        keep="${NETSIM_KEEP}/`echo "${4}-${loss}" | tr -c 'A-Za-z0-9._=-' _`"
        mkdir -p "${keep}"
        cp -R "${TMPD}"/* "${keep}/"
        echo "      kept in ${keep}"
      fi
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
  replay_test "${PWD}/${rec}" "${PWD}/${rec}" \
    "`cat "tests/results/${name%.edrf}.out"`" "${name}"
  # Quit by player 1, by player 2, and by both on the same frame: each one
  # replaying the recording of its own side then
  expected=`quit_rec "${rec}" 0 quit0.edrf`
  quit_rec "${rec}" 1 quit1.edrf > /dev/null
  q0="${TMPD}/quit0.edrf"
  q1="${TMPD}/quit1.edrf"
  replay_test "${q0}" "${q0}" "${expected}" "${name} quit by player 1"
  replay_test "${q1}" "${q1}" "${expected}" "${name} quit by player 2"
  replay_test "${q1}" "${q0}" "${expected}" "${name} quit by both players"
done

if [ "${NFAILED}" -ne 0 ]
then
  echo "${NFAILED} NetSim test(s) FAILED" >&2
  exit 1
fi
