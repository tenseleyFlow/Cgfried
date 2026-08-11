#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
fleet=${1:-$root/scripts/fleet-perf.sh}
fixtures=$root/tests/scripts/gates/fixtures/fleet
tmp=${TMPDIR:-/tmp}/cgf-fleet-perf-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/root/.benchmarks/runs" "$tmp/logs"

fail()
{
    echo "fleet_perf_test: $*" >&2
    exit 1
}

run_fleet()
{
    run_host=$1
    run_system=$2
    run_machine=$3
    run_out=$4
    run_regression=${5:-0}
    run_governor=${6:-}
    run_load1=${7:-}
    FIXTURE_HOST=$run_host FIXTURE_SYSTEM=$run_system \
    FIXTURE_MACHINE=$run_machine \
    FIXTURE_COMPARE_LOG=$tmp/logs/compare \
    FIXTURE_GATE_LOG=$tmp/logs/gate \
    FIXTURE_GATE_REGRESSION=$run_regression \
    FIXTURE_GOVERNOR=$run_governor \
    FIXTURE_LOAD1=$run_load1 \
    CGF_FLEET_ROOT=$tmp/root \
    CGF_FLEET_HOST=$run_host \
    CGF_FLEET_HOSTNAME_CMD=$fixtures/fake-hostname.sh \
    CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
    CGF_FLEET_DATE_CMD=$fixtures/fake-date.sh \
    CGF_FLEET_KERNEL_COMPARE=$fixtures/fake-kernel-compare.sh \
    CGF_FLEET_RUNTIME_GATE=$fixtures/fake-runtime-gate.sh \
    CGF_FLEET_RUNTIME_CONFIG=$fixtures/kernel-runtime.conf \
        "$fleet" >"$run_out" 2>&1
}

run_fleet kasumi Linux x86_64 "$tmp/kasumi.out"
grep -F 'trial warmup: host=kasumi target=x86_64-linux-gnu baseline=missing distinct_days=1/3; gate not run' \
    "$tmp/kasumi.out" >/dev/null || fail "missing explicit first-run warmup report"
grep -Fx 'runtime_only=1' "$tmp/logs/compare" >/dev/null ||
    fail "fleet wrapper did not request runtime-only measurement"
grep -Fx 'targets=x86_64-linux-gnu' "$tmp/logs/compare" >/dev/null ||
    fail "kasumi did not select x86_64-linux-gnu"
[ -s "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" ] ||
    fail "kasumi dated artifact is missing"
[ ! -e "$tmp/logs/gate" ] || fail "gate ran without a baseline and three runs"

run_fleet hasu Linux x86_64 "$tmp/hasu.out"
grep -Fx 'targets=x86_64-linux-gnu' "$tmp/logs/compare" >/dev/null ||
    fail "hasu did not select x86_64-linux-gnu"
[ -s "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-hasu-kernels.txt" ] ||
    fail "hasu dated artifact is missing"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/nomad.out"
grep -Fx 'targets=arm64-macos' "$tmp/logs/compare" >/dev/null ||
    fail "nomad-1 did not select native arm64-macos"
grep -F 'target=arm64-macos' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "nomad-1 artifact carries the wrong target"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" "$tmp/logs/gate"
printf 'baseline\ngovernor=unavailable\nload1=unknown\n' >"$tmp/root/.benchmarks/baseline-kernel-runtime-arm64-macos.nomad-1.txt"
printf 'date=2026-08-09T23:00:00Z\ngovernor=unavailable\nload1=unknown\n' >"$tmp/root/.benchmarks/runs/2026-08-09T110000Z-nomad-1-kernels.txt"
printf 'date=2026-08-09T01:00:00Z\ngovernor=unavailable\nload1=unknown\n' >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-nomad-1-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/distinct-warmup.out"
grep -F 'baseline=present distinct_days=2/3; gate not run' \
    "$tmp/distinct-warmup.out" >/dev/null ||
    fail "same-day artifacts counted as distinct nightly runs"
[ ! -e "$tmp/logs/gate" ] || fail "gate ran with fewer than three distinct UTC days"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
printf 'date=2026-08-08T12:00:00Z\ngovernor=unavailable\nload1=unknown\n' >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-nomad-1-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/gated.out"
grep -F 'gate evaluated' "$tmp/gated.out" >/dev/null ||
    fail "gate was not invoked once baseline plus three runs existed"
[ "$(wc -l <"$tmp/logs/gate" | tr -d ' ')" -eq 5 ] ||
    fail "runtime gate did not receive config, baseline, and three runs"
sed -n '3p' "$tmp/logs/gate" | grep -F '2026-08-08T120000Z-nomad-1-kernels.txt' >/dev/null ||
    fail "oldest of the latest three runs is wrong"
sed -n '4p' "$tmp/logs/gate" | grep -F '2026-08-09T110000Z-nomad-1-kernels.txt' >/dev/null ||
    fail "latest provenance timestamp from the middle UTC day was not selected"
if grep -F '2026-08-09T120000Z-nomad-1-kernels.txt' "$tmp/logs/gate" >/dev/null; then
    fail "older artifact from the same UTC day reached the gate"
fi
sed -n '5p' "$tmp/logs/gate" | grep -F '2026-08-10T120000Z-nomad-1-kernels.txt' >/dev/null ||
    fail "new dated run was not passed to the gate"
grep -Fx 'baseline' "$tmp/root/.benchmarks/baseline-kernel-runtime-arm64-macos.nomad-1.txt" >/dev/null ||
    fail "fleet wrapper mutated the baseline"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/trial-trip.out" 1
grep -F 'fleet.runtime_gate=trial-trip' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "trial regression was not recorded as a trial trip"
grep -F 'fleet.runtime_gate_trip=yes' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "trial regression lacks its stable trip channel"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
printf 'host=nomad-1\ngovernor=unavailable\nload1=unknown\n' >"$tmp/root/.benchmarks/runs/2026-08-07T120000Z-nomad-1-kernels.txt"
set +e
run_fleet nomad-1 Darwin arm64 "$tmp/malformed-date.out"
malformed_date_status=$?
set -e
[ "$malformed_date_status" -eq 3 ] || fail "missing date provenance did not fail closed"
grep -F 'expected one valid UTC date provenance' "$tmp/malformed-date.out" >/dev/null ||
    fail "missing date provenance diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-07T120000Z-nomad-1-kernels.txt" \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
printf 'date=2026-08-08T12:00:00Z\ngovernor=unavailable\nload1=unknown\n' >"$tmp/root/.benchmarks/runs/2026-08-07T120000Z-nomad-1-kernels.txt"
set +e
run_fleet nomad-1 Darwin arm64 "$tmp/duplicate-date.out"
duplicate_date_status=$?
set -e
[ "$duplicate_date_status" -eq 3 ] || fail "duplicate date provenance did not fail closed"
grep -F 'duplicate artifact timestamp 2026-08-08T12:00:00Z' \
    "$tmp/duplicate-date.out" >/dev/null ||
    fail "duplicate date provenance diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'governor=powersave\nload1=0.10\n' >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\ngovernor=powersave\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\ngovernor=powersave\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/provenance-only.out" 0 powersave
grep -F 'controlled-load/performance-governor evidence and rebaseline required; gate not run' \
    "$tmp/provenance-only.out" >/dev/null ||
    fail "non-comparable Linux history lacks an explicit rebaseline report"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "non-comparable Linux history lacks its provenance-only state"
grep -Fx 'fleet.runtime_gate_trip=no' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "provenance-only Linux history was recorded as a trip"
[ ! -e "$tmp/logs/gate" ] || fail "gate ran on non-comparable Linux history"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'host=kasumi\nload1=0.10\n' >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/missing-governor.out" 0 performance
missing_governor_status=$?
set -e
[ "$missing_governor_status" -eq 3 ] ||
    fail "missing runtime governor provenance did not fail closed"
grep -F 'expected exactly one governor provenance field' \
    "$tmp/missing-governor.out" >/dev/null ||
    fail "missing runtime governor diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
printf 'governor=performance\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/duplicate-governor.out" 0 performance
duplicate_governor_status=$?
set -e
[ "$duplicate_governor_status" -eq 3 ] ||
    fail "duplicate runtime governor provenance did not fail closed"
grep -F 'expected exactly one governor provenance field' \
    "$tmp/duplicate-governor.out" >/dev/null ||
    fail "duplicate runtime governor diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'governor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/high-load.out" 0 performance 0.75
grep -F 'controlled-load/performance-governor evidence and rebaseline required; gate not run' \
    "$tmp/high-load.out" >/dev/null ||
    fail "high-load Linux history was not classified as provenance-only"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "high-load Linux history lacks its provenance-only state"
[ ! -e "$tmp/logs/gate" ] || fail "gate ran on high-load Linux evidence"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/malformed-load.out" 0 performance unknown
malformed_load_status=$?
set -e
[ "$malformed_load_status" -eq 3 ] ||
    fail "malformed Linux load provenance did not fail closed"
grep -F 'Linux load1 provenance must be numeric' "$tmp/malformed-load.out" >/dev/null ||
    fail "malformed Linux load diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
printf 'governor=performance\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/missing-load.out" 0 performance 0.10
missing_load_status=$?
set -e
[ "$missing_load_status" -eq 3 ] ||
    fail "missing runtime load provenance did not fail closed"
grep -F 'expected exactly one load1 provenance field' "$tmp/missing-load.out" >/dev/null ||
    fail "missing runtime load diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
printf 'governor=performance\nload1=0.10\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/duplicate-load.out" 0 performance 0.10
duplicate_load_status=$?
set -e
[ "$duplicate_load_status" -eq 3 ] ||
    fail "duplicate runtime load provenance did not fail closed"
grep -F 'expected exactly one load1 provenance field' "$tmp/duplicate-load.out" >/dev/null ||
    fail "duplicate runtime load diagnostic is absent"

set +e
run_fleet kasumi Darwin arm64 "$tmp/mismatch.out"
mismatch_status=$?
set -e
[ "$mismatch_status" -eq 3 ] || fail "topology mismatch did not fail with status 3"
grep -F 'topology mismatch' "$tmp/mismatch.out" >/dev/null ||
    fail "topology mismatch diagnostic is missing"

echo 'fleet_perf_test: topology, runtime-only routing, trial warmup, gate invocation, and baseline immutability passed'
