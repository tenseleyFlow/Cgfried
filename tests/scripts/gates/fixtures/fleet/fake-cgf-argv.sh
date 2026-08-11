#!/bin/sh
set -eu

: "${FIXTURE_CGF_ARGV_LOG:?}"
: >"$FIXTURE_CGF_ARGV_LOG"
for arg do
    printf '%s\n' "$arg" >>"$FIXTURE_CGF_ARGV_LOG"
done
