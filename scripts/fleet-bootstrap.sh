#!/bin/sh
# Produce one controlled, dated Sprint 58 O2 stage1 timing receipt.
set -eu
LC_ALL=C
export LC_ALL

die()
{
    echo "fleet-bootstrap: $*" >&2
    exit 3
}

root=${CGF_FLEET_ROOT:-$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)}
host=${CGF_FLEET_HOST:-}
stamp=${CGF_FLEET_STAMP:-$(date -u '+%Y-%m-%dT%H%M%SZ')}
run_dir=${CGF_FLEET_RUN_DIR:-$root/.benchmarks/runs}
result=${CGF_FLEET_BOOTSTRAP_RESULT:-$run_dir/$stamp-$host-bootstrap.txt}
baseline=${CGF_FLEET_BOOTSTRAP_BASELINE:-$root/.benchmarks/baseline-bootstrap-O2-x86_64-linux-gnu.$host.txt}
make_cmd=${CGF_FLEET_MAKE_CMD:-make}
git_cmd=${CGF_FLEET_GIT_CMD:-git}
uname_cmd=${CGF_FLEET_UNAME_CMD:-uname}
hostcc=${HOSTCC:-${CGF_FLEET_CC:-gcc}}
jobs=${CGF_BOOTSTRAP_JOBS:-8}
work_base=${CGF_FLEET_BOOTSTRAP_WORK:-$root/build/fleet-bootstrap-$stamp}
gate=${CGF_FLEET_BOOTSTRAP_GATE:-$root/scripts/bootstrap-time-gate.sh}
control=${CGF_FLEET_BENCH_CONTROL:-$root/scripts/bench-control.sh}
sysroot=${CGF_FLEET_SYSROOT:-}

case $host in
kasumi | hasu) ;;
*) die 'CGF_FLEET_HOST must explicitly name kasumi or hasu' ;;
esac
system=$($uname_cmd -s 2>/dev/null || echo unknown)
machine=$($uname_cmd -m 2>/dev/null || echo unknown)
[ "$system:$machine" = Linux:x86_64 ] ||
    die "$host topology mismatch: got $system $machine"
[ "$jobs" = 8 ] || die 'CGF_BOOTSTRAP_JOBS must be exactly 8'
case $stamp in
????-??-??T??????Z) ;;
*) die "malformed UTC stamp '$stamp'" ;;
esac
case $run_dir in
"$root"/.benchmarks/runs | "$root"/.benchmarks/runs/*) ;;
*) die 'run directory must be inside .benchmarks/runs' ;;
esac
case $result in
"$run_dir"/????-??-??T??????Z-"$host"-bootstrap.txt) ;;
*) die "result must be a dated $host bootstrap artifact" ;;
esac
case ${work_base##*/} in
fleet-bootstrap-????-??-??T??????Z) ;;
*) die 'bootstrap work root has an unsafe name' ;;
esac
for command_path in "$make_cmd" "$git_cmd" "$uname_cmd" "$hostcc"; do
    command -v "$command_path" >/dev/null 2>&1 ||
        die "required command not found: $command_path"
done
[ -x "$gate" ] || die "timing gate is not executable: $gate"
[ -x "$control" ] || die "control helper is not executable: $control"
[ -d "$root/.git" ] || die 'a git checkout is required'
[ -z "$($git_cmd -C "$root" status --porcelain --untracked-files=normal)" ] ||
    die 'checkout must be clean before bootstrap timing'
[ ! -e "$result" ] || die "refusing to overwrite $result"
if [ -n "$sysroot" ]; then
    [ -d "$sysroot" ] || die "fleet sysroot is missing: $sysroot"
fi

mkdir -p "$run_dir" "${work_base%/*}"
echo "fleet-bootstrap: running controlled O2 stage1 self-compile on $host"
"$make_cmd" -C "$root" bootstrap-O2 HOSTCC="$hostcc" \
    BOOTSTRAP_JOBS=8 BOOTSTRAP_WORK="$work_base" \
    BOOTSTRAP_HOST="$host" BOOTSTRAP_SYSROOT="$sysroot"
timing=$work_base/O2/stage1-time.txt
[ -r "$timing" ] || die "bootstrap timing receipt is missing: $timing"

temporary=$run_dir/.$stamp-$host-bootstrap.tmp
trap 'rm -f "$temporary"' EXIT HUP INT TERM
cp "$timing" "$temporary"
control_status=0
"$control" classify --require-v2 "$temporary" \
    >/dev/null || control_status=$?
[ "$control_status" -eq 0 ] ||
    die "bootstrap timing receipt is not controlled (status $control_status)"

gate_status=0
if [ -r "$baseline" ]; then
    "$gate" "$baseline" "$temporary" || gate_status=$?
    case $gate_status in
    0) gate_result=pass ;;
    1) gate_result=trial-trip ;;
    *) die "bootstrap timing gate infrastructure failed (status $gate_status)" ;;
    esac
else
    "$gate" "$baseline" "$temporary"
    gate_result=warmup
fi
{
    echo "fleet.nightly_stamp=$stamp"
    echo "fleet.bootstrap_time_gate=$gate_result"
    echo 'fleet.bootstrap_baseline_mutated=no'
} >>"$temporary"
mv "$temporary" "$result"
trap - EXIT HUP INT TERM
echo "fleet-bootstrap: wrote $result (gate=$gate_result; baseline unchanged)"
exit "$gate_status"
