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
    run_power_profile=${8:-}
    run_scaling_driver=${9:-}
    run_epp=${10:-}
    run_control_protocol=${11:-}
    run_logical_cpus=${12:-}
    run_cpu_idle_pct=${13:-}
    run_artifact_host=${14:-}
    run_compare_status=${15:-0}
    FIXTURE_HOST=$run_host FIXTURE_SYSTEM=$run_system \
    FIXTURE_MACHINE=$run_machine \
    FIXTURE_COMPARE_LOG=$tmp/logs/compare \
    FIXTURE_GATE_LOG=$tmp/logs/gate \
    FIXTURE_GATE_REGRESSION=$run_regression \
    FIXTURE_GOVERNOR=$run_governor \
    FIXTURE_LOAD1=$run_load1 \
    FIXTURE_POWER_PROFILE=$run_power_profile \
    FIXTURE_SCALING_DRIVER=$run_scaling_driver \
    FIXTURE_EPP=$run_epp \
    FIXTURE_CONTROL_PROTOCOL=$run_control_protocol \
    FIXTURE_LOGICAL_CPUS=$run_logical_cpus \
    FIXTURE_CPU_IDLE_PCT=$run_cpu_idle_pct \
    FIXTURE_ARTIFACT_HOST=$run_artifact_host \
    FIXTURE_COMPARE_STATUS=$run_compare_status \
    CGF_FLEET_ROOT=$tmp/root \
    CGF_FLEET_HOST=$run_host \
    CGF_FLEET_HOSTNAME_CMD=$fixtures/fake-hostname.sh \
    CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
    CGF_FLEET_DATE_CMD=$fixtures/fake-date.sh \
    CGF_FLEET_KERNEL_COMPARE=$fixtures/fake-kernel-compare.sh \
    CGF_FLEET_RUNTIME_GATE=$fixtures/fake-runtime-gate.sh \
    CGF_FLEET_RUNTIME_CONFIG=$fixtures/kernel-runtime.conf \
    CGF_BENCH_CONTROL=$root/scripts/bench-control.sh \
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

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-hasu-kernels.txt"
set +e
run_fleet hasu Linux x86_64 "$tmp/compare-status3.out" \
    0 '' '' '' '' '' '' '' '' '' 3
compare_status3=$?
set -e
[ "$compare_status3" -eq 3 ] ||
    fail "kernel comparison control failure did not propagate status 3"
[ ! -e "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-hasu-kernels.txt" ] ||
    fail "failed kernel comparison emitted a runtime artifact"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/nomad.out"
grep -Fx 'targets=arm64-macos' "$tmp/logs/compare" >/dev/null ||
    fail "nomad-1 did not select native arm64-macos"
grep -F 'target=arm64-macos' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "nomad-1 artifact carries the wrong target"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" "$tmp/logs/gate"
printf 'host=nomad-1\ngovernor=unavailable\nload1=unknown\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-arm64-macos.nomad-1.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/legacy-nomad.out"
grep -F 'kernel-runtime provenance-only' "$tmp/legacy-nomad.out" >/dev/null ||
    fail "historical Nomad unknown-load baseline blocked v2 migration"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "historical Nomad baseline did not remain provenance-only"
[ ! -e "$tmp/logs/gate" ] ||
    fail "runtime gate consumed the historical Nomad baseline"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
printf 'baseline\nhost=nomad-1\ngovernor=unavailable\nload1=0.10\npower_profile=unavailable\nscaling_driver=unavailable\nenergy_performance_preference=unavailable\n' >"$tmp/root/.benchmarks/baseline-kernel-runtime-arm64-macos.nomad-1.txt"
printf 'date=2026-08-09T23:00:00Z\nhost=nomad-1\ngovernor=unavailable\nload1=0.10\npower_profile=unavailable\nscaling_driver=unavailable\nenergy_performance_preference=unavailable\n' >"$tmp/root/.benchmarks/runs/2026-08-09T110000Z-nomad-1-kernels.txt"
printf 'date=2026-08-09T01:00:00Z\nhost=nomad-1\ngovernor=unavailable\nload1=0.10\npower_profile=unavailable\nscaling_driver=unavailable\nenergy_performance_preference=unavailable\n' >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-nomad-1-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/distinct-warmup.out"
grep -F 'baseline=present distinct_days=2/3; gate not run' \
    "$tmp/distinct-warmup.out" >/dev/null ||
    fail "same-day artifacts counted as distinct nightly runs"
[ ! -e "$tmp/logs/gate" ] || fail "gate ran with fewer than three distinct UTC days"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
printf 'date=2026-08-08T12:00:00Z\nhost=nomad-1\ngovernor=unavailable\nload1=0.10\npower_profile=unavailable\nscaling_driver=unavailable\nenergy_performance_preference=unavailable\n' >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-nomad-1-kernels.txt"
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
printf 'host=nomad-1\ngovernor=unavailable\nload1=0.10\n' >"$tmp/root/.benchmarks/runs/2026-08-07T120000Z-nomad-1-kernels.txt"
set +e
run_fleet nomad-1 Darwin arm64 "$tmp/malformed-date.out"
malformed_date_status=$?
set -e
[ "$malformed_date_status" -eq 3 ] || fail "missing date provenance did not fail closed"
grep -F 'expected one valid UTC date provenance' "$tmp/malformed-date.out" >/dev/null ||
    fail "missing date provenance diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-07T120000Z-nomad-1-kernels.txt" \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
printf 'date=2026-08-08T12:00:00Z\ngovernor=unavailable\nload1=0.10\n' >"$tmp/root/.benchmarks/runs/2026-08-07T120000Z-nomad-1-kernels.txt"
set +e
run_fleet nomad-1 Darwin arm64 "$tmp/duplicate-date.out"
duplicate_date_status=$?
set -e
[ "$duplicate_date_status" -eq 3 ] || fail "duplicate date provenance did not fail closed"
grep -F 'duplicate artifact timestamp 2026-08-08T12:00:00Z' \
    "$tmp/duplicate-date.out" >/dev/null ||
    fail "duplicate date provenance diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'host=kasumi\ngovernor=powersave\nload1=0.10\n' >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\nhost=kasumi\ngovernor=powersave\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\nhost=kasumi\ngovernor=powersave\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/provenance-only.out" 0 powersave
grep -F 'controlled-load/performance-power evidence and rebaseline required; gate not run' \
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
printf 'date=2026-08-08T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/missing-governor.out" 0 performance
missing_governor_status=$?
set -e
[ "$missing_governor_status" -eq 3 ] ||
    fail "missing runtime governor provenance did not fail closed"
grep -F 'input has no unique governor provenance' \
    "$tmp/missing-governor.out" >/dev/null ||
    fail "missing runtime governor diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
printf 'host=kasumi\ngovernor=performance\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/duplicate-governor.out" 0 performance
duplicate_governor_status=$?
set -e
[ "$duplicate_governor_status" -eq 3 ] ||
    fail "duplicate runtime governor provenance did not fail closed"
grep -F 'input has no unique governor provenance' \
    "$tmp/duplicate-governor.out" >/dev/null ||
    fail "duplicate runtime governor diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'host=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/high-load.out" 0 performance 3.61
grep -F 'controlled-load/performance-power evidence and rebaseline required; gate not run' \
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
grep -F 'invalid load1 provenance' "$tmp/malformed-load.out" >/dev/null ||
    fail "malformed Linux load diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
printf 'host=kasumi\ngovernor=performance\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/missing-load.out" 0 performance 0.10
missing_load_status=$?
set -e
[ "$missing_load_status" -eq 3 ] ||
    fail "missing runtime load provenance did not fail closed"
grep -F 'input has no unique load1 provenance' "$tmp/missing-load.out" >/dev/null ||
    fail "missing runtime load diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
printf 'host=kasumi\ngovernor=performance\nload1=0.10\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/duplicate-load.out" 0 performance 0.10
duplicate_load_status=$?
set -e
[ "$duplicate_load_status" -eq 3 ] ||
    fail "duplicate runtime load provenance did not fail closed"
grep -F 'input has no unique load1 provenance' "$tmp/duplicate-load.out" >/dev/null ||
    fail "duplicate runtime load diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'host=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/legacy-migration.out"
grep -F 'controlled-load/performance-power evidence and rebaseline required; gate not run' \
    "$tmp/legacy-migration.out" >/dev/null ||
    fail "legacy runtime artifacts were not classified as provenance-only"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "legacy migration lacks its provenance-only state"
[ ! -e "$tmp/logs/gate" ] || fail "gate ran during legacy provenance migration"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'host=kasumi\ngovernor=performance\nload1=0.10\npower_profile=performance\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\npower_profile=performance\nscaling_driver=acpi-cpufreq\nenergy_performance_preference=performance\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.10\npower_profile=performance\nscaling_driver=acpi-cpufreq\nenergy_performance_preference=performance\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/partial-legacy.out"
partial_legacy_status=$?
set -e
[ "$partial_legacy_status" -eq 3 ] ||
    fail "partially populated legacy controls did not fail closed"
grep -F 'partial power-control provenance' \
    "$tmp/partial-legacy.out" >/dev/null ||
    fail "partial legacy control diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt "$tmp/logs/gate"
printf 'host=kasumi\ngovernor=performance\nload1=0.10\npower_profile=performance\nscaling_driver=intel_pstate\nenergy_performance_preference=performance\n' \
    >"$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
printf 'date=2026-08-08T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.20\npower_profile=performance\nscaling_driver=intel_pstate\nenergy_performance_preference=performance\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-08T120000Z-kasumi-kernels.txt"
printf 'date=2026-08-09T12:00:00Z\nhost=kasumi\ngovernor=performance\nload1=0.30\npower_profile=performance\nscaling_driver=intel_pstate\nenergy_performance_preference=performance\n' \
    >"$tmp/root/.benchmarks/runs/2026-08-09T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/control-mismatch.out"
grep -F 'controlled-load/performance-power evidence and rebaseline required; gate not run' \
    "$tmp/control-mismatch.out" >/dev/null ||
    fail "fully populated control mismatch was not provenance-only"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "control mismatch lacks its provenance-only state"
[ ! -e "$tmp/logs/gate" ] || fail "control mismatch unexpectedly reached the direct gate"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt \
    "$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/current-missing-power.out" \
    0 performance 0.10 __missing__ acpi-cpufreq performance
current_missing_status=$?
set -e
[ "$current_missing_status" -eq 3 ] ||
    fail "current artifact missing power_profile did not fail closed"
grep -F 'partial power-control provenance' \
    "$tmp/current-missing-power.out" >/dev/null ||
    fail "current missing power_profile diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/current-duplicate-driver.out" \
    0 performance 0.10 performance __duplicate__ performance
current_duplicate_status=$?
set -e
[ "$current_duplicate_status" -eq 3 ] ||
    fail "current artifact duplicate scaling_driver did not fail closed"
grep -F 'invalid or non-unique scaling_driver provenance' \
    "$tmp/current-duplicate-driver.out" >/dev/null ||
    fail "current duplicate scaling_driver diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/current-malformed-epp.out" \
    0 performance 0.10 performance acpi-cpufreq bad/value
current_malformed_status=$?
set -e
[ "$current_malformed_status" -eq 3 ] ||
    fail "current artifact malformed EPP did not fail closed"
grep -F 'invalid or non-unique energy_performance_preference provenance' \
    "$tmp/current-malformed-epp.out" >/dev/null ||
    fail "current malformed EPP diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt \
    "$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt" \
    "$tmp/logs/gate"
set +e
run_fleet kasumi Linux x86_64 "$tmp/no-baseline-invalid-load.out" \
    0 performance unknown performance acpi-cpufreq performance
no_baseline_invalid_load_status=$?
set -e
[ "$no_baseline_invalid_load_status" -eq 3 ] ||
    fail "invalid current Linux load reached no-baseline warmup"
grep -F 'invalid load1 provenance' \
    "$tmp/no-baseline-invalid-load.out" >/dev/null ||
    fail "no-baseline invalid Linux load diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/no-baseline-uncontrolled.out" \
    0 powersave 0.10 performance acpi-cpufreq performance
grep -F 'kernel-runtime provenance-only' "$tmp/no-baseline-uncontrolled.out" >/dev/null ||
    fail "uncontrolled current Linux state reached no-baseline warmup"
grep -Fx 'fleet.runtime_gate_trip=no' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "uncontrolled no-baseline Linux state was recorded as a trip"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt"
run_fleet kasumi Linux x86_64 "$tmp/no-baseline-high-load.out" \
    0 performance 3.61 performance acpi-cpufreq performance
grep -F 'kernel-runtime provenance-only' "$tmp/no-baseline-high-load.out" >/dev/null ||
    fail "high-load current Linux state reached no-baseline warmup"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt" >/dev/null ||
    fail "high-load no-baseline Linux state lacks provenance-only state"
[ ! -e "$tmp/logs/gate" ] || fail "no-baseline Linux control test invoked the gate"

rm -f "$tmp/root/.benchmarks/runs/"*-nomad-1-kernels.txt \
    "$tmp/root/.benchmarks/baseline-kernel-runtime-arm64-macos.nomad-1.txt"
set +e
run_fleet nomad-1 Darwin arm64 "$tmp/no-baseline-invalid-nomad.out" \
    0 unavailable 0.10 performance unavailable unavailable
no_baseline_invalid_nomad_status=$?
set -e
[ "$no_baseline_invalid_nomad_status" -eq 3 ] ||
    fail "invalid current Nomad controls reached no-baseline warmup"
grep -F 'nomad-1 power-control provenance must be unavailable' \
    "$tmp/no-baseline-invalid-nomad.out" >/dev/null ||
    fail "no-baseline invalid Nomad control diagnostic is absent"

rm -f "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt"
run_fleet nomad-1 Darwin arm64 "$tmp/no-baseline-high-load-nomad.out" \
    0 unavailable 2.01 unavailable unavailable unavailable
grep -F 'kernel-runtime provenance-only' \
    "$tmp/no-baseline-high-load-nomad.out" >/dev/null ||
    fail "high-load current Nomad state reached no-baseline warmup"
grep -Fx 'fleet.runtime_gate=provenance-only' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "high-load no-baseline Nomad state lacks provenance-only state"
grep -Fx 'fleet.runtime_gate_trip=no' \
    "$tmp/root/.benchmarks/runs/2026-08-10T120000Z-nomad-1-kernels.txt" >/dev/null ||
    fail "high-load no-baseline Nomad state was recorded as a trip"

rm -f "$tmp/root/.benchmarks/runs/"*-kasumi-kernels.txt \
    "$tmp/root/.benchmarks/baseline-kernel-runtime-x86_64-linux-gnu.kasumi.txt"
set +e
run_fleet kasumi Linux x86_64 "$tmp/current-wrong-host.out" \
    0 performance 0.10 performance acpi-cpufreq performance \
    fleet-control-v2 18 90 hasu
current_wrong_host_status=$?
set -e
[ "$current_wrong_host_status" -eq 3 ] ||
    fail "wrong current artifact host did not fail closed"
grep -F 'expected unique host=kasumi provenance' \
    "$tmp/current-wrong-host.out" >/dev/null ||
    fail "wrong current artifact host diagnostic is absent"

set +e
run_fleet kasumi Darwin arm64 "$tmp/mismatch.out"
mismatch_status=$?
set -e
[ "$mismatch_status" -eq 3 ] || fail "topology mismatch did not fail with status 3"
grep -F 'topology mismatch' "$tmp/mismatch.out" >/dev/null ||
    fail "topology mismatch diagnostic is missing"

echo 'fleet_perf_test: topology, runtime-only routing, trial warmup, gate invocation, and baseline immutability passed'
