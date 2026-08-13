#!/bin/sh
set -eu

[ "$#" -eq 1 ] && [ "$1" = 5 ] || exit 2
cp "$CGF_BOOTSTRAP_TEST_STAT_AFTER" "$CGF_BOOTSTRAP_TEST_PROC/stat"
