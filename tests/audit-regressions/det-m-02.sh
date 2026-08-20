#!/bin/sh
# XFAIL(audit): DET-M-02 stage1 timing derives MAD from a single sample
# Reproducer contract: 0 = baseline defect reproduced, 1 = remediated/XPASS,
# 2 = malformed checkout or unavailable required tool.
set -u
LC_ALL=C
export LC_ALL

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=${1:-$(CDPATH= cd -- "$SCRIPT_DIR/../.." && pwd)}
BOOTSTRAP="$ROOT/scripts/bootstrap.sh"

[ -r "$BOOTSTRAP" ] || exit 2

# The production timing command deliberately asks timeit for one measured
# sample. A one-point distribution has MAD zero by construction.
if ! grep -Eq '"\$stage0/timeit" -n 1 -w 0 ' "$BOOTSTRAP"; then
    exit 1
fi

receipts=0
for receipt in "$ROOT"/.benchmarks/runs/*-bootstrap.txt; do
    [ -e "$receipt" ] || continue
    grep -Eq '^stage1[.]O2[.]wall_ms_median=[0-9]' "$receipt" || exit 2
    mad=$(sed -n 's/^stage1[.]O2[.]wall_ms_mad=//p' "$receipt")
    [ -n "$mad" ] || exit 2
    receipts=$((receipts + 1))
    case $mad in
    0 | 0.0 | 0.00 | 0.000 | 0.0000 | 0.00000 | 0.000000) ;;
    *) exit 2 ;;
    esac
done

[ "$receipts" -gt 0 ] || exit 2

echo "DET-M-02 reproduced: all $receipts stage1 receipts report zero MAD from the production -n 1 timing command"
exit 0
