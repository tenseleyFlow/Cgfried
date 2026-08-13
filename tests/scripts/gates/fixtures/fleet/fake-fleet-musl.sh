#!/bin/sh
set -eu

: "${CGF_FLEET_MUSL_ROOT:?}"
: "${CGF_FLEET_HOST:?}"
: "${CGF_FLEET_STAMP:?}"
: "${CGF_FLEET_MUSL_SOURCE:?}"
result=$CGF_FLEET_MUSL_ROOT/.benchmarks/runs/$CGF_FLEET_STAMP-$CGF_FLEET_HOST-musl-full-build.txt
{
    echo 'schema=cgfried.musl-full-build.v1'
    echo "host=$CGF_FLEET_HOST"
    echo "source=$CGF_FLEET_MUSL_SOURCE"
    echo "fleet.gate=${FIXTURE_MUSL_GATE:-warmup}"
} >"$result"
exit "${FIXTURE_MUSL_STATUS:-0}"
