#!/bin/sh

set -e

cmake -G "Unix Makefiles"
for build_type in debug production
do
  make BUILD_TYPE=${build_type} clean all
  mv digger *.o ${build_type}/
done
