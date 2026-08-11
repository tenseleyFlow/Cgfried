#!/bin/sh
# Sprint 53 correctness gate: build and execute every kernel at every OPT_EQ
# level through the arm64-linux backend.  REPS is deliberately reduced: this
# lane proves checksums and optimization equivalence, while fleet runs own
# statistically meaningful timing.
set -eu

prog=kernel_arm64_exec
script_dir=$(dirname -- "$0")
repo=$(CDPATH='' cd -- "$script_dir/.." && pwd)
compiler=${1:-$repo/build/cgfried}
runner=${2:-$repo/build/cgf-test}
work=${CGF_KERNEL_A64_EXEC_WORK:-$repo/build/kernel-arm64-exec}
reps=${CGF_KERNEL_EXEC_REPS:-1}
host=$(uname -m 2>/dev/null || echo unknown)
system=$(uname -s 2>/dev/null || echo unknown)
kernel_count=$(find "$repo/tests/bench/kernels" -maxdepth 1 -type f -name '*.c' |
    wc -l | tr -d ' ')
execution_count=$((kernel_count * 6))

case "$compiler" in
/*) ;;
*) compiler=$repo/$compiler ;;
esac
case "$runner" in
/*) ;;
*) runner=$repo/$runner ;;
esac

die()
{
    echo "$prog: $*" >&2
    exit 1
}

skip()
{
    echo "HARNESS_SKIP suite=kernel-arm64-exec test=opt-eq count=$execution_count reason=\"$*\""
    exit 0
}

[ -x "$compiler" ] || die "compiler is not executable: $compiler"
[ -x "$runner" ] || die "test runner is not executable: $runner"
[ "$kernel_count" -ge 19 ] ||
    die "expected at least 19 kernels, found $kernel_count"
case "$reps" in
'' | *[!0-9]*) die "CGF_KERNEL_EXEC_REPS must be a positive integer" ;;
0) die "CGF_KERNEL_EXEC_REPS must be greater than zero" ;;
esac

mkdir -p "$work"
wrapper=$work/cgf-arm64-linux

case "$host:$system" in
aarch64:Linux | arm64:Linux)
    # The native compiler already selects arm64-linux. Force system gas when
    # the bundled assembler has not been built, matching the normal corpus
    # lane's tool-discovery contract.
    command -v as >/dev/null 2>&1 || skip "missing native assembler as"
    command -v ld >/dev/null 2>&1 || skip "missing native linker ld"
    cat >"$wrapper" <<EOF
#!/bin/sh
exec env CGF_AS=0 '$compiler' --target=arm64-linux -DREPS=$reps "\$@"
EOF
    ;;
*)
    as_name=${CGF_KERNEL_A64_AS:-aarch64-linux-gnu-as}
    ld_name=${CGF_KERNEL_A64_LD:-aarch64-linux-gnu-ld}
    sysroot=${CGF_QEMU_SYSROOT:-/usr/aarch64-linux-gnu}
    as_path=$(command -v "$as_name" 2>/dev/null || true)
    ld_path=$(command -v "$ld_name" 2>/dev/null || true)
    [ -n "$as_path" ] || skip "missing $as_name"
    [ -n "$ld_path" ] || skip "missing $ld_name"
    [ -d "$sysroot" ] || skip "missing arm64 sysroot $sysroot"
    [ -d "$sysroot/include" ] || skip "missing arm64 headers $sysroot/include"
    [ -d "$sysroot/lib" ] || skip "missing arm64 runtime $sysroot/lib"
    if [ -z "${CGF_QEMU:-}" ] &&
        ! command -v qemu-aarch64-static >/dev/null 2>&1 &&
        ! command -v qemu-aarch64 >/dev/null 2>&1; then
        skip "missing qemu-aarch64"
    fi
    export CGF_QEMU_SYSROOT="$sysroot"
    cat >"$wrapper" <<EOF
#!/bin/sh
CGF_AS_PATH='$as_path' \
CGF_LD_PATH='$ld_path' \
CGF_CRT_DIR='$sysroot/lib' \
exec '$compiler' --target=arm64-linux -isystem '$sysroot/include' \
    -DREPS=$reps "\$@"
EOF
    ;;
esac
chmod +x "$wrapper"

# Catch routing failures once, with a focused diagnostic, before expanding
# them into 126 noisy per-level failures.
printf 'int main(void) { return 42; }\n' >"$work/smoke.c"
if ! "$wrapper" "$work/smoke.c" -o "$work/smoke" 2>"$work/smoke.err"; then
    echo "$prog: wrapper failed to compile the smoke program" >&2
    head -20 "$work/smoke.err" >&2
    exit 1
fi
if "$repo/scripts/qemu-run.sh" "$work/smoke"; then
    smoke_status=0
else
    smoke_status=$?
fi
[ "$smoke_status" -eq 42 ] ||
    die "smoke program exited $smoke_status, expected 42"

CGF_TEST_CC="$wrapper" \
CGF_TEST_WORK="$work/test-work" \
CGF_TEST_RUN="$repo/scripts/qemu-run.sh" \
CGF_TEST_TARGET=arm64-linux \
CGF_TEST_TIMEOUT=${CGF_KERNEL_A64_TIMEOUT:-120} \
"$runner" --profile linux-arm64 "$repo/tests/bench/kernels"
