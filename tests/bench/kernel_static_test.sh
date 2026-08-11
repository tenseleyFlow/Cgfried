#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
gate=$repo/scripts/kernel-static.sh
fixtures=$repo/tests/bench/fixtures/kernel-gate
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-kernel-static-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

pass_case() {
    case_name=$1
    shift
    if ! "$@" >"$tmp/$case_name.out" 2>"$tmp/$case_name.err"; then
        echo "kernel_static_test: expected pass: $case_name" >&2
        sed 's/^/  /' "$tmp/$case_name.err" >&2
        exit 1
    fi
}

fail_case() {
    case_name=$1
    expected_status=$2
    expected=$3
    shift 3
    set +e
    "$@" >"$tmp/$case_name.out" 2>"$tmp/$case_name.err"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "kernel_static_test: expected failure: $case_name" >&2
        exit 1
    fi
    if [ "$status" -ne "$expected_status" ]; then
        echo "kernel_static_test: $case_name exited $status, expected $expected_status" >&2
        sed 's/^/  /' "$tmp/$case_name.err" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$tmp/$case_name.err" >/dev/null; then
        echo "kernel_static_test: $case_name lacked diagnostic: $expected" >&2
        sed 's/^/  /' "$tmp/$case_name.err" >&2
        exit 1
    fi
}

pass_case exact "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/exact.txt"
pass_case threshold-boundaries "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/pass-boundaries.txt"
fail_case floor-regression 1 "floor.icount regressed" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-floor.txt"
fail_case percent-regression 1 "percent.icount regressed" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-percent.txt"
fail_case text-regression 1 "floor.text regressed" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-text.txt"
pass_case text-does-not-affect-icount env CGF_KERNEL_GATE_KIND=icount \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-text.txt"
pass_case icount-does-not-affect-text env CGF_KERNEL_GATE_KIND=text \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-floor.txt"
fail_case missing 3 "missing result metric floor.text" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/missing.txt"
fail_case extra 3 "unexpected result metric extra.icount" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/extra.txt"
fail_case duplicate 3 "duplicate metric floor.icount" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/duplicate.txt"
fail_case malformed 3 "must be a non-negative integer" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/malformed.txt"
fail_case incomplete-baseline 3 "baseline kernel floor lacks .padding" \
    "$gate" --gate "$fixtures/incomplete-baseline.txt" \
    "$fixtures/incomplete-result.txt"

fail_case usage 3 "usage:" "$gate" --gate
fail_case unreadable 3 "cannot read" "$gate" --gate "$tmp/not-found" \
    "$fixtures/exact.txt"
fail_case bad-gate-kind 3 "CGF_KERNEL_GATE_KIND must be all, icount, or text" \
    env CGF_KERNEL_GATE_KIND=maybe "$gate" --gate "$fixtures/baseline.txt" \
    "$fixtures/exact.txt"
fail_case bad-measure-only 3 "CGF_KERNEL_MEASURE_ONLY must be 0 or 1" \
    env CGF_KERNEL_MEASURE_ONLY=maybe "$gate" --gate \
    "$fixtures/baseline.txt" "$fixtures/exact.txt"

echo "kernel_static_test: 16 cases passed"
