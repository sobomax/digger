#!/bin/sh

# NetSim replay tests: every two Digger eDRF recording is played over NetSim
# by two digger instances talking to each other on localhost, each one
# sending its own player's recorded controls. Both have to receive the
# recorded controls of the other player, pass the recording's state
# checkpoints and end the game the same way, also with packets being lost.
# So does a game of one quit half way through (Q) by either player, or by
# both on the same frame, which both have to leave on the same frame, one
# whose call is never ACKed, which goes on all the same, one whose answer
# is lost twice, which only starts late, and one whose caller is gone
# right after its ACK, which the answering side has to give up on.

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
    # SIGTERM, then SIGKILL if that's not enough (as with SDL, which only
    # queues it, for a main loop that may be blocked)
    ( sleep "${NETSIM_TIMEOUT}"; kill "${pid}" 2>/dev/null && \
      touch "${TMPD}/${peer}.timeout" && sleep 5 && \
      kill -9 "${pid}" 2>/dev/null ) &
    wpid=$!
    rc=0
    wait "${pid}" || rc=$?
    kill "${wpid}" 2>/dev/null || true
    echo "${rc}" > "${TMPD}/${peer}.rc"
  ) &
}

# peer_log name: the peer's log, as ${TMPD}/name.log: its stderr, and on
# Windows its DIGGER.log
peer_log() {
  cat "${TMPD}/${1}.err" "${TMPD}/${1}"/DIGGER.log 2>/dev/null | \
    tr -d '\r' | grep -v '^GetINIString: ' > "${TMPD}/${1}.log" || true
}

# timeline: both peers' logs (see peer_log()) as one, in the order of their
# times (the same clock for both, with DIGGER_LOG_T0=0), each line told by
# whose it is; a line without a time goes with the one before it of its
# peer, e.g.
#    42.160:alice: foo bar
#          :alice: no time, so after foo bar
#    43.483:bob: barfoo
timeline() {
  tab=`printf '\t'`
  for p in alice bob
  do
    test -f "${TMPD}/${p}.log" || peer_log "${p}"
    awk -v peer="${p}" '
      /^\[-?[0-9]+\.[0-9]+\] / {
        ts = substr($1, 2, length($1) - 2)
        sub(/^[^ ]* /, "")
        printf("%s\t%s\t%d\t%s\t%s\n", ts, peer, NR, ts, $0)
        next
      }
      { printf("%s\t%s\t%d\t\t%s\n", ts == "" ? 0 : ts, peer, NR, $0) }
    ' "${TMPD}/${p}.log"
  done | sort -t "${tab}" -k1,1n -k2,2 -k3,3n | \
    awk -F "${tab}" '{ printf("%10s:%s: %s\n", $4, $2, $5) }'
}

# check_peer name expected player: exit status 0, the expected result, as
# the player expected; what it had to say if not (see test_result())
check_peer() {
  rc=`cat "${TMPD}/${1}.rc"`
  got=`tr -d '\r' < "${TMPD}/${1}.out" | grep '^score=' || true`
  peer_log "${1}"
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

# replay_test rec_alice rec_bob expected label [env_alice [env_bob
# [losses]]]: the recordings replayed over NetSim, by alice (player 2, see
# check_peer()) and bob (player 1, as he starts the game), each with its
# own environment if given, with each loss (or those given)
replay_test() {
  for loss in ${7:-${LOSSES}}
  do
    # Both peers' logs in the monotonic clock's time, which then go
    # together
    lenv="DIGGER_LOG_T0=0"
    test "${loss}" != none && lenv="${lenv} ${loss}"
    run_peer alice "DIGGER_NETSIM_REPLAY=${1} ${lenv} ${5}" \
      /N:alice-bob@:${PORT}
    sleep 1
    run_peer bob "DIGGER_NETSIM_REPLAY=${2} DIGGER_NETSIM_REPLAY_START=1 \
      ${lenv} ${6}" /N:bob@127.0.0.1:${PORT}-alice
    wait
    # Both of them, whichever fails
    ok=true
    check_peer alice "${3}" 2 || ok=false
    check_peer bob "${3}" 1 || ok=false
    test_result "${4} (netsim, loss: ${loss})" "${ok}"
  done
}

# test_result label ok: the test's outcome, with the end of both peers'
# logs in the order of their times if it failed (see timeline()), and the
# run's files kept then (see NETSIM_KEEP); those of its peers gone
test_result() {
  if [ "${2}" = "true" ]
  then
    echo "${1}: PASS"
  else
    echo "${1}: FAIL"
    NFAILED=$((NFAILED + 1))
    timeline > "${TMPD}/timeline.log"
    tail -n $((NETSIM_TAIL * 2)) "${TMPD}/timeline.log" | sed 's|^|      |'
    if [ -n "${NETSIM_KEEP}" ]
    then
      keep="${NETSIM_KEEP}/`echo "${1}" | tr -c 'A-Za-z0-9._=-' _`"
      mkdir -p "${keep}"
      cp -R "${TMPD}"/* "${keep}/"
      echo "      kept in ${keep}"
    fi
  fi
  rm -f "${TMPD}/timeline.log"
  rm -rf "${TMPD}"/alice* "${TMPD}"/bob*
}

# vanish_test rec label: bob ACKs alice's answer to his INVITE and is gone
# then (no media, no BYE either): alice has to give up on the session by
# the sync timeout from that ACK on (out of her slow start by it), rather
# than wait for him for ever (a watchdog of its own for that)
vanish_test() {
  watchdog=${NETSIM_TIMEOUT}
  NETSIM_TIMEOUT=30
  run_peer alice "DIGGER_NETSIM_REPLAY=${1} DIGGER_LOG_T0=0" \
    /N:alice-bob@:${PORT}
  sleep 1
  run_peer bob "DIGGER_NETSIM_REPLAY=${1} DIGGER_NETSIM_REPLAY_START=1 \
    DIGGER_LOG_T0=0 DIGGER_NETSIM_MUTE_AFTER_ACK=1" \
    /N:bob@127.0.0.1:${PORT}-alice
  wait
  peer_log alice
  peer_log bob
  rc=`cat "${TMPD}/alice.rc"`
  ok=true
  if [ "${rc}" -eq 0 ] || [ -f "${TMPD}/alice.timeout" ] ||
     ! grep -q 'slow start over.*, by ACK' "${TMPD}/alice.log" ||
     ! grep -q 'session failed: retransmit timeout: frame' "${TMPD}/alice.log"
  then
    echo "    alice: FAIL (exit status ${rc}, to fail by the sync timeout" \
      "after the ACK)"
    test -f "${TMPD}/alice.timeout" && \
      echo "      stopped after ${NETSIM_TIMEOUT}s"
    ok=false
  fi
  NETSIM_TIMEOUT=${watchdog}
  test_result "${2} (netsim)" "${ok}"
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
  # Bob never ACKs alice's answer to his INVITE: once its (short) Timer L
  # is up, alice keeps the call, as it's live, and has to have been told
  replay_test "${PWD}/${rec}" "${PWD}/${rec}" \
    "`cat "tests/results/${name%.edrf}.out"`" "${name} without ACK" \
    "DIGGER_NETSIM_EXPECT_NO_ACK=1 DIGGER_NETSIM_SIP_TIMER_L=200" \
    "DIGGER_NETSIM_SIP_NO_ACK=1" none
  # Alice's answer lost twice: bob only gets it 1.5 s (T1 + 2*T1) on, past
  # the sync timeout, which alice's frames are not to time out by meanwhile
  replay_test "${PWD}/${rec}" "${PWD}/${rec}" \
    "`cat "tests/results/${name%.edrf}.out"`" "${name} answer lost twice" \
    "DIGGER_NETSIM_SIP_DROP_2XX=2" "" none
  vanish_test "${PWD}/${rec}" "${name} caller gone after its ACK"
done

if [ "${NFAILED}" -ne 0 ]
then
  echo "${NFAILED} NetSim test(s) FAILED" >&2
  exit 1
fi
