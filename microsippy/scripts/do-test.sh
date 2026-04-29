#!/bin/sh

set -e
set -x

TOOLSPATH=`realpath ${0}`
TOOLSDIR=`dirname ${TOOLSPATH}`

. "${TOOLSDIR}/test.common.sub"

MLOG="${BUILDDIR}/monitor.log"
TESTS="empty foobar 100trying ACK OPTIONS INVITE CANCEL 200OK"
MON_PID=
MON_PIPE_PID=

cleanup()
{
  if [ ! -z "${MON_PID}" ]
  then
    kill -TERM "${MON_PID}" 2>/dev/null || true
    wait "${MON_PID}" 2>/dev/null || true
  fi
  if [ ! -z "${MON_PIPE_PID}" ]
  then
    wait "${MON_PIPE_PID}" 2>/dev/null || true
  fi
}

dump_failure_logs()
{
  echo "==== basic test failure diagnostics ===="
  echo "SRCDIR=${SRCDIR}"
  echo "BUILDDIR=${BUILDDIR}"
  echo "BRD_IP=${BRD_IP}"
  echo "MON_PID=${MON_PID}"
  echo "MON_PIPE_PID=${MON_PIPE_PID}"
  echo "---- loopback diagnostics ----"
  cat /etc/hosts 2>/dev/null || true
  getent hosts localhost 2>/dev/null || true
  getent hosts 127.0.0.1 2>/dev/null || true
  ip addr show lo 2>/dev/null || true
  ip route get 127.0.0.1 2>/dev/null || true
  ifconfig lo 2>/dev/null || true
  ss -lunp 2>/dev/null || netstat -anu 2>/dev/null || true
  python3 -c 'import socket; s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", 0)); print("python bind 127.0.0.1:0 ok", s.getsockname())' 2>&1 || true
  echo "---- generated files ----"
  ls -l "${BUILDDIR}"/*.req "${BUILDDIR}"/*.res "${BUILDDIR}"/*.res.pp 2>/dev/null || true
  if [ -e "${MLOG}" ]
  then
    echo "---- ${MLOG} ----"
    cat "${MLOG}"
  else
    echo "monitor log not found: ${MLOG}"
  fi
}

finish()
{
  rc=${?}
  set +e
  if [ ${rc} -ne 0 ]
  then
    dump_failure_logs
  fi
  cleanup
  exit ${rc}
}
trap finish EXIT

if [ -e "${MLOG}" ]
then
  rm "${MLOG}"
fi

. "${SRCDIR}/scripts/test_start.sub"
MON_RC=${?}
i=0
while [ ${i} -lt 10 ] && [ ! -s "${MON_PIDFILE}" ]
do
  sleep 1
  i=$((${i} + 1))
done
if [ ! -s "${MON_PIDFILE}" ]
then
  exit 1
fi
MON_PID=`cat "${MON_PIDFILE}"`

. "${SRCDIR}/scripts/test_waitready.sub"

if [ ! -z "${BRD_IP}" ]
then
  for tst in ${TESTS}
  do
    REQFILE="${BUILDDIR}/${tst}.req"
    RESFILE="${BUILDDIR}/${tst}.res"
    sed "s|%%BRD_IP%%|${BRD_IP}|g" "${TOOLSDIR}/${tst}.raw" > "${REQFILE}"
    for testing in 1 2 3
    do
      nc -w 1 -u "${BRD_IP}" 5060 < "${REQFILE}" > "${RESFILE}"
    done
  done
  sleep 3
fi
if [ -z "${BRD_IP}" ]
then
  exit 1
fi
grep -q 'Waiting for data' "${MLOG}"
grep -q 'SIP/2.0 100 Trying' "${MLOG}"
for tst in ${TESTS}
do
  EXPFILE="${TOOLSDIR}/expect/${tst}"
  REQFILE="${BUILDDIR}/${tst}.req"
  RESFILE="${BUILDDIR}/${tst}.res"
  if [ ! -e "${EXPFILE}" ]
  then
    EXPFILE="${REQFILE}"
  else
    RESFILE_pp="${RESFILE}.pp"
    sed "s|${BRD_IP}|%%BRD_IP%%|g" "${RESFILE}" > "${RESFILE_pp}"
    RESFILE="${RESFILE_pp}"
  fi
  ${DIFF} "${EXPFILE}" "${RESFILE}"
done
for s in "usipy_sip_msg_ctor_fromwire" "usipy_sip_msg_parse_hdrs" "heap remaining" "usipy_sip_req_parse_ruri" \
  "usipy_sip_msg_get_tid" "usipy_sip_res_ctor_fromreq"
do
  grep -w "${s}" "${MLOG}"
done
