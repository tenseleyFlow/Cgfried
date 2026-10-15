#!/bin/sh
set -eu

: "${CGF_LD_LOG:?CGF_LD_LOG is required}"
printf '%s\n' "$@" >"$CGF_LD_LOG"
echo "staging-ld: intentional rejection" >&2
exit 23
