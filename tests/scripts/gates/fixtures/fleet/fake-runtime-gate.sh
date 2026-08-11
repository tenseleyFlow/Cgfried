#!/bin/sh
set -eu
: "${FIXTURE_GATE_LOG:?}"
printf '%s\n' "$@" >"$FIXTURE_GATE_LOG"
state=$(sed -n 's/^[[:space:]]*state[[:space:]]*=[[:space:]]*//p' "$1")
if [ "${FIXTURE_GATE_REGRESSION:-0}" = 1 ]; then
    [ -z "${CGF_RUNTIME_GATE_RESULT_FILE:-}" ] ||
        echo trip >"$CGF_RUNTIME_GATE_RESULT_FILE"
    if [ "$state" = blocking ]; then
        echo 'runtime_gate: kernel-runtime: blocking failure (fixture)' >&2
        exit 1
    fi
    echo 'runtime_gate: kernel-runtime: trial reported regression (fixture)'
    exit 0
fi
[ -z "${CGF_RUNTIME_GATE_RESULT_FILE:-}" ] ||
    echo pass >"$CGF_RUNTIME_GATE_RESULT_FILE"
echo 'runtime_gate: kernel-runtime: pass (fixture)'
