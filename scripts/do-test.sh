#!/bin/sh

set -e
set -x

TOOLSPATH=`realpath ${0}`
TOOLSDIR=`dirname ${TOOLSPATH}`

. "${TOOLSDIR}/test.common.sub"

MLOG="${BUILDDIR}/monitor.log"
TESTS="empty foobar 100trying ACK OPTIONS INVITE CANCEL 200OK"

if [ -e "${MLOG}" ]
then
  rm "${MLOG}"
fi

. "${SRCDIR}/scripts/test_start.sub"
MON_RC=${?}
MON_PID=${!}

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
kill -TERM ${MON_PID}
wait ${MON_PID} || true
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
