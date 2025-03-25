#!/bin/sh

set -e

cmake -DCMAKE_BUILD_TYPE=Debug -G "Unix Makefiles"
make -f Makefile clean all
mv digger debug/
cmake -DCMAKE_BUILD_TYPE=Release -G "Unix Makefiles"
make -f Makefile clean all
mv digger production/
