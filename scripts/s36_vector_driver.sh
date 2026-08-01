#!/bin/sh
set -eu

CC=${1:?usage: s36_vector_driver.sh <cgfried> <cgf-test>}
RUNNER=${2:?usage: s36_vector_driver.sh <cgfried> <cgf-test>}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-s36-vector.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

fail()
{
    echo "s36_vector_driver: $*" >&2
    exit 1
}

"$CC" --help >"$work/help"
grep -F -- "-ffast-math" "$work/help" >/dev/null ||
    fail "--help omits -ffast-math"
grep -F -- "docs/fast-math.md" "$work/help" >/dev/null ||
    fail "--help omits the fast-math policy link"

CGF_TEST_CC="$CC" "$RUNNER" \
    tests/programs/driver/fast_math_component_warns.c \
    tests/programs/driver/fast_math_component_werror.c \
    tests/programs/headers/iec559_undefined.c \
    tests/programs/headers/iec559_fast_flag_undefined.c \
    tests/programs/opt/s36_fast_math_identities.cgfir \
    tests/programs/opt/s36_fast_math_order_disable.cgfir \
    tests/programs/opt/s36_fast_math_order_reset.cgfir \
    tests/programs/opt/s36_fast_math_component_keeps_bundle.cgfir \
    tests/programs/opt/s36_vector_map.cgfir \
    tests/programs/opt/s36_vector_reduce.cgfir \
    tests/programs/opt/s36_vector_runtime.c \
    tests/programs/opt/s36_vector_integer_reductions.c \
    tests/programs/opt/s36_vector_fp_reductions.c \
    tests/programs/opt/s36_vector_overlap.c \
    tests/programs/opt/s36_vector_source_integer_ir.c \
    tests/programs/opt/s36_vector_source_fp_ir.c \
    tests/programs/opt/s36_vector_lane_iv_bail.c \
    tests/programs/opt/s36_vector_scan_bail.c \
    tests/programs/opt/s36_vector_trip_runtime.c

"$CC" -O3 -emit-ir tests/programs/opt/s36_vector_runtime.c \
    >"$work/runtime.ir"
grep -F "load v4i32" "$work/runtime.ir" >/dev/null ||
    fail "trip-1001 source map did not vectorize"
"$CC" -O3 -emit-ir tests/programs/opt/s36_vector_trip_runtime.c \
    >"$work/trips.ir"
trip_vectors=$(grep -c 'load v4i32' "$work/trips.ir" || true)
[ "$trip_vectors" -ge 4 ] ||
    fail "trips 8/9/1000/1001 did not all vectorize"

"$CC" -O3 -emit-ir tests/programs/opt/s36_vector_integer_reductions.c \
    >"$work/integer.ir"
integer_adds=$(grep -c 'vreduce_add' "$work/integer.ir" || true)
integer_ands=$(grep -c 'vreduce_and' "$work/integer.ir" || true)
integer_ors=$(grep -c 'vreduce_or' "$work/integer.ir" || true)
integer_xors=$(grep -c 'vreduce_xor' "$work/integer.ir" || true)
[ "$integer_adds" -ge 2 ] && [ "$integer_ands" -ge 1 ] &&
    [ "$integer_ors" -ge 1 ] && [ "$integer_xors" -ge 1 ] ||
    fail "source integer reduction family is incomplete"

"$CC" -O3 -emit-ir tests/programs/opt/s36_vector_fp_reductions.c \
    >"$work/fp-strict.ir"
if grep -F "vreduce_" "$work/fp-strict.ir" >/dev/null; then
    fail "strict FP reduction vectorized without reassociation license"
fi
"$CC" -Ofast -emit-ir tests/programs/opt/s36_vector_fp_reductions.c \
    >"$work/fp-fast.ir"
grep -F "vreduce_add" "$work/fp-fast.ir" >/dev/null ||
    fail "Ofast source FP add reduction did not vectorize"
grep -F "vreduce_mul" "$work/fp-fast.ir" >/dev/null ||
    fail "Ofast source FP multiply reduction did not vectorize"

CGF_OPT_BAIL_LOG=1 "$CC" -O3 -emit-ir \
    tests/programs/opt/s36_vector_overlap.c >"$work/overlap.ir" \
    2>"$work/overlap.err"
grep -F "vec_alias_unproven" "$work/overlap.err" >/dev/null ||
    fail "overlap negative missed vec_alias_unproven"
if grep -E 'v(16i8|8i16|4i32|2i64|4f32|2f64)' \
    "$work/overlap.ir" >/dev/null; then
    fail "overlap negative emitted vector IR"
fi

for negative in lane_iv_bail scan_bail; do
    CGF_OPT_BAIL_LOG=1 "$CC" -O3 -emit-ir \
        "tests/programs/opt/s36_vector_$negative.c" \
        >"$work/$negative.ir" 2>"$work/$negative.err"
    grep -F "vec_body_op" "$work/$negative.err" >/dev/null ||
        fail "$negative missed vec_body_op"
done
if grep -E 'store v2i64|vsplat v2i64 i64 %' \
    "$work/lane_iv_bail.ir" >/dev/null; then
    fail "lane-varying induction was used as uniform vector data"
fi
if grep -E 'v(16i8|8i16|4i32|2i64|4f32|2f64)' \
    "$work/scan_bail.ir" >/dev/null; then
    fail "prefix-scan negative emitted vector IR"
fi

CGF_OPT_DISABLE_VECTORIZE=1 "$CC" -O3 -emit-ir \
    tests/programs/opt/s36_vector_map.cgfir >"$work/disabled.ir"
if grep -E 'v(16i8|8i16|4i32|2i64|4f32|2f64)' \
    "$work/disabled.ir" >/dev/null; then
    fail "CGF_OPT_DISABLE_VECTORIZE did not preserve scalar IR"
fi

echo "s36_vector_driver: policy, runtime, reductions, remainders, alias bail, and disable control green"
