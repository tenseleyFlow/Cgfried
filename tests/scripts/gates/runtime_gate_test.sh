#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
gate=$root/scripts/runtime_gate.sh
fixtures=$root/tests/scripts/gates/fixtures/runtime
scratch=${TMPDIR:-/tmp}/cgf-runtime-gate-test.$$
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
mkdir -p "$scratch"

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
        -e 's/^load1=0.10$/load1=unknown/' \
        -e 's/x86_64-linux-gnu/arm64-macos/g' \
        "$fixtures/$source.txt" >"$scratch/$source-unavailable.txt"
done
expect 0 'pass (1 comparisons, state=blocking)' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-unavailable.txt" \
    "$scratch/boundary-1-unavailable.txt" \
    "$scratch/boundary-2-unavailable.txt" \
    "$scratch/boundary-3-unavailable.txt"

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
expect 3 'expected key=value' \
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
expect 3 'non-nomad runtime requires governor=performance (got powersave)' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/mismatched-governor.txt" "$fixtures/boundary-2.txt" \
    "$fixtures/boundary-3.txt"
for source in baseline regress-1 regress-2 regress-3; do
    sed 's/^governor=performance$/governor=powersave/' \
        "$fixtures/$source.txt" >"$scratch/$source-powersave.txt"
done
expect 3 'non-nomad runtime requires governor=performance (got powersave)' \
    "$gate" "$fixtures/blocking.conf" "$scratch/baseline-powersave.txt" \
    "$scratch/regress-1-powersave.txt" "$scratch/regress-2-powersave.txt" \
    "$scratch/regress-3-powersave.txt"
sed 's/^load1=0.10$/load1=0.51/' "$fixtures/regress-1.txt" \
    >"$scratch/high-load.txt"
expect 3 'load1 exceeds controlled limit 0.5 (got 0.51)' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/high-load.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
sed '/^load1=/d' "$fixtures/regress-1.txt" >"$scratch/missing-load.txt"
expect 3 'missing load1' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$scratch/missing-load.txt" "$fixtures/regress-2.txt" \
    "$fixtures/regress-3.txt"
expect 3 'duplicate UTC run day 2026-07-08' \
    "$gate" "$fixtures/blocking.conf" "$fixtures/baseline.txt" \
    "$fixtures/regress-1.txt" "$fixtures/same-day.txt" \
    "$fixtures/regress-3.txt"

expect 0 'stage1-self-time: inactive (deferred until Sprint 58)' \
    "$gate" "$root/ci/gates.d/stage1-self-time.conf"
expect 0 'musl-full-build: inactive (deferred until Sprint 57)' \
    "$gate" "$root/ci/gates.d/musl-full-build.conf"
expect 1 '13 consecutive quiet days; 14 required' \
    "$gate" --promote "$fixtures/trial.conf" "$fixtures/promotion-13.txt"
expect 0 'promotion eligible (14 consecutive quiet days through 2026-06-14)' \
    "$gate" --promote "$fixtures/trial.conf" "$fixtures/promotion-14.txt"

echo "runtime_gate_test: $tests tests passed"
