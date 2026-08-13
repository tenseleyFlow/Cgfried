#!/bin/sh
# Deterministic container-boundary stand-in for the bootstrap-cross meta-test.
set -eu
LC_ALL=C
export LC_ALL

root=
target=arm64-linux
for argument in "$@"; do
    case $argument in
    BOOTSTRAP_ROOT=*) root=${argument#*=} ;;
    BOOTSTRAP_TARGET=*) target=${argument#*=} ;;
    esac
done
[ -n "$root" ] || {
    echo 'fake-cross-make: BOOTSTRAP_ROOT is required' >&2
    exit 2
}

find "$root/compiler" "$root/runtime" -type f -name '*.s' -print |
    sort | while IFS= read -r assembly; do
    cp "$assembly" "${assembly%.s}.o"
done

mkdir -p "$root/$target"
archive=$root/$target/libcgf_rt.a
: >"$archive"
find "$root/runtime" -type f -name '*.o' -print | sort |
    while IFS= read -r object; do
        sha256sum "$object" | awk '{print $1}'
    done >"$archive"

: >"$root/cgfried"
find "$root/compiler" -type f -name '*.o' -print | sort |
    while IFS= read -r object; do
        sha256sum "$object" | awk '{print $1}'
    done >"$root/cgfried"
sha256sum "$archive" | awk '{print $1}' >>"$root/cgfried"
chmod +x "$root/cgfried"
