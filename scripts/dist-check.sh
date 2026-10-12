#!/bin/sh
# Prove that the exported release, rather than the developer checkout, builds,
# tests on the native release lane, installs, and compiles from its staged
# versioned layout.
set -eu

LC_ALL=C
export LC_ALL

archive=${1:-}
[ -n "$archive" ] && [ -f "$archive" ] || {
    echo "usage: $0 cgfried-VERSION.tar.gz" >&2
    exit 3
}
case $archive in
    /*) ;;
    *) archive=$PWD/$archive ;;
esac

tmp=$(mktemp -d "${TMPDIR:-/tmp}/cgf-dist-check.XXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
tar -xzf "$archive" -C "$tmp"

set -- "$tmp"/cgfried-*
[ "$#" -eq 1 ] && [ -d "$1" ] || {
    echo "dist-check: archive must contain one cgfried-VERSION root" >&2
    exit 1
}
source_root=$1
jobs=${DIST_CHECK_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}

make --no-print-directory -C "$source_root" -j"$jobs" all
make --no-print-directory -C "$source_root" test-release-contracts \
    test-release-metadata test-install

platform=$(uname -s):$(uname -m)
case $platform in
    Linux:x86_64)
        make --no-print-directory -C "$source_root" -j"$jobs" test
        ;;
    Linux:aarch64 | Linux:arm64)
        make --no-print-directory -C "$source_root" -j"$jobs" \
            build/cgf-test build/a64mir
        CGF_A64_CGF=build/cgfried \
            sh -c 'cd "$1" && sh scripts/a64_corpus_lane.sh "" build/cgf-test' \
            sh "$source_root"
        sh -c 'cd "$1" && sh scripts/a64_exec_lane.sh build/a64mir' \
            sh "$source_root"
        ;;
    Darwin:arm64 | Darwin:aarch64)
        CGF_AS=0 "$source_root/ci/campaigns/test-arm64-macos-stdarg.sh" \
            "$source_root/build/cgfried"
        CGF_AS=0 "$source_root/ci/campaigns/test-float16-hosted-boundary.sh" \
            "$source_root/build/cgfried"
        ;;
    *)
        echo "dist-check: unsupported native release lane $platform" >&2
        exit 1
        ;;
esac

echo "dist-check: PASS ($platform)"
