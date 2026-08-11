#!/bin/sh
set -eu

repo=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
gate=$repo/scripts/perf_gate.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-perf-gate-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

write_config()
{
    state=$1
    {
        echo 'name=fixture-gate'
        echo "state=$state"
        echo 'where=fixture'
        echo 'when=fixture'
        echo 'threshold=>1%'
        echo 'rationale=Fixture state-machine coverage'
        echo 'owner_sprint=Sprint 54'
        [ "$state" != inactive ] || echo 'defer_until=Sprint 99'
    } >"$tmp/$state.conf"
}

for state in blocking trial report-only inactive; do
    write_config "$state"
done

"$gate" "$tmp/blocking.conf" -- sh -c 'exit 0' >"$tmp/pass.out"
grep -F 'fixture-gate PASS (blocking; >1%)' "$tmp/pass.out" >/dev/null

if "$gate" "$tmp/blocking.conf" -- sh -c 'exit 1' >"$tmp/block.out"; then
    echo 'perf_gate_test: blocking regression unexpectedly passed' >&2
    exit 1
fi
grep -F 'fixture-gate FAIL (blocking; >1%)' "$tmp/block.out" >/dev/null

"$gate" "$tmp/trial.conf" -- sh -c 'exit 1' >"$tmp/trial.out"
grep -F 'fixture-gate TRIP-RECORDED (trial; >1%)' "$tmp/trial.out" >/dev/null

"$gate" "$tmp/report-only.conf" -- sh -c 'exit 1' >"$tmp/report.out"
grep -F 'fixture-gate TRIP-RECORDED (report-only; >1%)' "$tmp/report.out" >/dev/null

touch "$tmp/should-not-run"
"$gate" "$tmp/inactive.conf" >"$tmp/inactive.out"
grep -F 'fixture-gate INACTIVE until Sprint 99' "$tmp/inactive.out" >/dev/null

status=0
"$gate" "$tmp/trial.conf" -- sh -c 'exit 3' >"$tmp/error.out" || status=$?
[ "$status" -eq 3 ] || {
    echo "perf_gate_test: trial hid harness status $status" >&2
    exit 1
}
status=0
"$gate" "$tmp/trial.conf" -- sh -c 'exit 2' >"$tmp/error-two.out" || status=$?
[ "$status" -eq 3 ] || {
    echo "perf_gate_test: child status 2 was not normalized to infrastructure status 3" >&2
    exit 1
}
grep -F 'ERROR status=2' "$tmp/error-two.out" >/dev/null

summary=$tmp/summary.md
GITHUB_STEP_SUMMARY=$summary "$gate" "$tmp/trial.conf" -- sh -c 'exit 1' \
    >"$tmp/summary.out"
grep -F 'fixture-gate TRIP-RECORDED' "$summary" >/dev/null

override_message=$repo/tests/scripts/gates/fixtures/policy/message-override.txt
override_diff=$repo/tests/scripts/gates/fixtures/policy/diff-src.txt
PERF_COMMIT_MESSAGE_FILE=$override_message PERF_POLICY_DIFF_FILE=$override_diff \
GITHUB_STEP_SUMMARY=$summary "$gate" "$tmp/blocking.conf" -- sh -c 'exit 1' \
    >"$tmp/override.out"
grep -F 'fixture-gate OVERRIDDEN' "$tmp/override.out" >/dev/null
grep -F 'perf override: #417' "$summary" >/dev/null

printf 'name=broken\nstate=trial\n' >"$tmp/malformed.conf"
status=0
"$gate" "$tmp/malformed.conf" -- true >"$tmp/malformed.out" \
    2>"$tmp/malformed.err" || status=$?
[ "$status" -eq 3 ] || {
    echo "perf_gate_test: malformed config returned $status, expected 3" >&2
    exit 1
}

echo 'perf_gate_test: 10 fixture cases passed'
