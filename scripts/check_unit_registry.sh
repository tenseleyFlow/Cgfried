#!/bin/sh
# Every unit test declared in tests/unit/ must be IN the registry.
#
# gen_unit_registry.sh discovers tests by matching
# `^void test_<name>(TestCtx *` on ONE LINE. A declaration long enough for
# clang-format to wrap therefore vanishes from discovery -- silently, because
# a test that is never registered is indistinguishable from a test that
# passes. The suite still reports "N tests, 0 failures"; N is just smaller
# than it should be, and nobody counts N by hand.
#
# This has now happened TWICE. Sprint 36 hit it and fixed that instance by
# renaming the test; the note claimed the count was asserted by the full
# suite, but no such check existed, and
# test_dep_ptr_recognizer_accepts_k_i_plus_c_and_rejects_nonaffine was dead
# from the day it was written until Sprint 55 counted the two lists. A fix
# that renames one test does not stop the next one.
#
# The comparison is DECLARED (what a human wrote) against REGISTERED (what
# will run). Comparing the registry against itself would prove nothing.
set -eu
LC_ALL=C
export LC_ALL

registry=${1:-}
if [ -z "$registry" ] || [ ! -f "$registry" ]; then
    echo "usage: check_unit_registry.sh <build>/gen/unit_registry.c" >&2
    exit 2
fi

work=${TMPDIR:-/tmp}/cgf-unitreg.$$
mkdir -p "$work"
trap 'rm -rf "$work"' EXIT INT TERM

# A declaration is any line STARTING a test function, wrapped or not: the
# generator's stricter one-line pattern is exactly what we are auditing, so
# this side must be looser than it, or the two agree by construction.
grep -h '^void test_[a-z0-9_]*(' tests/unit/*.c |
    sed -n 's/^void \(test_[a-z0-9_]*\)(.*/\1/p' | sort -u > "$work/declared"
sed -n 's/^    { "\(test_[a-z0-9_]*\)",.*/\1/p' "$registry" |
    sort -u > "$work/registered"

if ! comm -23 "$work/declared" "$work/registered" > "$work/missing"; then
    echo "check_unit_registry: could not compare the two lists" >&2
    exit 2
fi

if [ -s "$work/missing" ]; then
    echo "check_unit_registry: declared but NEVER REGISTERED (these tests do" \
        "not run):" >&2
    sed 's/^/  /' "$work/missing" >&2
    echo "  gen_unit_registry.sh needs the declaration on ONE line;" \
        "shorten the name." >&2
    exit 1
fi

n=$(grep -c . "$work/registered")
echo "check_unit_registry: all $n declared unit tests are registered"
