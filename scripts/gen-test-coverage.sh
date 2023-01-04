#!/bin/sh

set -e

if [ "${CC}" = "clang" ]
then
  echo llvm-cov gcov "${@}" >&2
  exec llvm-cov gcov "${@}"
fi
echo gcov "${@}" >&2
exec gcov "${@}"

