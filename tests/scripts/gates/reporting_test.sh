#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
fixtures=$root/tests/scripts/gates/fixtures/reporting
summary=$root/scripts/bench-summary.sh
report=$root/scripts/perf-report.sh
trend=$root/scripts/bench-trend.sh
tmp=${TMPDIR:-/tmp}/cgf-reporting-test.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "reporting_test: $*" >&2
    exit 1
}

expect_status()
{
    expected=$1
    shift
    set +e
    "$@" >"$tmp/out" 2>"$tmp/err"
    actual=$?
    set -e
    [ "$actual" -eq "$expected" ] || {
        cat "$tmp/out" "$tmp/err" >&2
        fail "expected exit $expected, got $actual: $*"
    }
}

summary_args="--target x86_64-linux-gnu --class ci"
# shellcheck disable=SC2086
GITHUB_STEP_SUMMARY=$tmp/step.md "$summary" $summary_args \
    --bench "$fixtures/bench.base.txt" "$fixtures/bench.current.txt" \
    --size "$fixtures/size.base.txt" "$fixtures/size.current.txt" \
    --static "$fixtures/static.base.txt" "$fixtures/static.current.txt" \
    >"$tmp/summary.md"
cmp "$fixtures/summary.golden.md" "$tmp/summary.md" || fail "summary golden differs"
cmp "$tmp/summary.md" "$tmp/step.md" || fail "GITHUB_STEP_SUMMARY append differs"

# Exactly-at-threshold inputs above pass; the smallest represented increase
# beyond the size threshold fails as an ordinary gate result.
# shellcheck disable=SC2086
expect_status 1 "$summary" $summary_args \
    --bench "$fixtures/bench.base.txt" "$fixtures/bench.current.txt" \
    --size "$fixtures/size.base.txt" "$fixtures/size.fail.txt" \
    --static "$fixtures/static.base.txt" "$fixtures/static.current.txt"
grep -F 'FAIL (+15%)' "$tmp/out" >/dev/null || fail "failed threshold was not reported"

# Malformed and incomplete inputs are I/O-contract failures, never a partial
# report or an accidental successful gate.
# shellcheck disable=SC2086
expect_status 3 "$summary" $summary_args \
    --bench "$fixtures/malformed.txt" "$fixtures/bench.current.txt"
# shellcheck disable=SC2086
expect_status 3 "$summary" $summary_args \
    --bench "$fixtures/bench.base.txt" "$fixtures/missing.txt"

"$report" --version 0.0.1 --output "$tmp/report-one.md" \
    --baseline "$fixtures/bench.base.txt" \
    --baseline "$fixtures/bench-hasu.base.txt" \
    --latest "$fixtures/report/bench.current.txt" \
    --latest "$fixtures/bench-hasu.current.txt" \
    --golden "$fixtures/golden.txt" \
    --dashboard "$fixtures/dashboard.md" \
    --previous "$fixtures/previous.md" >"$tmp/report-one.out"
"$report" --version 0.0.1 --output "$tmp/report-two.md" \
    --baseline "$fixtures/bench.base.txt" \
    --baseline "$fixtures/bench-hasu.base.txt" \
    --latest "$fixtures/report/bench.current.txt" \
    --latest "$fixtures/bench-hasu.current.txt" \
    --golden "$fixtures/golden.txt" \
    --dashboard "$fixtures/dashboard.md" \
    --previous "$fixtures/previous.md" >"$tmp/report-two.out"
cmp "$fixtures/report.golden.md" "$tmp/report-one.md" || fail "release report golden differs"
cmp "$tmp/report-one.md" "$tmp/report-two.md" || fail "release report is nondeterministic"
grep -F '| hasu | self.maxrss_kb_max | 800 | 840 | +5.0% | n/a |' \
    "$tmp/report-one.md" >/dev/null || fail "second fleet host was not reported"
grep -F '| latest | x86_64-linux-gnu | hasu | bench-hasu.current.txt | hasu | n/a | 2026-08-10T13:00:00Z | current-hasu | clean | runs=10,warmup=1 |' \
    "$tmp/report-one.md" >/dev/null || fail "hasu provenance was incomplete"
grep -F '<!-- perf-metric x86_64-linux-gnu kasumi self.maxrss_kb_max 1200 -->' \
    "$tmp/report-one.md" >/dev/null || fail "prior-report marker omitted host scope"
expect_status 3 "$report" --version 0.0.1 --output "$tmp/mismatch-report.md" \
    --baseline "$fixtures/bench-hasu.base.txt" --latest "$fixtures/report/bench.current.txt" \
    --golden "$fixtures/golden.txt" --dashboard "$fixtures/dashboard.md"
sed '/^timeit_protocol=/d' "$fixtures/bench-hasu.current.txt" \
    >"$tmp/missing-protocol.txt"
expect_status 3 "$report" --version 0.0.1 --output "$tmp/missing-provenance-report.md" \
    --baseline "$fixtures/bench-hasu.base.txt" --latest "$tmp/missing-protocol.txt" \
    --golden "$fixtures/golden.txt" --dashboard "$fixtures/dashboard.md"
grep -F 'baseline/latest input lacks protocol provenance' "$tmp/err" >/dev/null || \
    fail "release report accepted incomplete provenance"
sed '/^# target=/d' "$fixtures/bench-hasu.base.txt" \
    >"$tmp/baseline-x86_64-linux-gnu.hasu.txt"
expect_status 3 "$report" --version 0.0.1 --output "$tmp/missing-target-report.md" \
    --baseline "$tmp/baseline-x86_64-linux-gnu.hasu.txt" \
    --latest "$fixtures/bench-hasu.current.txt" \
    --golden "$fixtures/golden.txt" --dashboard "$fixtures/dashboard.md"
grep -F 'missing unique target provenance' "$tmp/err" >/dev/null || \
    fail "baseline filename masked missing target provenance"
expect_status 3 "$report" --version 0.0.1 --output "$tmp/bad-report.md" \
    --baseline "$fixtures/size.base.txt" --latest "$fixtures/report/bench.current.txt" \
    --golden "$fixtures/golden.txt" --dashboard "$fixtures/dashboard.md"

"$trend" --days 90 \
    "$fixtures/run-2026-05-01.txt" "$fixtures/run-2026-06-01.txt" \
    "$fixtures/run-2026-07-01.txt" "$fixtures/run-2026-08-10.txt" \
    >"$tmp/trend-one.md"
"$trend" --days 90 \
    "$fixtures/run-2026-08-10.txt" "$fixtures/run-2026-06-01.txt" \
    "$fixtures/run-2026-05-01.txt" "$fixtures/run-2026-07-01.txt" \
    >"$tmp/trend-two.md"
cmp "$fixtures/trend.golden.md" "$tmp/trend-one.md" || fail "trend golden differs"
cmp "$tmp/trend-one.md" "$tmp/trend-two.md" || fail "trend depends on input order"
grep -F '| FLAG |' "$tmp/trend-one.md" >/dev/null || fail "threshold trend was not flagged"
grep -F 'matmul-64.cgf.wall_ms_median | 100 | 111 | +11.0% | +10% (runtime; per-run gate also requires >4 MAD)' \
    "$tmp/trend-one.md" >/dev/null || fail "Cgfried runtime trend did not use the +10% threshold"
grep -F 'sieve.cgf.wall_ms_median | 100 | 110 | +10.0% | +10% (runtime; per-run gate also requires >4 MAD)' \
    "$tmp/trend-one.md" >/dev/null || fail "exact Cgfried runtime threshold did not pass"
grep -F 'matmul-64.gcc.wall_ms_median | 50 | 75 | +50.0% | report-only (gcc runtime reference)' \
    "$tmp/trend-one.md" >/dev/null || fail "GCC runtime reference was gated"
grep -F 'matmul-64.text | 100 | 106 | +6.0% | +5%' "$tmp/trend-one.md" >/dev/null ||
    fail "kernel static text did not use the +5% threshold"
grep -F 'hello.O2.text | 100 | 120 | +20.0% | report-only (size section)' \
    "$tmp/trend-one.md" >/dev/null || fail "size-section text was gated"
grep -F 'cgf.size_unstripped | 140 | 200 | +42.9% | report-only (unstripped size)' \
    "$tmp/trend-one.md" >/dev/null || fail "unstripped size was gated"
"$trend" "$fixtures/size-comment-run.txt" >"$tmp/comment-provenance-trend.md"
grep -F 'arm64-linux / nomad-1 | cgf.size | 100 | 100 | +0.0% | +15%' \
    "$tmp/comment-provenance-trend.md" >/dev/null ||
    fail "size comment-header target/host provenance was not preserved"
grep -F '# Performance trend — last 90 days' "$tmp/comment-provenance-trend.md" >/dev/null ||
    fail "size comment-header date provenance was not accepted"
expect_status 3 "$trend" "$fixtures/malformed.txt"

echo 'reporting_test: 13 summary, report, trend, determinism, and fail-closed cases passed'
