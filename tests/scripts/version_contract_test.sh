#!/bin/sh
# Frozen v0.1.0 driver-identification contracts. Build systems consume these
# byte-for-byte, so diagnostics, target annotations, or extra blank lines are
# regressions too.
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cgf=${CGF_TEST_CC:-$root/build/cgfried}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-version-contract.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "version-contract: $*" >&2
    exit 1
}

expect_output()
{
    label=$1
    expected=$2
    shift 2
    status=0
    "$@" >"$tmp/$label.out" 2>"$tmp/$label.err" || status=$?
    [ "$status" -eq 0 ] || {
        cat "$tmp/$label.err" >&2
        fail "$label exited $status"
    }
    [ ! -s "$tmp/$label.err" ] || {
        cat "$tmp/$label.err" >&2
        fail "$label wrote to stderr"
    }
    printf '%s\n' "$expected" >"$tmp/$label.expected"
    cmp "$tmp/$label.expected" "$tmp/$label.out" ||
        fail "$label output drifted"
}

[ -x "$cgf" ] || fail "compiler is unavailable at $cgf"

expect_output version 'cgfried (Cgfried) 0.1.0
Copyright (C) 1970 Cgfried contributors
License GPL-3.0-only: GNU GPL version 3 only <https://gnu.org/licenses/gpl.html>.' \
    env SOURCE_DATE_EPOCH=0 "$cgf" --version
expect_output dumpversion '0.1.0' "$cgf" -dumpversion

for target in x86_64-linux-gnu arm64-linux arm64-macos \
    x86_64-linux-musl x86_64-freebsd
do
    label=$(printf '%s\n' "$target" | tr -c 'A-Za-z0-9' '_')
    expect_output "dumpmachine-$label" "$target" \
        "$cgf" "--target=$target" -dumpmachine
done

echo 'version-contract: PASS (version + five target selections)'
