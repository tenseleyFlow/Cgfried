#!/bin/sh
# Publish one immutable controlled musl full-build artifact; never edit baselines.
set -eu

LC_ALL=C
export LC_ALL
prog=fleet-musl-build
root=${CGF_FLEET_MUSL_ROOT:-$(CDPATH='' cd "$(dirname "$0")/.." && pwd -P)}
host=${CGF_FLEET_HOST:-}
stamp=${CGF_FLEET_STAMP:-$(date -u '+%Y-%m-%dT%H%M%SZ')}
source=${CGF_FLEET_MUSL_SOURCE:-$root/.docs/refs/musl}
run_dir=${CGF_FLEET_MUSL_RUN_DIR:-$root/.benchmarks/runs}
result=$run_dir/$stamp-$host-musl-full-build.txt
control=${CGF_FLEET_MUSL_CONTROL:-$root/scripts/bench-control.sh}
bench=${CGF_FLEET_MUSL_BENCH:-$root/scripts/musl-full-build-bench.sh}
gate=${CGF_FLEET_MUSL_GATE:-$root/scripts/musl-full-build-gate.sh}
perf_gate=${CGF_FLEET_MUSL_PERF_GATE:-$root/scripts/perf_gate.sh}
config=${CGF_FLEET_MUSL_CONFIG:-$root/ci/gates.d/musl-full-build.conf}
uname_cmd=${CGF_FLEET_UNAME_CMD:-uname}
git_cmd=${CGF_FLEET_MUSL_GIT_CMD:-${CGF_FLEET_GIT_CMD:-git}}

die() { echo "$prog: $*" >&2; exit 3; }
case $host in kasumi | hasu) ;; *) die 'CGF_FLEET_HOST must explicitly name kasumi or hasu' ;; esac
case $stamp in [0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9][0-9][0-9][0-9][0-9]Z) ;; *) die 'malformed UTC stamp' ;; esac
for helper in "$control" "$bench" "$gate" "$perf_gate"; do [ -x "$helper" ] || die "helper is not executable: $helper"; done
[ -r "$config" ] || die "gate configuration is not readable: $config"
command -v "$git_cmd" >/dev/null 2>&1 || die "Git command is unavailable: $git_cmd"
[ "$($uname_cmd -s 2>/dev/null):$($uname_cmd -m 2>/dev/null)" = Linux:x86_64 ] ||
    die 'musl full-build lane requires Linux x86_64'
[ ! -e "$result" ] || die "refusing to overwrite immutable artifact: $result"
[ -d "$root/.git" ] || die "fleet root is not a Git checkout: $root"
tree_status=$("$git_cmd" -C "$root" status --porcelain --untracked-files=normal) ||
    die 'cannot inspect the Cgfried tree'
[ -z "$tree_status" ] || die 'Cgfried tree must be clean before measurement'
mkdir -p "$run_dir"
work=$root/build/fleet-musl-build-$stamp-$host
[ ! -e "$work" ] || die "refusing to reuse work directory: $work"
mkdir -p "$work"
failure=$root/build/fleet-musl-failure-$host
work_complete=no

finish_work()
{
    status=$?
    trap - EXIT HUP INT TERM
    [ ! -e "$work" ] || {
        [ "$work" = "$root/build/fleet-musl-build-$stamp-$host" ] || {
            echo "$prog: refusing to clean unexpected work path: $work" >&2
            exit 3
        }
        [ ! -L "$work" ] || {
            echo "$prog: refusing to clean symlinked work path: $work" >&2
            exit 3
        }
        if [ "$work_complete" = yes ]; then
            rm -rf "$work" || {
                echo "$prog: cannot remove completed work directory: $work" >&2
                exit 3
            }
        else
            [ "$failure" = "$root/build/fleet-musl-failure-$host" ] || exit 3
            if [ -e "$failure" ]; then
                [ -d "$failure" ] && [ ! -L "$failure" ] || {
                    echo "$prog: refusing to replace unexpected failure path: $failure" >&2
                    exit 3
                }
                # This is the previous generated failure bundle for this host;
                # replacing it bounds retained diagnostics to one failed run.
                rm -rf "$failure" || {
                    echo "$prog: cannot replace prior failure bundle: $failure" >&2
                    exit 3
                }
            fi
            mv "$work" "$failure" || {
                echo "$prog: cannot retain failed-run evidence: $failure" >&2
                exit 3
            }
            echo "$prog: retained failed-run evidence in $failure" >&2
        fi
    }
    exit "$status"
}
trap finish_work EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM
control_receipt=$work/control.txt
baseline=$root/.benchmarks/baseline-musl-full-build-x86_64-linux-musl.$host.txt
baseline_before=
if [ -r "$baseline" ]; then
    baseline_before=$(cksum "$baseline") || die 'cannot checksum the host baseline'
fi
{
    echo "host=$host"
    "$control" measure "$host"
} >"$control_receipt" || die 'control capture failed'
pending=$work/result.txt
date_utc=$(printf '%s\n' "$stamp" | sed 's/^\(....-..-..\)T\(..\)\(..\)\(..\)Z$/\1T\2:\3:\4Z/')
CGF_MUSL_BUILD_HOST=$host CGF_MUSL_BUILD_CONTROL_RECEIPT=$control_receipt \
CGF_MUSL_BUILD_DATE_UTC=$date_utc \
    "$bench" "$source" "$work/bench" "$pending" || die 'musl full-build measurement failed'

if [ -r "$baseline" ]; then
    gate_status=0
    gate_output=$work/gate.out
    "$perf_gate" "$config" -- "$gate" "$baseline" "$pending" >"$gate_output" || gate_status=$?
    [ "$gate_status" -eq 0 ] || die "trial gate infrastructure failed (status $gate_status)"
    [ "$(cksum "$baseline")" = "$baseline_before" ] || die 'host baseline changed during the fleet run'
    cat "$gate_output"
    if grep -F 'TRIP-RECORDED (trial;' "$gate_output" >/dev/null; then
        gate_state=trial-trip
    elif grep -F 'PASS (trial;' "$gate_output" >/dev/null; then
        gate_state=trial-pass
    else
        die 'trial wrapper returned an unrecognized result'
    fi
else
    gate_state=warmup
    echo "$prog: warmup: host=$host baseline=missing; gate not run"
fi
{
    echo "fleet.host=$host"
    echo "fleet.run_id=$stamp-$host-musl-full-build"
    echo 'fleet.baseline_mutated=no'
    echo "fleet.gate=$gate_state"
} >>"$pending"
mv "$pending" "$result" || die 'cannot publish the immutable dated artifact'
work_complete=yes
echo "$prog: wrote $result (baseline unchanged)"
