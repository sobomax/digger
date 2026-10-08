#!/bin/sh

set -e

# Test the installed production package's local playback, and the separate
# instrumented package's NetSim replay and fault injection.
DIGGER_BIN=${DIGGER_BIN:-`command -v digger`}
NETSIM_DIGGER_BIN=${NETSIM_DIGGER_BIN:-/usr/lib/digger/digger-instrumented}
for binary in "${DIGGER_BIN}" "${NETSIM_DIGGER_BIN}"
do
  if [ ! -x "${binary}" ]; then
    echo "Package test executable not found: ${binary}" >&2
    exit 1
  fi
done
export DIGGER_BIN NETSIM_DIGGER_BIN
exec sh ./scripts/do-test-run.sh
