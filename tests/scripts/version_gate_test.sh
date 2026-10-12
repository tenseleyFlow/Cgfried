#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
gate=$root/ci/check-version.sh
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-version-gate.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

fail()
{
    echo "version-gate-meta: $*" >&2
    exit 1
}

sh "$gate" "$root" >/dev/null

mkdir -p "$tmp/src" "$tmp/doc" "$tmp/include"
cp "$root/src/version.h" "$tmp/src/version.h"
cp "$root/doc/cgfried.1" "$tmp/doc/cgfried.1"
cp "$root/CHANGELOG.md" "$tmp/CHANGELOG.md"
cp "$root/README.md" "$tmp/README.md"
cp "$root/Makefile" "$tmp/Makefile"

sed 's/^#define CGF_VERSION_PATCH 0$/#define CGF_VERSION_PATCH 1/' \
    "$root/src/version.h" >"$tmp/src/version.h"
if sh "$gate" "$tmp" >"$tmp/drift.out" 2>&1; then
    fail "one-place numeric version drift unexpectedly passed"
fi
grep -Fq 'numeric triple 0.1.1' "$tmp/drift.out" ||
    fail "numeric drift did not produce the expected diagnosis"

cp "$root/src/version.h" "$tmp/src/version.h"
sed '1s/Cgfried 0.1.0/Cgfried 0.1.1/' "$root/doc/cgfried.1" \
    >"$tmp/doc/cgfried.1"
if sh "$gate" "$tmp" >"$tmp/man-drift.out" 2>&1; then
    fail "one-place man-page version drift unexpectedly passed"
fi
grep -Fq 'man page version 0.1.1 != 0.1.0' "$tmp/man-drift.out" ||
    fail "man drift did not produce the expected diagnosis"

echo 'version-gate-meta: PASS'
