#!/bin/sh
# Compare two controlled Sprint 58 O2 stage1 self-compile timing receipts.
set -eu
LC_ALL=C
export LC_ALL

usage()
{
    echo 'usage: bootstrap-time-gate.sh BASELINE RESULT' >&2
    exit 3
}

die()
{
    echo "bootstrap-time-gate: $*" >&2
    exit 3
}

[ "$#" -eq 2 ] || usage
baseline=$1
result=$2
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
control=${CGF_BOOTSTRAP_BENCH_CONTROL:-$root/scripts/bench-control.sh}
[ -r "$result" ] || die "cannot read $result"
[ -x "$control" ] || die "control helper is not executable: $control"
[ ! -e "$baseline" ] || [ -r "$baseline" ] ||
    die "cannot read existing baseline $baseline"

baseline_present=0
if [ -r "$baseline" ]; then
    baseline_present=1
fi

require_controlled()
{
    receipt=$1
    control_status=0
    control_state=$($control classify --require-v2 "$receipt") ||
        control_status=$?
    case $control_status:$control_state in
    0:controlled) ;;
    1:provenance-only)
        die "timing evidence is not controlled: $receipt"
        ;;
    3:*) exit 3 ;;
    *) die "control helper failed with status $control_status" ;;
    esac
}

require_controlled "$result"
if [ "$baseline_present" -eq 1 ]; then
    require_controlled "$baseline"
fi

set +e
awk -v baseline_file="$baseline" -v have_baseline="$baseline_present" \
    -f "$root/scripts/bootstrap-time-gate.awk" "$result"
status=$?
set -e
case $status in
0 | 1 | 3) exit "$status" ;;
*) die "metric parser failed with status $status" ;;
esac
