#!/bin/sh
set -eu

LC_ALL=C
export LC_ALL

root=$(CDPATH='' cd "$(dirname "$0")/../../.." && pwd -P)
nightly=${1:-$root/scripts/fleet-nightly.sh}
installer=${2:-$root/scripts/install-fleet-perf-schedule.sh}
fixtures=$root/tests/scripts/gates/fixtures/fleet
tmp=${TMPDIR:-/tmp}/cgf-fleet-nightly-test.$$
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
mkdir -p "$tmp/checkout/.git" "$tmp/checkout/.benchmarks/runs" "$tmp/home"

fail()
{
    echo "fleet_nightly_test: $*" >&2
    exit 1
}

run_nightly()
{
    run_stamp=$1
    run_bench_status=$2
    run_perf_status=$3
    run_push=$4
    run_fail_once=$5
    run_host=$6
    run_system=$7
    run_machine=$8
    run_runtime_trip=${9:-no}
    run_nix_include=${10:-}
    run_nix_crt=${11:-}
    FIXTURE_SYSTEM=$run_system FIXTURE_MACHINE=$run_machine \
    FIXTURE_GIT_LOG=$tmp/git.log FIXTURE_GIT_STAGED=$tmp/staged \
    FIXTURE_PUSH_STATE=$tmp/push-state FIXTURE_PUSH_FAIL_ONCE=$run_fail_once \
    FIXTURE_MAKE_LOG=$tmp/make.log \
    FIXTURE_BENCH_STATUS=$run_bench_status FIXTURE_PERF_STATUS=$run_perf_status \
    FIXTURE_RUNTIME_TRIP=$run_runtime_trip \
    FIXTURE_BENCH_ENV_LOG=${FIXTURE_BENCH_ENV_LOG:-} \
    CGF_FLEET_HOST=$run_host CGF_FLEET_STAMP=$run_stamp \
    CGF_FLEET_PUSH=$run_push CGF_FLEET_CHECKOUT=$tmp/checkout \
    CGF_FLEET_GIT_CMD=$fixtures/fake-git.sh \
    CGF_FLEET_MAKE_CMD=$fixtures/fake-make.sh \
    CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
    CGF_FLEET_DATE_CMD=$fixtures/fake-date.sh \
    CGF_FLEET_CC=/bin/true \
    CGF_FLEET_NIX_INCLUDE_DIR=$run_nix_include \
    CGF_FLEET_NIX_CRT_DIR=$run_nix_crt \
    CGF_FLEET_BENCH=$fixtures/fake-fleet-bench.sh \
    CGF_FLEET_PERF=$fixtures/fake-fleet-perf.sh \
        "$nightly"
}

: >"$tmp/git.log"
run_nightly 2026-08-10T120000Z 0 0 1 1 kasumi Linux x86_64 >"$tmp/pass.out" 2>"$tmp/pass.err"
compile=$tmp/checkout/.benchmarks/runs/2026-08-10T120000Z-kasumi.txt
runtime=$tmp/checkout/.benchmarks/runs/2026-08-10T120000Z-kasumi-kernels.txt
[ -s "$compile" ] && [ -s "$runtime" ] || fail "nightly did not produce both artifacts"
grep -F 'fleet.nightly_stamp=2026-08-10T120000Z' "$compile" >/dev/null ||
    fail "compile artifact lacks shared stamp"
grep -F 'fleet.nightly_stamp=2026-08-10T120000Z' "$runtime" >/dev/null ||
    fail "runtime artifact lacks shared stamp"
[ "$(grep -c 'push origin trunk' "$tmp/git.log")" -eq 2 ] ||
    fail "push did not perform exactly one retry"
grep -F 'pull --rebase origin trunk' "$tmp/git.log" >/dev/null ||
    fail "push retry did not rebase safely"
grep -F 'CC=/bin/true build/cgfried build/timeit' "$tmp/make.log" >/dev/null ||
    fail "Linux portable build targets are wrong"
grep -F 'gate-trip=no' "$tmp/git.log" >/dev/null || fail "clean commit did not record gate state"

: >"$tmp/git.log"
run_nightly 2026-08-10T123000Z 0 0 0 0 kasumi Linux x86_64 yes \
    >"$tmp/trial-trip.out" 2>"$tmp/trial-trip.err"
grep -F 'gate-trip=yes' "$tmp/git.log" >/dev/null ||
    fail "non-blocking trial trip was not recorded in the commit"
grep -F 'non-blocking trial runtime trip recorded' "$tmp/trial-trip.out" >/dev/null ||
    fail "scheduler did not report its successful trial trip"

: >"$tmp/git.log"
run_nightly 2026-08-10T130000Z 0 0 0 0 nomad-1 Darwin arm64 \
    >"$tmp/macos.out" 2>"$tmp/macos.err"
grep -F 'CC=/bin/true build/cgfried build/timeit tools' "$tmp/make.log" >/dev/null ||
    fail "macOS portable build did not include compiler, timer, and bundled tools"
grep -F 'submodule update --init afs-as afs-ld' "$tmp/git.log" >/dev/null ||
    fail "macOS nightly did not initialize its tool submodules"

mkdir -p "$tmp/nix/include" "$tmp/nix/lib"
: >"$tmp/nix/lib/crt1.o"
: >"$tmp/git.log"
FIXTURE_BENCH_ENV_LOG=$tmp/nix-env.log \
run_nightly 2026-08-10T140000Z 0 0 0 0 hasu Linux x86_64 no \
    "$tmp/nix/include" "$tmp/nix/lib" >"$tmp/nix.out" 2>"$tmp/nix.err"
grep -F "CGF_BENCH_CGF=$tmp/checkout/scripts/fleet-cgf-sysroot.sh" \
    "$tmp/nix-env.log" >/dev/null || fail "NixOS bench wrapper was not exported"
grep -F "CGF_KERNEL_CGF=$tmp/checkout/scripts/fleet-cgf-sysroot.sh" \
    "$tmp/nix-env.log" >/dev/null || fail "NixOS runtime wrapper was not exported"
grep -F "CGF_FLEET_REAL_CGF=$tmp/checkout/build/cgfried" \
    "$tmp/nix-env.log" >/dev/null || fail "NixOS real compiler path was not exported"
nix_sysroot=$(sed -n 's/^CGF_FLEET_SYSROOT=//p' "$tmp/nix-env.log")
[ -n "$nix_sysroot" ] || fail "NixOS fleet sysroot was not exported"
[ "$(readlink "$nix_sysroot/usr/include")" = "$tmp/nix/include" ] ||
    fail "NixOS include link is wrong"
[ "$(readlink "$nix_sysroot/usr/lib/x86_64-linux-gnu")" = "$tmp/nix/lib" ] ||
    fail "NixOS library link is wrong"
for nix_artifact in \
    "$tmp/checkout/.benchmarks/runs/2026-08-10T140000Z-hasu.txt" \
    "$tmp/checkout/.benchmarks/runs/2026-08-10T140000Z-hasu-kernels.txt"; do
    grep -F "sysroot_include=$tmp/nix/include" "$nix_artifact" >/dev/null ||
        fail "NixOS artifact omitted include-store provenance"
    grep -F "sysroot_crt=$tmp/nix/lib" "$nix_artifact" >/dev/null ||
        fail "NixOS artifact omitted CRT-store provenance"
done
FIXTURE_CGF_ARGV_LOG=$tmp/nix-cgf-argv.log \
CGF_FLEET_REAL_CGF=$fixtures/fake-cgf-argv.sh \
CGF_FLEET_SYSROOT=$nix_sysroot \
    "$root/scripts/fleet-cgf-sysroot.sh" -S 'path with spaces.c'
{
    echo "--sysroot=$nix_sysroot"
    echo '-S'
    echo 'path with spaces.c'
} >"$tmp/nix-cgf-argv.expected"
cmp "$tmp/nix-cgf-argv.expected" "$tmp/nix-cgf-argv.log" >/dev/null ||
    fail "NixOS compiler wrapper did not preserve argv after the sysroot"

: >"$tmp/git.log"
set +e
run_nightly 2026-08-11T120000Z 1 0 0 0 kasumi Linux x86_64 >"$tmp/trip.out" 2>"$tmp/trip.err"
trip_status=$?
set -e
[ "$trip_status" -eq 1 ] || fail "genuine gate trip was not returned as status 1"
[ -s "$tmp/checkout/.benchmarks/runs/2026-08-11T120000Z-kasumi.txt" ] ||
    fail "gate-trip compile artifact was not preserved"
[ -s "$tmp/checkout/.benchmarks/runs/2026-08-11T120000Z-kasumi-kernels.txt" ] ||
    fail "gate-trip runtime artifact was not preserved"
grep -F 'gate-trip=yes' "$tmp/git.log" >/dev/null || fail "trip commit was not recorded"

: >"$tmp/git.log"
set +e
run_nightly 2026-08-12T120000Z 3 0 0 0 kasumi Linux x86_64 >"$tmp/infra.out" 2>"$tmp/infra.err"
infra_status=$?
set -e
[ "$infra_status" -eq 3 ] || fail "infrastructure error did not fail with status 3"
grep -F 'compile benchmark infrastructure failed' "$tmp/infra.err" >/dev/null ||
    fail "infrastructure failure diagnostic is missing"
! grep -F 'commit ' "$tmp/git.log" >/dev/null ||
    fail "infrastructure failure was committed"

HOME=$tmp/home XDG_CONFIG_HOME=$tmp/config FIXTURE_SYSTEM=Linux \
FIXTURE_MACHINE=x86_64 CGF_FLEET_INSTALL_DRY_RUN=1 \
CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
    "$installer" kasumi >"$tmp/systemd.out"
grep -F 'OnCalendar=*-*-* 01:15:00 UTC' "$tmp/systemd.out" >/dev/null ||
    fail "kasumi systemd stagger is wrong"
grep -F 'systemctl --user enable --now' "$tmp/systemd.out" >/dev/null ||
    fail "systemd user activation is missing"
[ ! -e "$tmp/config/systemd/user/cgfried-fleet-perf.timer" ] ||
    fail "systemd dry run wrote a timer"

HOME=$tmp/home FIXTURE_SYSTEM=Darwin FIXTURE_MACHINE=arm64 \
CGF_FLEET_INSTALL_DRY_RUN=1 CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
    "$installer" nomad-1 >"$tmp/launchd.out"
grep -F '<key>Minute</key><integer>55</integer>' "$tmp/launchd.out" >/dev/null ||
    fail "nomad-1 LaunchAgent stagger is wrong"
grep -F 'launchctl bootstrap gui/UID' "$tmp/launchd.out" >/dev/null ||
    fail "LaunchAgent user activation is missing"
[ ! -e "$tmp/home/Library/LaunchAgents/com.tenseleyflow.cgfried-fleet-perf.plist" ] ||
    fail "LaunchAgent dry run wrote a plist"

: >"$tmp/scheduler.log"
HOME=$tmp/home XDG_CONFIG_HOME=$tmp/config-install FIXTURE_SYSTEM=Linux \
FIXTURE_MACHINE=x86_64 FIXTURE_SCHEDULER_LOG=$tmp/scheduler.log \
CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
CGF_FLEET_SYSTEMCTL_CMD=$fixtures/fake-systemctl.sh \
    "$installer" hasu >"$tmp/systemd-install.out"
[ -s "$tmp/config-install/systemd/user/cgfried-fleet-perf.service" ] &&
[ -s "$tmp/config-install/systemd/user/cgfried-fleet-perf.timer" ] ||
    fail "Linux user units were not installed"
grep -F -- '--user enable --now cgfried-fleet-perf.timer' "$tmp/scheduler.log" >/dev/null ||
    fail "Linux user timer was not enabled"

: >"$tmp/scheduler.log"
HOME=$tmp/home-install FIXTURE_SYSTEM=Darwin FIXTURE_MACHINE=arm64 \
FIXTURE_SCHEDULER_LOG=$tmp/scheduler.log \
CGF_FLEET_UNAME_CMD=$fixtures/fake-uname.sh \
CGF_FLEET_LAUNCHCTL_CMD=$fixtures/fake-launchctl.sh \
CGF_FLEET_ID_CMD=$fixtures/fake-id.sh \
    "$installer" nomad-1 >"$tmp/launchd-install.out"
[ -s "$tmp/home-install/Library/LaunchAgents/com.tenseleyflow.cgfried-fleet-perf.plist" ] ||
    fail "LaunchAgent was not installed"
grep -F 'bootstrap gui/501' "$tmp/scheduler.log" >/dev/null ||
    fail "LaunchAgent was not bootstrapped in the user domain"

mkdir -p "$tmp/bench-root/.git" "$tmp/bench-root/.benchmarks/runs"
printf 'baseline\n' >"$tmp/bench-root/.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt"
: >"$tmp/git.log"
set +e
FIXTURE_GIT_LOG=$tmp/git.log FIXTURE_GIT_STAGED=$tmp/staged \
FIXTURE_BENCHMARK_GATE_STATUS=1 CGF_FLEET_ROOT=$tmp/bench-root \
CGF_FLEET_HOST=kasumi CGF_FLEET_STAMP=2026-08-13T120000Z \
CGF_FLEET_COMMIT=0 CGF_FLEET_GIT_CMD=$fixtures/fake-git.sh \
CGF_FLEET_BENCH_SCRIPT=$fixtures/fake-bench-measure.sh \
CGF_FLEET_BENCHMARK_GATE=$fixtures/fake-benchmark-gate.sh \
    "$root/scripts/fleet-bench.sh" >"$tmp/fleet-bench.out" 2>"$tmp/fleet-bench.err"
fleet_bench_status=$?
set -e
[ "$fleet_bench_status" -eq 1 ] || fail "fleet-bench did not preserve gate status 1"
grep -F 'fleet.gate=trip' \
    "$tmp/bench-root/.benchmarks/runs/2026-08-13T120000Z-kasumi.txt" >/dev/null ||
    fail "fleet-bench artifact did not record its gate trip"
! grep -F 'commit ' "$tmp/git.log" >/dev/null ||
    fail "fleet-bench committed despite CGF_FLEET_COMMIT=0"

rm -f "$tmp/bench-root/.benchmarks/baseline-x86_64-linux-gnu.kasumi.txt" \
    "$tmp/bench-root/.benchmarks/runs/2026-08-13T120000Z-kasumi.txt"
: >"$tmp/git.log"
FIXTURE_GIT_LOG=$tmp/git.log FIXTURE_GIT_STAGED=$tmp/staged \
FIXTURE_BENCHMARK_GATE_STATUS=3 \
CGF_FLEET_ROOT=$tmp/bench-root CGF_FLEET_HOST=kasumi \
CGF_FLEET_STAMP=2026-08-13T120000Z CGF_FLEET_COMMIT=0 \
CGF_FLEET_GIT_CMD=$fixtures/fake-git.sh \
CGF_FLEET_BENCH_SCRIPT=$fixtures/fake-bench-measure.sh \
CGF_FLEET_BENCHMARK_GATE=$fixtures/fake-benchmark-gate.sh \
    "$root/scripts/fleet-bench.sh" >"$tmp/fleet-warmup.out" 2>"$tmp/fleet-warmup.err"
grep -F 'compile benchmark warmup: host=kasumi target=x86_64-linux-gnu baseline=missing; gate not run' \
    "$tmp/fleet-warmup.out" >/dev/null || fail "missing compile warmup report"
grep -F 'fleet.gate=warmup' \
    "$tmp/bench-root/.benchmarks/runs/2026-08-13T120000Z-kasumi.txt" >/dev/null ||
    fail "compile warmup artifact did not record warmup state"

echo 'fleet_nightly_test: atomic artifacts, warmup, push retry, gate preservation, infrastructure failure, and user schedules passed'
