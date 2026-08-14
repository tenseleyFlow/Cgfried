#!/bin/sh
set -eu

root=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
cgf=${1:-$root/build/cgfried}
header=$root/ci/campaigns/compat/arm64-linux-u128-storage.h
layout=$root/tests/campaigns/arm64_u128_storage_layout.c
arithmetic=$root/tests/campaigns/arm64_u128_storage_arithmetic.c
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-arm64-compat-test.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail() {
    echo "campaign-arm64-compat-meta: $*" >&2
    exit 1
}

[ -x "$cgf" ] || fail "compiler is not executable: $cgf"
[ -r "$header" ] || fail "compatibility header is unreadable: $header"

"$cgf" --target=arm64-linux -std=gnu11 -include "$header" \
    -fsyntax-only "$layout"

if "$cgf" --target=arm64-linux -std=gnu11 -include "$header" \
    -fsyntax-only "$arithmetic" >"$tmp/arithmetic.out" \
    2>"$tmp/arithmetic.err"; then
    fail "opaque storage unexpectedly accepted integer arithmetic"
fi
grep -F 'invalid operands' "$tmp/arithmetic.err" >/dev/null ||
    fail "arithmetic rejection did not identify invalid operands"

if "$cgf" --target=x86_64-linux-gnu -std=gnu11 -include "$header" \
    -fsyntax-only "$layout" >"$tmp/x86.out" 2>"$tmp/x86.err"; then
    fail "ARM64 compatibility header unexpectedly accepted an x86 target"
fi
grep -F 'only for Cgfried ARM64 Linux campaigns' "$tmp/x86.err" >/dev/null ||
    fail "wrong-target rejection did not identify the compatibility boundary"

printf 'campaign-arm64-compat-meta: PASS policy=opaque-u64x2-align16-v1\n'
