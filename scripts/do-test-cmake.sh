#!/bin/sh

set -e

cmake -DCMAKE_BUILD_TYPE=Debug -DDIGGER_INSTRUMENTATION=ON -G "Unix Makefiles"
make -f Makefile clean all
mv digger debug/
cmake -DCMAKE_BUILD_TYPE=Release -DDIGGER_INSTRUMENTATION=ON -G "Unix Makefiles"
make -f Makefile clean all
mv digger production/
