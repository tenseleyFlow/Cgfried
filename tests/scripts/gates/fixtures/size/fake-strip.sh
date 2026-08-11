#!/bin/sh
set -eu

for arg in "$@"; do
    case $arg in
    -*) ;;
    *) file=$arg ;;
    esac
done
[ -n "${file:-}" ] || exit 2
tmp=$file.tmp
dd if="$file" of="$tmp" bs=1 count=7 2>/dev/null
mv "$tmp" "$file"
