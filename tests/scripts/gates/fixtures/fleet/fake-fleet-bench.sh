#!/bin/sh
set -eu
: "${CGF_FLEET_RESULT:?}"
{
    echo "host=$CGF_FLEET_HOST"
    echo "stamp=$CGF_FLEET_STAMP"
    echo 'fleet.gate=fixture'
} >"$CGF_FLEET_RESULT"
exit "${FIXTURE_BENCH_STATUS:-0}"
