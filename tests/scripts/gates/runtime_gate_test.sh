#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
gate=$root/scripts/runtime_gate.sh
source_fixtures=$root/tests/scripts/gates/fixtures/runtime
scratch=${TMPDIR:-/tmp}/cgf-runtime-gate-test.$$
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
fixtures=$scratch/fixtures
mkdir -p "$fixtures"
for fixture in "$source_fixtures"/*; do
    awk '
        { print }
        /^load1=/ {
            print "power_profile=performance"
            print "scaling_driver=acpi-cpufreq"
            print "energy_performance_preference=performance"
        }
    ' "$fixture" >"$fixtures/${fixture##*/}"
done

tests=0

expect()
{
    expected_status=$1
    expected_text=$2
    shift 2
    tests=$((tests + 1))
    set +e
    "$@" >"$scratch/out" 2>"$scratch/err"
    actual_status=$?
    set -e
    if [ "$actual_status" -ne "$expected_status" ]; then
        echo "runtime_gate_test: expected status $expected_status, got $actual_status: $*" >&2
        sed 's/^/stdout: /' "$scratch/out" >&2
        sed 's/^/stderr: /' "$scratch/err" >&2
        exit 1
    fi
    if ! grep -F "$expected_text" "$scratch/out" "$scratch/err" >/dev/null; then
        echo "runtime_gate_test: missing '$expected_text': $*" >&2
        sed 's/^/stdout: /' "$scratch/out" >&2
        sed 's/^/stderr: /' "$scratch/err" >&2
        exit 1
    fi
}

expect_result()
{
    expected_status=$1
    expected_result=$2
    shift 2
    tests=$((tests + 1))
    result=$scratch/result-$tests.txt
    set +e
    CGF_RUNTIME_GATE_RESULT_FILE=$result "$@" >"$scratch/out" 2>"$scratch/err"
    actual_status=$?
    set -e
    [ "$actual_status" -eq "$expected_status" ] || {
        echo "runtime_gate_test: result-channel status $actual_status, expected $expected_status" >&2
        exit 1
    }
    [ "$(cat "$result")" = "$expected_result" ] || {
        echo "runtime_gate_test: result channel is not '$expected_result'" >&2
        exit 1
    }
}

expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/boundary-1.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"

# Darwin records no cpufreq governor; the explicit value remains comparable
# when every member of the baseline/run set agrees.
for source in baseline boundary-1 boundary-2 boundary-3; do
    sed -e 's/^host=kasumi$/host=nomad-1/' \
        -e 's/^target=x86_64-linux-gnu$/target=arm64-macos/' \
        -e 's/^governor=performance$/governor=unavailable/' \
        -e 's/^power_profile=performance$/power_profile=unavailable/' \
        -e 's/^scaling_driver=acpi-cpufreq$/scaling_driver=unavailable/' \
        -e 's/^energy_performance_preference=performance$/energy_performance_preference=unavailable/' \
        -e 's/x86_64-linux-gnu/arm64-macos/g' \
        "$fixtures/$source.txt" >"$scratch/$source-unavailable.txt"
done
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-unavailable.txt" \
    "$scratch/boundary-1-unavailable.txt" \
    "$scratch/boundary-2-unavailable.txt" \
    "$scratch/boundary-3-unavailable.txt"

sed -e 's/^load1=.*$/load1=unknown/' \
    -e '/^power_profile=/d' -e '/^scaling_driver=/d' \
    -e '/^energy_performance_preference=/d' \
    "$scratch/baseline-unavailable.txt" \
    >"$scratch/nomad-legacy-unknown.txt"
expect 3 'has uncontrolled runtime provenance' \
    "$gate" "$fixtures/blocking.conf" \
    "$scratch/nomad-legacy-unknown.txt" \
    "$scratch/boundary-1-unavailable.txt" \
    "$scratch/boundary-2-unavailable.txt" \
    "$scratch/boundary-3-unavailable.txt"

for source in baseline boundary-1 boundary-2 boundary-3; do
    sed -e 's/^governor=performance$/governor=powersave/' \
        -e 's/^scaling_driver=acpi-cpufreq$/scaling_driver=intel_pstate/' \
        "$fixtures/$source.txt" >"$scratch/$source-intel-pstate.txt"
done
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-intel-pstate.txt" \
    "$scratch/boundary-1-intel-pstate.txt" \
    "$scratch/boundary-2-intel-pstate.txt" \
    "$scratch/boundary-3-intel-pstate.txt"

sed 's/^load1=0.10$/load1=0.25/' "$fixtures/boundary-1.txt" \
    >"$scratch/boundary-1-different-load.txt"
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/boundary-1-different-load.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"

for source in baseline boundary-1 boundary-2 boundary-3; do
    {
        sed 's/^load1=0.10$/load1=2.8/' "$fixtures/$source.txt"
        printf '%s\n' 'control_protocol=fleet-control-v2' \
            'logical_cpus=18' 'cpu_idle_pct=90'
    } >"$scratch/$source-v2.txt"
done
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-v2.txt" \
    "$scratch/boundary-1-v2.txt" "$scratch/boundary-2-v2.txt" \
    "$scratch/boundary-3-v2.txt"

for source in baseline boundary-1 boundary-2 boundary-3; do
    {
        sed 's/^load1=0.10$/load1=3.60/' "$fixtures/$source.txt"
        printf '%s\n' 'control_protocol=fleet-control-v2' \
            'logical_cpus=18' 'cpu_idle_pct=85'
    } >"$scratch/$source-v2-boundary.txt"
done
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-v2-boundary.txt" \
    "$scratch/boundary-1-v2-boundary.txt" \
    "$scratch/boundary-2-v2-boundary.txt" \
    "$scratch/boundary-3-v2-boundary.txt"

sed 's/^load1=3.60$/load1=3.61/' "$scratch/boundary-1-v2-boundary.txt" \
    >"$scratch/v2-over-load.txt"
expect 3 'has uncontrolled runtime provenance' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-v2-boundary.txt" \
    "$scratch/v2-over-load.txt" "$scratch/boundary-2-v2-boundary.txt" \
    "$scratch/boundary-3-v2-boundary.txt"
sed 's/^cpu_idle_pct=85$/cpu_idle_pct=84.99/' \
    "$scratch/boundary-1-v2-boundary.txt" >"$scratch/v2-low-idle.txt"
expect 3 'has uncontrolled runtime provenance' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-v2-boundary.txt" \
    "$scratch/v2-low-idle.txt" "$scratch/boundary-2-v2-boundary.txt" \
    "$scratch/boundary-3-v2-boundary.txt"
sed 's/^logical_cpus=18$/logical_cpus=17/' "$scratch/boundary-1-v2.txt" \
    >"$scratch/v2-cpu-mismatch.txt"
expect 3 'logical_cpus does not match other v2 evidence' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-v2.txt" \
    "$scratch/v2-cpu-mismatch.txt" "$scratch/boundary-2-v2.txt" \
    "$scratch/boundary-3-v2.txt"

# The median is 112 (>10%), but delta=12 remains inside 4*new_MAD=16.
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/noise-1.txt" "$fixtures/noise-2.txt" "$fixtures/noise-3.txt"

# The same 12 ms delta is outside 4*max(MAD)=4 and therefore blocks.
expect 1 'blocking failure (1 regression(s))' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/regress-1.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
expect 0 'trial reported 1 regression(s); non-blocking' \
    "$gate" "$root/ci/gates.d/kernel-runtime.conf" "$fixtures/baseline.txt" \
    "$fixtures/regress-1.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
expect_result 0 pass \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/boundary-1.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect_result 0 trip \
    "$gate" "$root/ci/gates.d/kernel-runtime.conf" "$fixtures/baseline.txt" \
    "$fixtures/regress-1.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
expect_result 1 trip \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/regress-1.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"

expect 3 'missing median/MAD pair' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/missing-mad.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect 3 'input has no unique host provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/malformed.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect 3 'duplicate key' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/duplicate.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect 3 'cannot read' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/not-present.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect 3 'runs does not match baseline' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/mismatched-runs.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect 3 'missing timeit_protocol' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/missing-protocol.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
expect 3 'has uncontrolled runtime provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/mismatched-governor.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
for source in baseline regress-1 regress-2 regress-3; do
    sed 's/^governor=performance$/governor=powersave/' \
        "$fixtures/$source.txt" >"$scratch/$source-powersave.txt"
done
expect 3 'has uncontrolled runtime provenance' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-powersave.txt" \
    "$scratch/regress-1-powersave.txt" "$scratch/regress-2-powersave.txt" \
    "$scratch/regress-3-powersave.txt"
sed 's/^load1=0.10$/load1=0.51/' "$fixtures/regress-1.txt" \
    >"$scratch/high-load.txt"
expect 3 'has uncontrolled runtime provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/high-load.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
sed '/^load1=/d' "$fixtures/regress-1.txt" >"$scratch/missing-load.txt"
expect 3 'input has no unique load1 provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/missing-load.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
sed '/^power_profile=/d' "$fixtures/regress-1.txt" \
    >"$scratch/missing-power-profile.txt"
expect 3 'partial power-control provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/missing-power-profile.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
sed 's/^scaling_driver=.*$/scaling_driver=intel\/pstate/' \
    "$fixtures/regress-1.txt" >"$scratch/malformed-scaling-driver.txt"
expect 3 'invalid or non-unique scaling_driver provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/malformed-scaling-driver.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
sed '/^energy_performance_preference=/p' "$fixtures/regress-1.txt" \
    >"$scratch/duplicate-epp.txt"
expect 3 'invalid or non-unique energy_performance_preference provenance' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/duplicate-epp.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
sed 's/^energy_performance_preference=performance$/energy_performance_preference=balance_performance/' \
    "$fixtures/regress-1.txt" >"$scratch/mismatched-epp.txt"
expect 3 'energy_performance_preference does not match baseline' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/mismatched-epp.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
expect 3 'duplicate UTC run day 2026-07-08' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/regress-1.txt" "$fixtures/same-day.txt" \
    "$fixtures/regress-3.txt"

expect 0 'musl-full-build: inactive (deferred until Sprint 57)' \
    "$gate" "$root/ci/gates.d/musl-full-build.conf"
expect 1 '13 consecutive quiet days; 14 required' \
    "$gate" --promote "$fixtures/trial.conf" "$fixtures/promotion-13.txt"
expect 0 'promotion eligible (14 consecutive quiet days through 2026-06-14)' \
    "$gate" --promote "$fixtures/trial.conf" "$fixtures/promotion-14.txt"

echo "runtime_gate_test: $tests tests passed"
