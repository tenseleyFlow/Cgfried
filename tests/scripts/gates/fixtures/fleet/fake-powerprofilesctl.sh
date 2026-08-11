#!/bin/sh
set -eu

: "${FIXTURE_POWER_PROFILE_STATE:?}"
: "${FIXTURE_GOVERNOR_ROOT:?}"

case ${1:-} in
get)
    cat "$FIXTURE_POWER_PROFILE_STATE"
    ;;
set)
    case ${2:-} in
    balanced | performance | power-saver) profile=$2 ;;
    *) exit 2 ;;
    esac
    [ "${FIXTURE_POWER_PROFILE_FAIL_SET:-}" != "$profile" ] || exit 1
    printf '%s\n' "$profile" >"$FIXTURE_POWER_PROFILE_STATE"
    case $profile in
    performance) governor=performance ;;
    *) governor=powersave ;;
    esac
    [ -z "${FIXTURE_POWER_PROFILE_GOVERNOR_OVERRIDE:-}" ] ||
        governor=$FIXTURE_POWER_PROFILE_GOVERNOR_OVERRIDE
    for governor_file in "$FIXTURE_GOVERNOR_ROOT"/cpu[0-9]*/cpufreq/scaling_governor; do
        [ -e "$governor_file" ] || continue
        printf '%s\n' "$governor" >"$governor_file"
    done
    case $profile in
    performance) epp=performance ;;
    *) epp=balance_performance ;;
    esac
    [ -z "${FIXTURE_POWER_PROFILE_EPP_OVERRIDE:-}" ] ||
        epp=$FIXTURE_POWER_PROFILE_EPP_OVERRIDE
    for epp_file in "$FIXTURE_GOVERNOR_ROOT"/cpu[0-9]*/cpufreq/energy_performance_preference; do
        [ -e "$epp_file" ] || continue
        printf '%s\n' "$epp" >"$epp_file"
    done
    ;;
*) exit 2 ;;
esac
