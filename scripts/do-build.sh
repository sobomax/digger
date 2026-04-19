#!/bin/sh

set -e

TOOLSPATH=`realpath ${0}`
TOOLSDIR=`dirname ${TOOLSPATH}`

. "${TOOLSDIR}/test.common.sub"

IDF_TGT=${2:-"build"}

if [ "${IDF_TGT}" = "build" ]
then
  cp "${SRCDIR}/sdkconfig.release"  "${SRCDIR}/sdkconfig"
  mkdir -p "${BUILDDIR}"
  PATH="${PATH}:${IDF_TOOLCHAIN}/bin" cmake \
   -DWIFI_CONFIG=ON -B"${BUILDDIR}" -H"${SRCDIR}"
  PATH="${PATH}:${IDF_TOOLCHAIN}/bin" make -C "${BUILDDIR}"
  exit 0
fi

if [ "${IDF_TGT}" = "flash" ]
then
  ESPPORT=/dev/ttyUSB0 make -C "${BUILDDIR}" flash
else
  cd "${SRCDIR}"
  PATH="${PATH}:${IDF_TOOLCHAIN}/bin" python ${IDF_PATH}/tools/idf.py ${IDF_TGT}
fi
