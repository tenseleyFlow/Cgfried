#!/bin/sh
set -eu

[ "$#" -eq 1 ] && [ "$1" = _NPROCESSORS_ONLN ] || exit 2
printf '%s\n' "${CGF_BOOTSTRAP_TEST_CPUS:-20}"
