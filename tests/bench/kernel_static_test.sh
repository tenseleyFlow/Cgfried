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
    expected=$2
    shift 2
    if "$@" >"$tmp/$case_name.out" 2>"$tmp/$case_name.err"; then
        echo "kernel_static_test: expected failure: $case_name" >&2
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
fail_case floor-regression "floor.icount regressed" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-floor.txt"
fail_case percent-regression "percent.icount regressed" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/fail-percent.txt"
fail_case missing "missing result metric floor.text" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/missing.txt"
fail_case extra "unexpected result metric extra.icount" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/extra.txt"
fail_case duplicate "duplicate metric floor.icount" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/duplicate.txt"
fail_case malformed "must be a non-negative integer" \
    "$gate" --gate "$fixtures/baseline.txt" "$fixtures/malformed.txt"
fail_case incomplete-baseline "baseline kernel floor lacks .padding" \
    "$gate" --gate "$fixtures/incomplete-baseline.txt" \
    "$fixtures/incomplete-result.txt"

echo "kernel_static_test: 9 fixture cases passed"
