#!/bin/sh
set -eu

case ${1:-} in
measure)
    [ "$#" -eq 2 ] || exit 3
    cat <<EOF
control_protocol=fleet-control-v2
logical_cpus=${FIXTURE_LOGICAL_CPUS:-8}
cpu_idle_pct=${FIXTURE_CPU_IDLE_PCT:-99.00}
load1=${FIXTURE_LOAD1:-0.10}
EOF
    ;;
classify)
    [ "${2:-}" = --require-v2 ] || exit 3
    [ "$#" -eq 3 ] || exit 3
    [ -r "$3" ] || exit 3
    [ -z "${FIXTURE_CONTROL_LOG:-}" ] || cp "$3" "$FIXTURE_CONTROL_LOG"
    exit "${FIXTURE_CONTROL_STATUS:-0}"
    ;;
*) exit 3 ;;
esac
