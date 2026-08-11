#!/bin/sh
set -eu
: "${CGF_FLEET_RESULT:?}"
if [ -n "${FIXTURE_BENCH_ENV_LOG:-}" ]; then
    {
        echo "CGF_BENCH_CGF=${CGF_BENCH_CGF:-}"
        echo "CGF_KERNEL_CGF=${CGF_KERNEL_CGF:-}"
        echo "CGF_FLEET_REAL_CGF=${CGF_FLEET_REAL_CGF:-}"
        echo "CGF_FLEET_SYSROOT=${CGF_FLEET_SYSROOT:-}"
        echo "CGF_FLEET_SYSROOT_INCLUDE=${CGF_FLEET_SYSROOT_INCLUDE:-}"
        echo "CGF_FLEET_SYSROOT_CRT=${CGF_FLEET_SYSROOT_CRT:-}"
    } >"$FIXTURE_BENCH_ENV_LOG"
fi
{
    echo "host=$CGF_FLEET_HOST"
    echo "stamp=$CGF_FLEET_STAMP"
    [ -z "${CGF_FLEET_SYSROOT_INCLUDE:-}" ] ||
        echo "sysroot_include=$CGF_FLEET_SYSROOT_INCLUDE"
    [ -z "${CGF_FLEET_SYSROOT_CRT:-}" ] ||
        echo "sysroot_crt=$CGF_FLEET_SYSROOT_CRT"
    echo 'fleet.gate=fixture'
} >"$CGF_FLEET_RESULT"
exit "${FIXTURE_BENCH_STATUS:-0}"
