#!/bin/sh
set -eu
: "${CGF_FLEET_RUNTIME_RESULT:?}"
{
    echo "host=$CGF_FLEET_HOST"
    echo "stamp=$CGF_FLEET_STAMP"
    [ -z "${CGF_FLEET_SYSROOT_INCLUDE:-}" ] ||
        echo "sysroot_include=$CGF_FLEET_SYSROOT_INCLUDE"
    [ -z "${CGF_FLEET_SYSROOT_CRT:-}" ] ||
        echo "sysroot_crt=$CGF_FLEET_SYSROOT_CRT"
    echo 'fleet.runtime_gate=fixture'
    echo "fleet.runtime_gate_trip=${FIXTURE_RUNTIME_TRIP:-no}"
} >"$CGF_FLEET_RUNTIME_RESULT"
exit "${FIXTURE_PERF_STATUS:-0}"
