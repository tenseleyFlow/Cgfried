#!/bin/sh
# Enter and leave the controlled Linux power profile used by fleet benchmarks.
set -eu

LC_ALL=C
export LC_ALL

prog=fleet-performance-mode
profile_cmd=${CGF_FLEET_POWER_PROFILE_CMD:-powerprofilesctl}
governor_root=${CGF_FLEET_GOVERNOR_ROOT:-/sys/devices/system/cpu}
state_dir=${CGF_FLEET_POWER_STATE_DIR:-${XDG_STATE_HOME:-$HOME/.local/state}/cgfried-fleet}
action=${1:-}
host=${2:-${CGF_FLEET_HOST:-}}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

valid_profile()
{
    case $1 in
    balanced | performance | power-saver) return 0 ;;
    *) return 1 ;;
    esac
}

read_profile()
{
    profile=$($profile_cmd get 2>/dev/null) || die "cannot read the current power profile"
    valid_profile "$profile" || die "invalid power profile '$profile'"
    printf '%s\n' "$profile"
}

read_uniform_cpu_field()
{
    field=$1
    label=$2
    set -- "$governor_root"/cpu[0-9]*/cpufreq/"$field"
    [ -r "$1" ] || die "no readable CPU $label values under $governor_root"

    observed=
    for field_file do
        [ -r "$field_file" ] || die "CPU $label is not readable: $field_file"
        value=$(sed -n '1p' "$field_file") || die "cannot read CPU $label: $field_file"
        case $value in
        '' | *[!A-Za-z0-9_-]*) die "invalid CPU $label in $field_file" ;;
        esac
        if [ -z "$observed" ]; then
            observed=$value
        elif [ "$observed" != "$value" ]; then
            die "CPU $label values disagree ('$observed' and '$value')"
        fi
    done
    printf '%s\n' "$observed"
}

verify_performance_state()
{
    governor=$(read_uniform_cpu_field scaling_governor 'scaling governor')
    [ "$governor" = performance ] && return 0

    driver=$(read_uniform_cpu_field scaling_driver 'scaling driver')
    epp=$(read_uniform_cpu_field energy_performance_preference \
        'energy-performance preference')
    if [ "$governor" = powersave ] && [ "$driver" = intel_pstate ] &&
       [ "$epp" = performance ]; then
        return 0
    fi
    die "effective CPU performance state is uncontrolled (governor='$governor' scaling_driver='$driver' energy_performance_preference='$epp')"
}

case $host in
kasumi | hasu) ;;
'') die "usage: $0 enter|leave kasumi|hasu" ;;
*) die "unsupported Linux fleet host '$host'" ;;
esac
case $action in
enter | leave) ;;
*) die "usage: $0 enter|leave kasumi|hasu" ;;
esac
command -v "$profile_cmd" >/dev/null 2>&1 || die "power profile command not found: $profile_cmd"

umask 077
state_file=$state_dir/power-profile.$host

if [ "$action" = enter ]; then
    current=$(read_profile)
    mkdir -p "$state_dir"
    if [ -e "$state_file" ]; then
        saved=$(cat "$state_file") || die "cannot read saved power profile: $state_file"
        valid_profile "$saved" || die "invalid saved power profile in $state_file"
    else
        state_tmp=$state_file.$$
        trap 'rm -f "$state_tmp"' EXIT HUP INT TERM
        printf '%s\n' "$current" >"$state_tmp"
        mv "$state_tmp" "$state_file"
        trap - EXIT HUP INT TERM
    fi

    "$profile_cmd" set performance >/dev/null 2>&1 ||
        die "cannot set the performance power profile"
    [ "$(read_profile)" = performance ] || die "performance power profile did not take effect"
    verify_performance_state
    echo "$prog: entered performance mode on $host"
    exit 0
fi

[ -e "$state_file" ] || exit 0
saved=$(cat "$state_file") || die "cannot read saved power profile: $state_file"
valid_profile "$saved" || die "invalid saved power profile in $state_file"
"$profile_cmd" set "$saved" >/dev/null 2>&1 || die "cannot restore power profile '$saved'"
[ "$(read_profile)" = "$saved" ] || die "power profile '$saved' was not restored"
rm -f "$state_file"
echo "$prog: restored power profile '$saved' on $host"
