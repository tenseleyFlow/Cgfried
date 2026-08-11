#!/bin/sh
set -eu

if [ "${1:-}" = -dumpmachine ]; then
    echo "${FIXTURE_CGF_TARGET:?}"
    exit 0
fi

{
    echo BEGIN
    for arg do
        echo "$arg"
    done
    echo END
} >>"${FIXTURE_CGF_LOG:?}"

echo 'stat: arena.ast peak_kb=1 blocks=1 waste_pct=0' >&2
echo 'stat: arena.ir peak_kb=0 blocks=0 waste_pct=0' >&2
echo 'stat: intern lookups=1 hits=1 hit_pct=100' >&2
echo 'stat: pp includes=1 guard_skips=0 tokens=1' >&2
