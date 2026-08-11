#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd)
gate=$repo/scripts/benchmark_gate.sh
fixtures=$repo/tests/bench/fixtures/gate
tmp=${TMPDIR:-/tmp}/cgf-benchmark-gate-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir "$tmp"

pass_case() {
    name=$1
    shift
    if ! "$@" >"$tmp/$name.out" 2>"$tmp/$name.err"; then
        echo "benchmark_gate_test: expected pass: $name" >&2
        sed 's/^/  /' "$tmp/$name.err" >&2
        exit 1
    fi
}

fail_case() {
    name=$1
    expected_status=$2
    expected=$3
    shift 3
    set +e
    "$@" >"$tmp/$name.out" 2>"$tmp/$name.err"
    status=$?
    set -e
    if [ "$status" -eq 0 ]; then
        echo "benchmark_gate_test: expected failure: $name" >&2
        exit 1
    fi
    if [ "$status" -ne "$expected_status" ]; then
        echo "benchmark_gate_test: $name exited $status, expected $expected_status" >&2
        sed 's/^/  /' "$tmp/$name.err" >&2
        exit 1
    fi
    if ! grep -F "$expected" "$tmp/$name.err" >/dev/null; then
        echo "benchmark_gate_test: $name lacked diagnostic: $expected" >&2
        sed 's/^/  /' "$tmp/$name.err" >&2
        exit 1
    fi
}

pass_case exact-boundaries "$gate" "$fixtures/baseline.txt" \
    "$fixtures/pass-boundaries.txt"
pass_case pass-at-29 "$gate" "$fixtures/baseline.txt" \
    "$fixtures/pass-29.txt"
fail_case fail-at-31 1 "self.wall_ms_median regressed" \
    "$gate" "$fixtures/baseline.txt" "$fixtures/fail-31.txt"
fail_case fail-cpu-at-31 1 "self.user+sys_ms_median regressed" \
    "$gate" "$fixtures/baseline.txt" "$fixtures/fail-cpu-31.txt"
fail_case fail-rss-at-21 1 "self.maxrss_kb_max regressed" \
    "$gate" "$fixtures/baseline.txt" "$fixtures/fail-rss-21.txt"
fail_case missing-metric 3 "missing required result metric self.maxrss_kb_max" \
    "$gate" "$fixtures/baseline.txt" "$fixtures/missing-rss.txt"
fail_case malformed-metric 3 "must have a non-negative numeric value" \
    "$gate" "$fixtures/baseline.txt" "$fixtures/malformed.txt"
fail_case duplicate-metric 3 "duplicate metric self.wall_ms_median" \
    "$gate" "$fixtures/baseline.txt" "$fixtures/duplicate.txt"

# Shared-CI mode must ignore even a large timing regression.
pass_case skip-time env BENCH_SKIP_TIME=1 "$gate" \
    "$fixtures/baseline.txt" "$fixtures/skip-time.txt"
# The same mode must continue enforcing the RSS limit.
fail_case skip-time-rss 1 "sqlite3.maxrss_kb_max regressed" \
    env BENCH_SKIP_TIME=1 "$gate" "$fixtures/baseline.txt" \
    "$fixtures/skip-time-rss-fail.txt"

# The state wrapper applies time and RSS policy independently.
pass_case rss-kind-ignores-time env BENCH_GATE_KIND=rss "$gate" \
    "$fixtures/baseline.txt" "$fixtures/fail-31.txt"
pass_case time-kind-ignores-rss env BENCH_GATE_KIND=time "$gate" \
    "$fixtures/baseline.txt" "$fixtures/fail-rss-21.txt"

fail_case usage 3 "usage:" "$gate"
fail_case unreadable 3 "cannot read" "$gate" "$tmp/not-found" \
    "$fixtures/pass-boundaries.txt"
fail_case bad-skip-time 3 "BENCH_SKIP_TIME must be 0 or 1" \
    env BENCH_SKIP_TIME=maybe "$gate" "$fixtures/baseline.txt" \
    "$fixtures/pass-boundaries.txt"
fail_case bad-gate-kind 3 "BENCH_GATE_KIND must be all, time, or rss" \
    env BENCH_GATE_KIND=maybe "$gate" "$fixtures/baseline.txt" \
    "$fixtures/pass-boundaries.txt"

echo "benchmark_gate_test: 16 cases passed"
