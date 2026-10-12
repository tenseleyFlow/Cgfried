#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build=${BUILD:-build}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-install-layout.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
stage=$tmp/stage
prefix=/usr/local

fail()
{
    echo "install-layout: $*" >&2
    exit 1
}

version=$("$root/$build/cgfried" -dumpversion)
target=$("$root/$build/cgfried" -dumpmachine)

make --no-print-directory -C "$root" BUILD="$build" \
    DESTDIR="$stage" PREFIX="$prefix" install >/dev/null

installed=$stage$prefix/bin/cgfried
[ -x "$installed" ] || fail "installed compiler is missing"
[ -L "$stage$prefix/bin/cgf" ] || fail "cgf alias is not a symlink"
[ -f "$stage$prefix/share/man/man1/cgfried.1" ] ||
    fail "manual page is missing"
[ -f "$stage$prefix/lib/cgfried/$version/include/stddef.h" ] ||
    fail "versioned headers are missing"
[ -f "$stage$prefix/lib/cgfried/$version/$target/libcgf_rt.a" ] ||
    fail "versioned runtime is missing"

search=$("$installed" -print-search-dirs)
printf '%s\n' "$search" | grep -Fq "$stage$prefix/bin" ||
    fail "installed search dirs do not name the staged prefix"

printf '%s\n' '#include <stddef.h>' \
    'int main(void) { size_t n = 0; return (int)n; }' >"$tmp/smoke.c"
(
    CDPATH= cd -- "$tmp"
    CGF_AS=0 "$installed" smoke.c -o smoke
    ./smoke
) || fail "staged installed compiler could not compile and run a program"

echo "install-layout: PASS ($version $target)"
