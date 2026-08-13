#!/bin/sh
set -eu

if [ "$#" -lt 2 ]; then
    echo "libc-test-static-make: usage: TREE BUILD [make-arguments...]" >&2
    exit 2
fi

tree=$1
build=$2
shift 2

# libc-test assigns its default templates after reading config.mak, so a
# config-file assignment is overwritten before BINS is expanded. Command-line
# variables have the required precedence and keep this campaign on its promised
# static-only lane.
exec make --no-print-directory -C "$tree" B="$build" \
    functional.BINS_TEMPL=bin-static.exe \
    regression.BINS_TEMPL=bin-static.exe \
    math.BINS_TEMPL=bin-static.exe \
    musl.BINS_TEMPL=bin-static.exe \
    "$@"
