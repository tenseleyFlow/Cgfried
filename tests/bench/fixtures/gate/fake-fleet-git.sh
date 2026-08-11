#!/bin/sh
set -eu
if [ "${1:-}" = -C ]; then
    shift 2
fi
case ${1:-} in
status | add | commit) exit 0 ;;
*) exit 2 ;;
esac
