#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-dist-repro.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" show -s --format=%ct HEAD)}

SOURCE_DATE_EPOCH=$epoch sh "$root/scripts/make-dist.sh" "$tmp/one.tar.gz"
SOURCE_DATE_EPOCH=$epoch sh "$root/scripts/make-dist.sh" "$tmp/two.tar.gz"
cmp "$tmp/one.tar.gz" "$tmp/two.tar.gz" || {
    echo "dist-repro: repeated archives differ" >&2
    exit 1
}

tar -tzf "$tmp/one.tar.gz" >"$tmp/members"
tar -tvzf "$tmp/one.tar.gz" >"$tmp/modes"
case $(sed -n '1p' "$tmp/modes") in
    drwxr-xr-x*) ;;
    *) echo "dist-repro: archive modes are not pinned to umask 0022" >&2; exit 1 ;;
esac
grep -q '^cgfried-0\.1\.0/afs-as/' "$tmp/members" ||
    { echo "dist-repro: afs-as sources are absent" >&2; exit 1; }
grep -q '^cgfried-0\.1\.0/afs-ld/' "$tmp/members" ||
    { echo "dist-repro: afs-ld sources are absent" >&2; exit 1; }
if grep -Eq '(^|/)\.git(/|$)' "$tmp/members"; then
    echo "dist-repro: archive contains Git administration data" >&2
    exit 1
fi

if command -v sha256sum >/dev/null 2>&1; then
    digest=$(sha256sum "$tmp/one.tar.gz" | awk '{ print $1 }')
else
    digest=$(shasum -a 256 "$tmp/one.tar.gz" | awk '{ print $1 }')
fi
echo "dist-repro: PASS sha256=$digest"
