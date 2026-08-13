#!/bin/sh
set -eu
LC_ALL=C
export LC_ALL

ROOT=$(CDPATH='' cd "$(dirname "$0")/../.." && pwd -P)
WORK_ROOT=${CGF_BENCH_SCRIPT_TEST_WORK:-$ROOT/build/bench-script-test}
mkdir -p "$WORK_ROOT"
WORK=$(mktemp -d "$WORK_ROOT/run.XXXXXX")
trap 'rm -rf "$WORK"' EXIT HUP INT TERM

fail()
{
    echo "bench_script_test: $*" >&2
    exit 1
}

run_target()
{
    target=$1
    name=$2
    run_work=$WORK/$name
    mkdir "$run_work" "$run_work/tmp"
    : >"$run_work/argv.log"
    run_status=0
    FIXTURE_CGF_TARGET=$target FIXTURE_CGF_LOG=$run_work/argv.log \
        TMPDIR=$run_work/tmp CGF_FLEET_HOST=bench-test \
        CGF_BENCH_FORCE=${FIXTURE_BENCH_FORCE:-1} CGF_BENCH_RUNS=1 \
        CGF_BENCH_WARMUP=0 CGF_BENCH_SELF_LIMIT=1 \
        CGF_BENCH_TU_COUNT=1 CGF_BENCH_TU_LINES=32 \
        CGF_BENCH_SKIP_MUSL=1 CGF_BENCH_CGF=$FIXTURES/fake-cgf.sh \
        CGF_BENCH_TIMEIT=$FIXTURES/fake-timeit.sh \
        CGF_BENCH_CONTROL_SCRIPT=${FIXTURE_CONTROL_SCRIPT:-$FIXTURES/fake-bench-control.sh} \
        CGF_BENCH_HOST_CLASS=${FIXTURE_HOST_CLASS:-} \
        BENCH_SKIP_TIME=${FIXTURE_BENCH_SKIP_TIME:-0} \
        FIXTURE_CONTROL_STATUS=${FIXTURE_CONTROL_STATUS:-0} \
        FIXTURE_CONTROL_LOG=$run_work/control.log \
        CGF_FLEET_SYSROOT_INCLUDE=${FIXTURE_SYSROOT_INCLUDE:-} \
        CGF_FLEET_SYSROOT_CRT=${FIXTURE_SYSROOT_CRT:-} \
        CGF_BENCH_WORK=$run_work/work \
        CGF_BENCH_RESULTS=$run_work/results.txt \
        sh "$ROOT/scripts/bench.sh" >/dev/null 2>"$run_work/stderr" ||
        run_status=$?
    [ -z "$(find "$run_work/tmp" -mindepth 1 -maxdepth 1 -print)" ] ||
        fail "control provenance temporary files leaked"
    return "$run_status"
}

FIXTURES=$ROOT/tests/bench/fixtures/bench-script
run_target arm64-macos macos

[ "$(grep -c '^BEGIN$' "$WORK/macos/argv.log")" -eq 3 ] ||
    fail "arm64 macOS run did not measure exactly sqlite3, self, and many-tu"
grep -Fx -- '-include' "$WORK/macos/argv.log" >/dev/null ||
    fail "arm64 macOS SQLite lane did not force the compatibility header"
[ "$(grep -Fxc "$ROOT/tests/bench/compat/arm64-macos-syntax.h" \
    "$WORK/macos/argv.log")" -eq 1 ] ||
    fail "SQLite compatibility header was not scoped to one lane"
[ "$(grep -Fxc "$ROOT/tests/bench/compat/arm64-macos-self-syntax.h" \
    "$WORK/macos/argv.log")" -eq 1 ] ||
    fail "self compatibility header was not scoped to one lane"
[ "$(grep -Fxc "$ROOT/tests/bench/compat/arm64-macos-self-overlay" \
    "$WORK/macos/argv.log")" -eq 1 ] ||
    fail "self SDK overlay was not scoped to one lane"
if grep -Eq '^[[:space:]]*#[[:space:]]*include' \
    "$ROOT/tests/bench/compat/arm64-macos-self-syntax.h"; then
    fail "forced self compatibility header expanded the measured include surface"
fi
grep -Fx 'sqlite3.status=measured' "$WORK/macos/results.txt" >/dev/null ||
    fail "SQLite lane was not measured"
grep -Fx 'self.status=measured' "$WORK/macos/results.txt" >/dev/null ||
    fail "self lane was not measured"
grep -F ':1-files:arm64-macos-self-sdk-syntax-v2' \
    "$WORK/macos/results.txt" >/dev/null || fail "self compatibility provenance missing"
grep -Fx 'many-tu.status=measured' "$WORK/macos/results.txt" >/dev/null ||
    fail "many-tu lane was not measured"
grep -Fx 'sqlite3.corpus=sqlite-amalgamation-3500400:arm64-macos-sdk-syntax-v1' \
    "$WORK/macos/results.txt" >/dev/null || fail "compatibility provenance missing"
FIXTURE_SYSROOT_INCLUDE=/nix/store/fixture-glibc-dev/include \
FIXTURE_SYSROOT_CRT=/nix/store/fixture-glibc/lib \
    run_target x86_64-linux-gnu linux
if grep -F 'arm64-macos-syntax.h' "$WORK/linux/argv.log" >/dev/null; then
    fail "arm64 macOS compatibility leaked into the Linux benchmark"
fi
grep -Fx 'sqlite3.corpus=sqlite-amalgamation-3500400' \
    "$WORK/linux/results.txt" >/dev/null || fail "ordinary SQLite provenance changed"
grep -Fx 'sysroot_include=/nix/store/fixture-glibc-dev/include' \
    "$WORK/linux/results.txt" >/dev/null || fail "compile include provenance missing"
grep -Fx 'sysroot_crt=/nix/store/fixture-glibc/lib' \
    "$WORK/linux/results.txt" >/dev/null || fail "compile CRT provenance missing"
grep -Fx 'musl.status=separate-trial-lane' "$WORK/linux/results.txt" >/dev/null ||
    fail "compile receipt does not route musl through its separate trial lane"
grep -Fx 'musl.gate=ci/gates.d/musl-full-build.conf' \
    "$WORK/linux/results.txt" >/dev/null || fail "musl gate provenance is missing"
grep -Fx 'musl.reach=scripts/fleet-musl-build.sh' \
    "$WORK/linux/results.txt" >/dev/null || fail "musl fleet entry point is missing"
for field in power_profile scaling_driver energy_performance_preference; do
    [ "$(grep -c "^$field=" "$WORK/linux/results.txt")" -eq 1 ] ||
        fail "Linux $field provenance was not emitted exactly once"
done
for field_value in control_protocol=fleet-control-v2 \
    logical_cpus=8 cpu_idle_pct=99.00 load1=0.10; do
    [ "$(grep -Fxc "$field_value" "$WORK/linux/results.txt")" -eq 1 ] ||
        fail "v2 control provenance missing: $field_value"
done
for field in host governor power_profile scaling_driver \
    energy_performance_preference control_protocol logical_cpus cpu_idle_pct \
    load1; do
    [ "$(grep -c "^$field=" "$WORK/linux/control.log")" -eq 1 ] ||
        fail "classifier input did not contain exactly one $field"
done

FIXTURE_CONTROL_STATUS=1 run_target x86_64-linux-gnu forced
grep -F 'WARNING: capacity/idle controls are provenance-only; forced recording enabled' \
    "$WORK/forced/stderr" >/dev/null || fail "forced provenance-only warning is missing"

set +e
FIXTURE_CONTROL_STATUS=1 FIXTURE_BENCH_FORCE=0 \
    run_target x86_64-linux-gnu refused
refused_status=$?
set -e
[ "$refused_status" -eq 3 ] || fail "unforced provenance-only run was not refused"
grep -F 'capacity/idle controls are provenance-only (set CGF_BENCH_FORCE=1 to record)' \
    "$WORK/refused/stderr" >/dev/null || fail "capacity/idle refusal diagnostic is missing"

set +e
FIXTURE_CONTROL_STATUS=3 run_target x86_64-linux-gnu malformed
malformed_status=$?
set -e
[ "$malformed_status" -eq 3 ] || fail "malformed control provenance did not fail"
grep -F 'malformed v2 capacity/idle provenance' "$WORK/malformed/stderr" \
    >/dev/null || fail "malformed control diagnostic is missing"

for shared_class in ci shared-ci arm64-ci; do
    shared_target=x86_64-linux-gnu
    [ "$shared_class" != arm64-ci ] || shared_target=arm64-linux
    shared_name=shared-$shared_class
    FIXTURE_HOST_CLASS=$shared_class \
    FIXTURE_BENCH_SKIP_TIME=1 \
    FIXTURE_CONTROL_SCRIPT=$WORK/missing-control \
        run_target "$shared_target" "$shared_name"
    grep -Fx "host_class=$shared_class" "$WORK/$shared_name/results.txt" \
        >/dev/null || fail "$shared_class host class provenance is missing"
    grep -E '^load1=([0-9]+([.][0-9]+)?|unknown)$' \
        "$WORK/$shared_name/results.txt" >/dev/null ||
        fail "$shared_class load provenance is missing or malformed"
    for field in control_protocol logical_cpus cpu_idle_pct; do
        if grep -q "^$field=" "$WORK/$shared_name/results.txt"; then
            fail "$shared_class invented fleet-only $field provenance"
        fi
    done
    grep -F 'shared-runner controls are provenance-only; timing gates must remain disabled' \
        "$WORK/$shared_name/stderr" >/dev/null ||
        fail "$shared_class provenance-only warning is missing"
done

set +e
FIXTURE_HOST_CLASS=ci FIXTURE_BENCH_SKIP_TIME=0 \
FIXTURE_CONTROL_SCRIPT=$WORK/missing-control \
    run_target x86_64-linux-gnu shared-time-enabled
shared_time_status=$?
set -e
[ "$shared_time_status" -eq 3 ] ||
    fail "a shared runner recorded with timing gates enabled"
grep -F "shared-runner host class 'ci' requires BENCH_SKIP_TIME=1" \
    "$WORK/shared-time-enabled/stderr" >/dev/null ||
    fail "shared-runner timing refusal diagnostic is missing"

set +e
FIXTURE_HOST_CLASS=other-ci FIXTURE_BENCH_SKIP_TIME=1 \
FIXTURE_CONTROL_SCRIPT=$WORK/missing-control \
    run_target x86_64-linux-gnu unknown-class
unknown_class_status=$?
set -e
[ "$unknown_class_status" -eq 3 ] ||
    fail "an arbitrary host class bypassed the fleet controller"
grep -F 'control helper is not executable' "$WORK/unknown-class/stderr" \
    >/dev/null || fail "arbitrary host-class refusal diagnostic is missing"

echo 'bench_script_test: fleet controls, shared-CI provenance, measured lanes, and macOS shim are target-scoped'
