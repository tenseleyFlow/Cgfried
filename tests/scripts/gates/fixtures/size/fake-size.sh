#!/bin/sh
set -eu

for arg in "$@"; do
    case $arg in
    -*) ;;
    *) file=$arg ;;
    esac
done
[ -n "${file:-}" ] || exit 2
printf '%s  :\n' "$file"
printf '.text 11 0\n'
printf '.data 3 0\n'
printf '.rodata 5 0\n'
