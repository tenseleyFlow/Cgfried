#!/bin/sh
set -eu

output=
opt=unknown
while [ "$#" -gt 0 ]; do
    case $1 in
    -o)
        shift
        output=${1:-}
        ;;
    -O2) opt=O2 ;;
    -Os) opt=Os ;;
    esac
    shift
done
[ -n "$output" ] || exit 2
printf 'fixture executable %s\n' "$opt" >"$output"
