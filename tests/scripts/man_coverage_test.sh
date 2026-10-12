#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
compiler=${CGF_TEST_CC:-$root/build/cgfried}
gate=$root/ci/check-man-coverage.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-man-coverage-meta.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "man-coverage-meta: $*" >&2
    exit 1
}

sh "$gate" "$root" "$compiler" >/dev/null

# Remove one public flag everywhere it occurs in the manual. The source table
# remains authoritative, so the gate must name the missing spelling.
sed 's/\\-\\-version/\\-\\-release-version/g' "$root/doc/cgfried.1" \
    >"$tmp/cgfried.1"
if CGF_MAN_FILE=$tmp/cgfried.1 sh "$gate" "$root" "$compiler" \
    >"$tmp/missing.out" 2>&1; then
    fail "manual with --version removed unexpectedly passed"
fi
grep -Fq 'man page omits driver option --version' "$tmp/missing.out" ||
    fail "missing option did not produce the expected diagnosis"

echo 'man-coverage-meta: PASS'
