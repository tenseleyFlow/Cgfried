#!/bin/sh
# Capture the controlled-host provenance required for stage1 timing evidence.
set -eu
LC_ALL=C
export LC_ALL

die()
{
    echo "bootstrap-control: $*" >&2
    exit 3
}

[ "$#" -eq 2 ] || die 'usage: bootstrap-control.sh HOST OUTPUT'
host=$1
output=$2
root=$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)
control=${CGF_BOOTSTRAP_BENCH_CONTROL:-$root/scripts/bench-control.sh}
uname_cmd=${CGF_BOOTSTRAP_UNAME_CMD:-uname}
governor_root=${CGF_BOOTSTRAP_GOVERNOR_ROOT:-/sys/devices/system/cpu}
power_profile_cmd=${CGF_BOOTSTRAP_POWER_PROFILE_CMD:-powerprofilesctl}
[ -x "$control" ] || die "control helper is not executable: $control"
command -v "$uname_cmd" >/dev/null 2>&1 ||
    die "uname command is unavailable: $uname_cmd"
[ ! -d "$output" ] || die "output is a directory: $output"

system=$($uname_cmd -s 2>/dev/null || echo unknown)
machine=$($uname_cmd -m 2>/dev/null || echo unknown)
case $host:$system:$machine in
kasumi:Linux:x86_64 | hasu:Linux:x86_64 | \
nomad-1:Darwin:arm64 | nomad-1:Darwin:aarch64) ;;
kasumi:* | hasu:* | nomad-1:*)
    die "$host topology mismatch: got $system $machine"
    ;;
*) die "unsupported fleet host: $host" ;;
esac

uniform_cpu_field()
{
    field=$1
    observed=
    for path in "$governor_root"/cpu[0-9]*/cpufreq/"$field"; do
        [ -r "$path" ] || continue
        value=$(sed -n '1p' "$path") || die "cannot read $path"
        case $value in
        '' | *[!A-Za-z0-9_-]*) die "invalid $field value in $path" ;;
        esac
        if [ -z "$observed" ]; then
            observed=$value
        elif [ "$observed" != "$value" ]; then
            die "$field values disagree: $observed and $value"
        fi
    done
    [ -n "$observed" ] || die "no readable $field values"
    printf '%s\n' "$observed"
}

if [ "$system" = Darwin ]; then
    governor=unavailable
    power_profile=unavailable
    scaling_driver=unavailable
    energy_preference=unavailable
else
    governor=$(uniform_cpu_field scaling_governor)
    scaling_driver=$(uniform_cpu_field scaling_driver)
    energy_preference=$(uniform_cpu_field energy_performance_preference)
    command -v "$power_profile_cmd" >/dev/null 2>&1 ||
        die 'powerprofilesctl is required on a Linux fleet host'
    power_profile=$($power_profile_cmd get 2>/dev/null) ||
        die 'cannot read the current power profile'
fi

parent=${output%/*}
[ "$parent" != "$output" ] || parent=.
mkdir -p "$parent"
tmp=$(mktemp "$parent/.bootstrap-control.XXXXXX") ||
    die 'cannot create control temporary file'
trap 'rm -f "$tmp"' EXIT HUP INT TERM
{
    echo "host=$host"
    echo "governor=$governor"
    echo "power_profile=$power_profile"
    echo "scaling_driver=$scaling_driver"
    echo "energy_performance_preference=$energy_preference"
    "$control" measure "$host"
} >"$tmp"

status=0
classification=$($control classify --require-v2 "$tmp") || status=$?
case $status:$classification in
0:controlled) ;;
1:provenance-only) die 'host is not controlled enough for timing evidence' ;;
3:*) exit 3 ;;
*) die "control classifier failed with status $status" ;;
esac
mv "$tmp" "$output"
trap - EXIT HUP INT TERM
echo "bootstrap-control: wrote controlled provenance for $host"
