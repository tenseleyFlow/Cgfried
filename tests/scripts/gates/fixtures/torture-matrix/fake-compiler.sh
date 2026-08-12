#!/bin/sh
set -eu

[ "$#" -eq 1 ] && [ "$1" = -dumpmachine ] || exit 2
printf '%s\n' "${FAKE_DUMPMACHINE_TARGET:-x86_64-linux-gnu}"
