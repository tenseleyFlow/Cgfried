#!/bin/sh
# Build a byte-reproducible release archive from Git objects only: the parent
# commit plus the two exact gitlink commits. No developer working-tree bytes
# can enter the archive.
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
treeish=${CGF_DIST_TREEISH:-HEAD}
version=$(sed -n '/^#define CGF_VERSION "/ {
    s/^[^"]*"//
    s/".*$//
    p
}' "$root/src/version.h")
[ -n "$version" ] || {
    echo "make-dist: cannot read CGF_VERSION" >&2
    exit 1
}

output=${1:-cgfried-$version.tar.gz}
case $output in
    /*) ;;
    *) output=$PWD/$output ;;
esac
epoch=${SOURCE_DATE_EPOCH:-$(git -C "$root" show -s --format=%ct "$treeish")}
case $epoch in
    '' | *[!0-9]*) echo "make-dist: SOURCE_DATE_EPOCH must be decimal" >&2; exit 1 ;;
esac

gzip_bin=${CGF_DIST_GZIP:-gzip}
if ! command -v "$gzip_bin" >/dev/null 2>&1; then
    echo "make-dist: GNU gzip is required: command not found: $gzip_bin" >&2
    exit 1
fi
if ! "$gzip_bin" --version 2>&1 |
    grep -F 'Free Software Foundation' >/dev/null
then
    echo "make-dist: GNU gzip is required for cross-host byte identity" >&2
    echo "make-dist: macOS: brew install gzip; or set CGF_DIST_GZIP" >&2
    exit 1
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-dist.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
name=cgfried-$version

git -C "$root" -c tar.umask=0022 archive --format=tar \
    --prefix="$name/" "$treeish" |
    tar -xf - -C "$tmp"

common=$(git -C "$root" rev-parse --git-common-dir)
case $common in
    /*) ;;
    *) common=$root/$common ;;
esac

for submodule in afs-as afs-ld
do
    set -- $(git -C "$root" ls-tree "$treeish" -- "$submodule")
    [ "$#" -eq 4 ] && [ "$1" = 160000 ] && [ "$2" = commit ] || {
        echo "make-dist: $submodule is not a pinned gitlink" >&2
        exit 1
    }
    revision=$3
    gitdir=$(git -C "$root/$submodule" rev-parse --absolute-git-dir \
        2>/dev/null || :)
    [ -n "$gitdir" ] || gitdir=$common/modules/$submodule
    [ -d "$gitdir" ] &&
        git --git-dir="$gitdir" cat-file -e "$revision^{commit}" 2>/dev/null || {
        echo "make-dist: missing pinned $submodule object $revision" >&2
        echo "make-dist: run 'git submodule update --init'" >&2
        exit 1
    }
    git --git-dir="$gitdir" -c tar.umask=0022 archive --format=tar \
        --prefix="$name/$submodule/" "$revision" |
        tar -xf - -C "$tmp"
done

# Re-index the object-derived tree in a private repository and give its sole
# commit the requested epoch. git-archive then fixes ordering, modes, uid/gid,
# and every member mtime without depending on GNU tar extensions.
archive_repo=$tmp/archive-repo
mkdir -p "$archive_repo"
mv "$tmp/$name" "$archive_repo/$name"
git init -q "$archive_repo"
git -C "$archive_repo" -c core.autocrlf=false -c core.filemode=true \
    add -f -- "$name"
tree=$(git -C "$archive_repo" write-tree)
commit=$(
    printf '%s\n' "Cgfried $version reproducible source archive" |
        GIT_AUTHOR_NAME=Cgfried GIT_AUTHOR_EMAIL=release@cgfried.invalid \
        GIT_COMMITTER_NAME=Cgfried GIT_COMMITTER_EMAIL=release@cgfried.invalid \
        GIT_AUTHOR_DATE="@$epoch +0000" GIT_COMMITTER_DATE="@$epoch +0000" \
        git -C "$archive_repo" commit-tree "$tree"
)
git -C "$archive_repo" -c tar.umask=0022 archive --format=tar "$commit" |
    "$gzip_bin" -n -9 >"$tmp/$name.tar.gz"
mv -f "$tmp/$name.tar.gz" "$output"

echo "make-dist: wrote $output"
