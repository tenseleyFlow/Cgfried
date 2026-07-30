#!/bin/sh
# PP phase closeout gate: no LANDS_IN_SPRINT marker under src/pp/ may name
# a sprint that has already shipped (3..7). Remaining seams must name
# Sprint 8+ (front end), 37 (warning flags), 52 (perf), or 55 (GNU exts).
set -eu
LC_ALL=C
export LC_ALL

hits=$(grep -rn 'LANDS_IN_SPRINT([3-7])' src/pp/ || true)
if [ -n "$hits" ]; then
    echo "check_pp_seams: stale seam marker(s) naming a shipped sprint:" >&2
    printf '%s\n' "$hits" >&2
    exit 1
fi
echo "check_pp_seams: no stale PP seams"
