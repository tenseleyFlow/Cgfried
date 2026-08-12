#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
check=$root/ci/campaigns/check-expected.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-campaign-expected-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

write_result() {
    file=$1
    shift
    {
        echo '# cgf-campaign-results-v1'
        printf '# columns=key\toutcome\tdetail\n'
        for row do printf '%s\n' "$row"; done
    } >"$file"
}

expect_fail() {
    label=$1
    pattern=$2
    shift 2
    if "$@" >"$tmp/$label.out" 2>"$tmp/$label.err"; then
        echo "campaign-expected-meta: $label unexpectedly passed" >&2
        exit 1
    fi
    grep -F "$pattern" "$tmp/$label.err" >/dev/null || {
        echo "campaign-expected-meta: $label emitted the wrong diagnostic" >&2
        cat "$tmp/$label.err" >&2
        exit 1
    }
}

pass=$(printf 'build\tPASS\tcompiler=cgfried')
skip=$(printf 'shared\tSKIP\tCAMP-DEMO-001')
fail=$(printf 'suite\tFAIL\tcase=known')
fixed=$(printf 'suite\tPASS\tcases=1')

write_result "$tmp/expected" "$pass" "$skip" "$fail"
cp "$tmp/expected" "$tmp/actual"
"$check" "$tmp/expected" "$tmp/actual" >/dev/null

write_result "$tmp/actual" "$pass" "$skip" "$fixed"
expect_fail improvement 'missing expected row:' \
    "$check" "$tmp/expected" "$tmp/actual"
grep -F 'unexpected actual row:' "$tmp/improvement.err" >/dev/null || exit 1

write_result "$tmp/actual" "$pass" "$fail"
expect_fail missing 'missing expected row:' \
    "$check" "$tmp/expected" "$tmp/actual"

extra=$(printf 'smoke\tPASS\texit=0')
write_result "$tmp/actual" "$pass" "$skip" "$extra" "$fail"
expect_fail extra 'unexpected actual row:' \
    "$check" "$tmp/expected" "$tmp/actual"

write_result "$tmp/actual" "$skip" "$pass" "$fail"
expect_fail ordering 'not in canonical byte order' \
    "$check" "$tmp/expected" "$tmp/actual"

write_result "$tmp/actual" "$pass" "$skip" "$fail" "$fail"
expect_fail duplicate 'duplicate result key: suite' \
    "$check" "$tmp/expected" "$tmp/actual"

printf 'campaign-expected-meta: PASS\n'
