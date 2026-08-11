#!/bin/sh
# Run the Sprint 54 kernel-runtime truth lane on one named fleet host.
# The host name is an inventory identity (CGF_FLEET_HOST), not necessarily
# the machine's local hostname: the nomad-1 SSH alias names a Darwin arm64 Mac.
set -eu

LC_ALL=C
export LC_ALL

prog=fleet-perf
root=${CGF_FLEET_ROOT:-$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)}
hostname_cmd=${CGF_FLEET_HOSTNAME_CMD:-hostname}
uname_cmd=${CGF_FLEET_UNAME_CMD:-uname}
date_cmd=${CGF_FLEET_DATE_CMD:-date}
compare=${CGF_FLEET_KERNEL_COMPARE:-$root/scripts/kernel-compare.sh}
gate=${CGF_FLEET_RUNTIME_GATE:-$root/scripts/runtime_gate.sh}
config=${CGF_FLEET_RUNTIME_CONFIG:-$root/ci/gates.d/kernel-runtime.conf}
run_dir=${CGF_FLEET_RUN_DIR:-$root/.benchmarks/runs}
work=${CGF_FLEET_KERNEL_WORK:-$root/build/fleet-kernel-runtime}

die()
{
    echo "$prog: $*" >&2
    exit 3
}

command -v "$hostname_cmd" >/dev/null 2>&1 || die "hostname command not found: $hostname_cmd"
command -v "$uname_cmd" >/dev/null 2>&1 || die "uname command not found: $uname_cmd"
command -v "$date_cmd" >/dev/null 2>&1 || die "date command not found: $date_cmd"
[ -x "$compare" ] || die "kernel comparison script is not executable: $compare"
[ -x "$gate" ] || die "runtime gate is not executable: $gate"
[ -r "$config" ] || die "runtime gate config is not readable: $config"

host=${CGF_FLEET_HOST:-$($hostname_cmd -s 2>/dev/null || $uname_cmd -n)}
system=$($uname_cmd -s 2>/dev/null || echo unknown)
machine=$($uname_cmd -m 2>/dev/null || echo unknown)
case $host:$system:$machine in
kasumi:Linux:x86_64 | hasu:Linux:x86_64) target=x86_64-linux-gnu ;;
nomad-1:Darwin:arm64 | nomad-1:Darwin:aarch64) target=arm64-macos ;;
kasumi:* | hasu:* | nomad-1:*)
    die "$host topology mismatch: expected kasumi/hasu=Linux x86_64 or nomad-1=Darwin arm64, got $system $machine"
    ;;
*) die "unsupported fleet host '$host'" ;;
esac

case $run_dir in
"$root"/.benchmarks/runs) ;;
"$root"/.benchmarks/runs/*) ;;
*) die "run directory must be inside $root/.benchmarks/runs" ;;
esac

stamp=${CGF_FLEET_STAMP:-$($date_cmd -u '+%Y-%m-%dT%H%M%SZ')}
case $stamp in
????-??-??T??????Z) ;;
*) die "date command returned malformed UTC stamp '$stamp'" ;;
esac
result=${CGF_FLEET_RUNTIME_RESULT:-$run_dir/$stamp-$host-kernels.txt}
case $result in
"$run_dir"/????-??-??T??????Z-"$host"-kernels.txt) ;;
*) die "runtime result must be a dated $host kernel artifact inside $run_dir" ;;
esac
[ ! -e "$result" ] || die "refusing to overwrite $result"
mkdir -p "$run_dir" "$work"

CGF_KERNEL_RUNTIME_ONLY=1 \
CGF_KERNEL_RUNTIME_HOST=$host \
CGF_KERNEL_RUNTIME_OUTPUT=$result \
CGF_KERNEL_TARGETS=$target \
CGF_KERNEL_COMPARE_WORK=$work \
    "$compare"
[ -s "$result" ] || die "kernel comparison produced no runtime artifact: $result"
echo "$prog: wrote $result (target=$target)"

baseline=${CGF_FLEET_RUNTIME_BASELINE:-$root/.benchmarks/baseline-kernel-runtime-$target.$host.txt}
history=$work/history.txt
find "$run_dir" -maxdepth 1 -type f -name "*-$(printf '%s' "$host")-kernels.txt" -print |
    sort >"$history"
run_count=$(wc -l <"$history" | tr -d ' ')

if [ ! -r "$baseline" ] || [ "$run_count" -lt 3 ]; then
    if [ -r "$baseline" ]; then
        baseline_state=present
    else
        baseline_state=missing
    fi
    echo "$prog: kernel-runtime trial warmup: host=$host target=$target baseline=$baseline_state runs=$run_count/3; gate not run"
    echo 'fleet.runtime_gate=warmup' >>"$result"
    echo 'fleet.runtime_gate_trip=no' >>"$result"
    exit 0
fi

last_three=$work/last-three.txt
tail -n 3 "$history" >"$last_three"
set --
while IFS= read -r run; do
    set -- "$@" "$run"
done <"$last_three"
[ "$#" -eq 3 ] || die "internal history selection did not yield three runs"
gate_status=0
gate_result_file=$work/runtime-gate-result.txt
CGF_RUNTIME_GATE_RESULT_FILE=$gate_result_file \
    "$gate" "$config" "$baseline" "$@" || gate_status=$?
[ -r "$gate_result_file" ] || die "runtime gate did not create its result file"
gate_decision=$(sed -n '1p' "$gate_result_file")
[ "$(wc -l <"$gate_result_file" | tr -d ' ')" -eq 1 ] ||
    die "runtime gate did not produce one machine-readable result"
case $gate_decision in
pass | trip) ;;
*) die "runtime gate produced invalid machine-readable result '$gate_decision'" ;;
esac
gate_state=$(awk '
    /^[[:space:]]*state[[:space:]]*=/ {
        line = $0
        sub(/[[:space:]]*#.*/, "", line)
        sub(/^[^=]*=[[:space:]]*/, "", line)
        sub(/[[:space:]]+$/, "", line)
        print line
        count++
    }
    END { if (count != 1) exit 3 }
' "$config") || die "cannot determine validated runtime gate state"
case $gate_state:$gate_status in
trial:0)
    case $gate_decision in
    pass) gate_result=trial-pass; gate_trip=no ;;
    trip) gate_result=trial-trip; gate_trip=yes ;;
    esac
    ;;
blocking:0)
    [ "$gate_decision" = pass ] || die "blocking gate returned success for a trip"
    gate_result=blocking-pass; gate_trip=no
    ;;
blocking:1)
    [ "$gate_decision" = trip ] || die "blocking gate failed without a trip"
    gate_result=blocking-trip; gate_trip=yes
    ;;
*) die "runtime gate returned unexpected state/status $gate_state/$gate_status" ;;
esac
echo "fleet.runtime_gate=$gate_result" >>"$result"
echo "fleet.runtime_gate_trip=$gate_trip" >>"$result"
echo "$prog: kernel-runtime gate evaluated against $baseline using the latest 3 runs; baseline unchanged"
exit "$gate_status"
