#!/bin/sh
set -eu

last=
for arg do last=$arg; done
if [ -n "${CGF_FAIL_MV_DEST:-}" ] && [ "$last" = "$CGF_FAIL_MV_DEST" ]; then
    exit 23
fi
exec "$CGF_REAL_MV" "$@"
