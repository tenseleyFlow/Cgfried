#!/bin/sh
set -eu

: "${CGF_FLEET_BOOTSTRAP_RESULT:?}"
{
    echo 'schema=cgfried.bootstrap-timing.v1'
    echo 'target=x86_64-linux-gnu'
    echo "host=$CGF_FLEET_HOST"
    echo "fleet.nightly_stamp=$CGF_FLEET_STAMP"
    case ${FIXTURE_BOOTSTRAP_STATUS:-0} in
    0) echo 'fleet.bootstrap_time_gate=pass' ;;
    1) echo 'fleet.bootstrap_time_gate=trial-trip' ;;
    esac
} >"$CGF_FLEET_BOOTSTRAP_RESULT"
exit "${FIXTURE_BOOTSTRAP_STATUS:-0}"
