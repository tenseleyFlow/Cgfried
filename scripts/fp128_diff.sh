#!/bin/sh
# Sprint 49 DoD 3: the arm64 binary128 soft-float runtime, differentiated
# against libgcc.
#
# One probe, linked twice — once letting libgcc supply __addtf3 and friends,
# once with libcgf_rt.a ahead of it — and the two runs must print identical
# bytes. That makes libgcc the oracle for the VALUES and, just as important,
# for the CALLING CONTRACT: a runtime whose arguments arrive in the wrong
# registers links and runs without complaint. Same technique as
# scripts/rt_diff.sh, which has used it for the 128-bit integer routines
# since Sprint 28.
#
# It found three real defects on its first run:
#   - binary128 travels in a q register, not the x0/x1 pair, so declaring
#     the operands as a 16-byte struct read them from the wrong registers;
#   - sf_mul's 256-bit renormalization shifted by an out-of-range count,
#     which only binary128 could reach (1.0 * 1.0 came back as 2^64);
#   - sf_div broke out of its quotient loop when the divisor was an
#     unnormalized subnormal, so 1.0/DBL_MIN_SUBNORMAL was a large finite
#     number instead of infinity — in EVERY format, x86 included.
#
# The last two were compiler bugs, not runtime bugs: the same softfloat core
# folds constants. Both are pinned in tests/unit/test_softfp.c.

set -eu

native=${CGF_FP128_NATIVE:-0}
case $native in
0)
    CC=${CGF_A64_GCC:-aarch64-linux-gnu-gcc}
    NM=${CGF_A64_NM:-aarch64-linux-gnu-nm}
    runner=${CGF_QEMU:-qemu-aarch64-static}
    ;;
1)
    CC=${CGF_A64_GCC:-gcc}
    NM=${CGF_A64_NM:-nm}
    runner=
    ;;
*)
    echo 'fp128_diff: CGF_FP128_NATIVE must be 0 or 1' >&2
    exit 2
    ;;
esac
RUNTIME_CC=${CGF_FP128_RUNTIME_CC:-$CC}
work=${CGF_FP128_WORK:-build/fp128-diff}

missing=
command -v "$CC" >/dev/null 2>&1 || missing="$missing $CC"
command -v "$RUNTIME_CC" >/dev/null 2>&1 || missing="$missing $RUNTIME_CC"
if [ -n "$runner" ]; then
    command -v "$runner" >/dev/null 2>&1 || missing="$missing $runner"
fi
if [ -n "$missing" ]; then
    echo "fp128_diff: skipped (missing:$missing)"
    exit 0
fi

mkdir -p "$work"

# Build fp128.c plus the softfloat core it delegates to. By default this is
# the historical GCC cross lane; Sprint 58's native ARM job instead points
# RUNTIME_CC at its stage1 Cgfried, exercising the self-hosted `_Float128`
# carrier and its AAPCS64 calling convention against the same libgcc oracle.
for unit in rt/fp128 util/softfp util/bigint; do
    obj="$work/$(echo "$unit" | tr / _).o"
    CGF_AS=0 "$RUNTIME_CC" -std=c11 -O2 -Wall -Wextra -Werror \
        -fno-strict-aliasing -Isrc -c -o "$obj" "src/$unit.c"
done
ar rcsD "$work/libcgf_rt.a" "$work/rt_fp128.o" "$work/util_softfp.o" \
    "$work/util_bigint.o"

"$CC" -std=c11 -O1 -static -o "$work/probe_gcc" tests/tools/fp128_probe.c
"$CC" -std=c11 -O1 -static -o "$work/probe_cgf" tests/tools/fp128_probe.c \
    "$work/libcgf_rt.a"

run_probe()
{
    if [ -n "$runner" ]; then
        "$runner" "$1"
    else
        "$1"
    fi
}

run_probe "$work/probe_gcc" >"$work/out_gcc.txt"
run_probe "$work/probe_cgf" >"$work/out_cgf.txt"

if ! cmp -s "$work/out_gcc.txt" "$work/out_cgf.txt"; then
    echo "fp128_diff: libcgf_rt disagrees with libgcc:" >&2
    diff "$work/out_gcc.txt" "$work/out_cgf.txt" | head -40 >&2
    exit 1
fi

lines=$(wc -l <"$work/out_gcc.txt" | tr -d ' ')
test "$lines" -ge 1000

# Every entry point the sprint enumerates must actually be DEFINED here, or
# the probe could be agreeing simply because it fell through to libgcc.
if command -v "$NM" >/dev/null 2>&1; then
    for sym in __addtf3 __subtf3 __multf3 __divtf3 __eqtf2 __netf2 \
        __lttf2 __letf2 __gttf2 __getf2 __unordtf2 __extendsftf2 \
        __extenddftf2 __trunctfsf2 __trunctfdf2 __fixtfsi __fixtfdi \
        __fixunstfsi __fixunstfdi __floatsitf __floatditf __floatunsitf \
        __floatunditf __negtf2; do
        if ! "$NM" "$work/libcgf_rt.a" | grep -q "T $sym\$"; then
            echo "fp128_diff: libcgf_rt.a does not define $sym" >&2
            exit 1
        fi
    done
    echo "fp128_diff: 24 entry points, $lines result lines identical to libgcc (runtime compiler: $RUNTIME_CC)"
else
    echo "fp128_diff: $lines result lines identical to libgcc (runtime compiler: $RUNTIME_CC; $NM absent, symbol audit skipped)"
fi
