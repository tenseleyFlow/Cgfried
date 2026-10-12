#!/bin/sh
# Refuse release metadata drift. src/version.h is authoritative; every public
# copy and the dist basename must match its recomposed numeric triple.
set -eu

LC_ALL=C
export LC_ALL

root=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}
header=$root/src/version.h
manpage=$root/doc/cgfried.1
changelog=$root/CHANGELOG.md
readme=$root/README.md

fail()
{
    echo "check-version: $*" >&2
    exit 1
}

[ -f "$header" ] || fail "missing src/version.h"
[ -f "$manpage" ] || fail "missing doc/cgfried.1"
[ -f "$changelog" ] || fail "missing CHANGELOG.md"
[ -f "$readme" ] || fail "missing README.md"

macro()
{
    awk -v name="$1" '$1 == "#define" && $2 == name { print $3 }' "$header"
}

version=$(macro CGF_VERSION | tr -d '"')
major=$(macro CGF_VERSION_MAJOR)
minor=$(macro CGF_VERSION_MINOR)
patch=$(macro CGF_VERSION_PATCH)

[ -n "$version" ] || fail "CGF_VERSION is absent"
case $major:$minor:$patch in
    *[!0-9:]* | :* | *::*) fail "numeric version macros are malformed" ;;
esac
recomposed=$major.$minor.$patch
[ "$version" = "$recomposed" ] ||
    fail "CGF_VERSION $version != numeric triple $recomposed"

macro_count=$(grep -R '^#define CGF_VERSION ' "$root/src" | wc -l |
    tr -d '[:space:]')
[ "$macro_count" -eq 1 ] ||
    fail "CGF_VERSION must have exactly one definition (found $macro_count)"

man_version=$(sed -n \
    '1s/^\.TH CGFRIED 1 "[^"]*" "Cgfried \([^"]*\)" "Cgfried Manual"$/\1/p' \
    "$manpage")
[ "$man_version" = "$version" ] ||
    fail "man page version ${man_version:-<missing>} != $version"

release_line=$(sed -n \
    's/^## \[\([0-9][0-9.]*\)\] - \([0-9][0-9-]*\)$/\1	\2/p' \
    "$changelog" | sed -n '1p')
[ -n "$release_line" ] || fail "CHANGELOG.md has no release heading"
changelog_version=${release_line%%	*}
changelog_date=${release_line#*	}
[ "$changelog_version" = "$version" ] ||
    fail "newest changelog release $changelog_version != $version"

man_date=$(sed -n '1s/^\.TH CGFRIED 1 "\([^"]*\)".*/\1/p' "$manpage")
[ "$man_date" = "$changelog_date" ] ||
    fail "man date $man_date != changelog date $changelog_date"

grep -Fq "Version $version " "$readme" ||
    fail "README.md does not identify Version $version"

dist_name=$(make --no-print-directory -s -C "$root" dist-name)
[ "$dist_name" = "cgfried-$version.tar.gz" ] ||
    fail "dist basename $dist_name != cgfried-$version.tar.gz"

echo "check-version: PASS ($version)"
