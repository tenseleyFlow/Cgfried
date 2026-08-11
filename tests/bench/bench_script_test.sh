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
    mkdir "$run_work"
    : >"$run_work/argv.log"
    FIXTURE_CGF_TARGET=$target FIXTURE_CGF_LOG=$run_work/argv.log \
        CGF_FLEET_HOST=bench-test CGF_BENCH_FORCE=1 CGF_BENCH_RUNS=1 \
        CGF_BENCH_WARMUP=0 CGF_BENCH_SELF_LIMIT=1 \
        CGF_BENCH_TU_COUNT=1 CGF_BENCH_TU_LINES=32 \
        CGF_BENCH_SKIP_MUSL=1 CGF_BENCH_CGF=$FIXTURES/fake-cgf.sh \
        CGF_BENCH_TIMEIT=$FIXTURES/fake-timeit.sh \
        CGF_FLEET_SYSROOT_INCLUDE=${FIXTURE_SYSROOT_INCLUDE:-} \
        CGF_FLEET_SYSROOT_CRT=${FIXTURE_SYSROOT_CRT:-} \
        CGF_BENCH_WORK=$run_work/work \
        CGF_BENCH_RESULTS=$run_work/results.txt \
        sh "$ROOT/scripts/bench.sh" >/dev/null 2>"$run_work/stderr"
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

echo 'bench_script_test: three measured lanes preserved; macOS shim is target-scoped'
