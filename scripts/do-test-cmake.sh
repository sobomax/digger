#!/bin/sh

set -e

. "${0%/*}/cmake_flags.sub"

cmake ${CMAKE_FLAGS_debug} ${CMAKE_FLAGS_TEST} -G "Unix Makefiles"
make -f Makefile clean all
mv digger debug/
cmake ${CMAKE_FLAGS_release} ${CMAKE_FLAGS_TEST} -G "Unix Makefiles"
make -f Makefile clean all
mv digger production/
