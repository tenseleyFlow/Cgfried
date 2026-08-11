#!/bin/sh
set -eu
: "${CGF_KERNEL_RUNTIME_OUTPUT:?}"
: "${FIXTURE_COMPARE_LOG:?}"
{
    echo "runtime_only=${CGF_KERNEL_RUNTIME_ONLY:-}"
    echo "host=${CGF_KERNEL_RUNTIME_HOST:-}"
    echo "targets=${CGF_KERNEL_TARGETS:-}"
} >"$FIXTURE_COMPARE_LOG"
{
    echo '# cgfried kernel runtime metrics v1'
    echo 'date=2026-08-10T12:00:00Z'
    echo "host=${CGF_KERNEL_RUNTIME_HOST:-}"
    echo "target=${CGF_KERNEL_TARGETS:-}"
    if [ -n "${FIXTURE_GOVERNOR:-}" ]; then
        echo "governor=$FIXTURE_GOVERNOR"
    elif [ "${CGF_KERNEL_TARGETS:-}" = arm64-macos ]; then
        echo 'governor=unavailable'
    else
        echo 'governor=performance'
    fi
    echo "${CGF_KERNEL_TARGETS:-}.tiny.O2.cgf.wall_ms_median=1.000000"
    echo "${CGF_KERNEL_TARGETS:-}.tiny.O2.cgf.wall_ms_mad=0.010000"
} >"$CGF_KERNEL_RUNTIME_OUTPUT"
