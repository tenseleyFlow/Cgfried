#!/bin/sh
set -eu

cc=${1:-build/cgfried}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-s34-loops.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

emit_ir()
{
    emit_level=$1
    emit_source=$2
    emit_name=$3
    shift 3
    CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
        "$cc" "$@" -emit-ir "$emit_level" "$emit_source" \
        >"$work/$emit_name.ir" 2>"$work/$emit_name.err"
}

build_run()
{
    run_level=$1
    run_source=$2
    run_name=$3
    CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
        "$cc" "$run_level" "$run_source" -o "$work/$run_name" \
        2>"$work/$run_name.err"
    "$work/$run_name" >"$work/$run_name.out"
}

expect_bail()
{
    bail_file=$1
    bail_pass=$2
    bail_reason=$3
    bail_func=$4
    grep -Fqx "bail: $bail_pass $bail_reason func=@$bail_func" "$bail_file"
}

# Canonicalization is observable both through the verifier gate and through
# the two required edge splits: this fixture starts with four blocks and O2
# must add a dedicated preheader and a dedicated exit.
emit_ir -O0 tests/programs/opt/s34_loop_canonical.cgfir canonical_o0
emit_ir -O2 tests/programs/opt/s34_loop_canonical.cgfir canonical_o2
test "$(grep -Ec '^[A-Za-z_.][A-Za-z0-9_.]*\([^)]*\):$' \
    "$work/canonical_o0.ir")" -eq 4
test "$(grep -Ec '^[A-Za-z_.][A-Za-z0-9_.]*\([^)]*\):$' \
    "$work/canonical_o2.ir")" -ge 6

# Positive LICM: one load moves from the loop header to its preheader.
emit_ir -O0 tests/programs/opt/s34_licm_positive.cgfir licm_positive_o0
emit_ir -O2 tests/programs/opt/s34_licm_positive.cgfir licm_positive_o2
test "$(grep -Fc 'load i32' "$work/licm_positive_o2.ir")" -eq 1
o0_load=$(grep -n 'load i32' "$work/licm_positive_o0.ir" | cut -d: -f1)
o0_loop=$(grep -n '^loop(' "$work/licm_positive_o0.ir" | cut -d: -f1)
o2_load=$(grep -n 'load i32' "$work/licm_positive_o2.ir" | cut -d: -f1)
o2_loop=$(grep -n '^loop(' "$work/licm_positive_o2.ir" | cut -d: -f1)
test "$o0_load" -gt "$o0_loop"
test "$o2_load" -lt "$o2_loop"

# Every LICM speculation row has an exact negative decision and the unsafe
# instruction remains in its guarded/clobbered/observable location.
emit_ir -O2 tests/programs/opt/s34_licm_bails.cgfir licm_bails
expect_bail "$work/licm_bails.err" licm licm_div_not_nonzero licm_div_guard
expect_bail "$work/licm_bails.err" licm licm_load_not_guaranteed licm_guarded_load
expect_bail "$work/licm_bails.err" licm licm_load_clobbered licm_clobbered_load
expect_bail "$work/licm_bails.err" licm licm_volatile licm_volatile_loop
expect_bail "$work/licm_bails.err" licm licm_call licm_call_loop
expect_bail "$work/licm_bails.err" licm licm_sink_unsafe licm_conditional_store
grep -q 'sdiv i32 120' "$work/licm_bails.ir"
grep -q 'load i32.*volatile' "$work/licm_bails.ir"
grep -q 'call i32 @observe' "$work/licm_bails.ir"

# A two-entry SCC is never misclassified as a natural loop.
emit_ir -O2 tests/programs/opt/s34_irreducible.cgfir irreducible
grep -q '^bail: .* loop_irreducible func=@irreducible$' \
    "$work/irreducible.err"

# Strength reduction adds the accumulator parameter and removes the multiply
# from the body. Widened modular arithmetic and a non-affine recurrence take
# their exact conservative exits.
emit_ir -O2 tests/programs/opt/s34_strength_shapes.cgfir strength
sed -n '/^func i32 @strength_affine/,/^}/p' "$work/strength.ir" \
    >"$work/strength_affine.ir"
grep -Eq '^loop\(i32 %[0-9]+, i32 %[0-9]+, i32 %[0-9]+\):$' \
    "$work/strength_affine.ir"
sed -n '/^body():$/,/^[A-Za-z_.][A-Za-z0-9_.]*(.*):$/p' \
    "$work/strength_affine.ir" >"$work/strength_body.ir"
! grep -q 'imul i32' "$work/strength_body.ir"
expect_bail "$work/strength.err" strength sr_wrap strength_wrap
expect_bail "$work/strength.err" strength sr_nonaffine strength_nonaffine

# -fwrapv removes signed no-wrap provenance at the lowering boundary. The
# loop transforms must consume this per-invocation setting, never a cached
# analysis result.
emit_ir -O0 tests/programs/opt/s34_licm_runtime.c strict_nsw
emit_ir -O0 tests/programs/opt/s34_licm_runtime.c wrap_nsw -fwrapv
grep -q ', nsw' "$work/strict_nsw.ir"
! grep -q ', nsw' "$work/wrap_nsw.ir"

# O3 must positively full-unroll the four-trip loop. Sprint 35 upgrades the
# nine-trip case to a serial factor-four partial loop; runtime and short
# pinned shapes remain explicit conservative exits.
emit_ir -O2 tests/programs/opt/s34_unroll_shapes.cgfir unroll_o2
emit_ir -O3 tests/programs/opt/s34_unroll_shapes.cgfir unroll_o3
sed -n '/^func i32 @unroll_full/,/^}/p' "$work/unroll_o2.ir" \
    >"$work/unroll_full_o2.ir"
sed -n '/^func i32 @unroll_full/,/^}/p' "$work/unroll_o3.ir" \
    >"$work/unroll_full_o3.ir"
test "$(grep -Fc 'iadd i32' "$work/unroll_full_o2.ir")" -eq 2
test "$(grep -Fc 'iadd i32' "$work/unroll_full_o3.ir")" -ge 4
! grep -q '^loop(' "$work/unroll_full_o3.ir"
expect_bail "$work/unroll_o3.err" unroll unroll_multi_exit unroll_multi_exit
expect_bail "$work/unroll_o3.err" unroll unroll_trip_wrap unroll_trip_wrap
expect_bail "$work/unroll_o3.err" unroll unroll_runtime_unsupported \
    unroll_runtime_unsupported
expect_bail "$work/unroll_o3.err" unroll unroll_pinned unroll_pinned
sed -n '/^func i32 @unroll_partial_unsupported/,/^}/p' \
    "$work/unroll_o3.ir" >"$work/unroll_partial.ir"
test "$(grep -Fc 'iadd i32' "$work/unroll_partial.ir")" -eq 10
grep -q '^loop(' "$work/unroll_partial.ir"
for func in \
    unroll_runtime_unsupported \
    unroll_pinned
do
    sed -n "/^func .* @$func/,/^}/p" "$work/unroll_o3.ir" \
        >"$work/$func.ir"
    grep -q '^loop(' "$work/$func.ir"
done
grep -q 'load i32.*volatile' "$work/unroll_pinned.ir"

# Semantic regressions: zero-trip div/null guards, conditional stores,
# modular 23-execution trip count, volatile dynamic count, affine wrap, and
# serial FP accumulation all execute with verification enabled.
for source in \
    tests/programs/opt/s34_licm_runtime.c \
    tests/programs/opt/s34_strength_runtime.c \
    tests/programs/opt/s34_volatile_runtime.c \
    tests/programs/opt/s34_wrap_runtime.c
do
    base=$(basename "$source" .c)
    build_run -O0 "$source" "${base}_o0"
    build_run -O3 "$source" "${base}_o3"
done

build_run -O0 tests/programs/opt/s34_fp_serial.c fp_o0
build_run -O3 tests/programs/opt/s34_fp_serial.c fp_o3
cmp "$work/fp_o0.out" "$work/fp_o3.out"

# The odd target is unreachable in modulo-256 arithmetic. Both optimization
# levels must stay infinite; an incorrect trip count or full unroll terminates.
if ! command -v timeout >/dev/null 2>&1; then
    echo 's34_loop_driver: GNU timeout is required for the infinite-loop gate' >&2
    exit 1
fi
for level in O0 O3; do
    CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
        "$cc" "-$level" tests/tools/s34_infinite_wrap.c \
        -o "$work/infinite_$level" 2>"$work/infinite_$level.err"
    set +e
    timeout 1 "$work/infinite_$level"
    status=$?
    set -e
    test "$status" -eq 124
done
grep -q '^bail: unroll unroll_trip_wrap func=@main$' \
    "$work/infinite_O3.err"

# Review-time source audit kept executable: no named decision may be hidden
# behind an enum, typo, or dynamically assembled string.
for reason in \
    loop_irreducible \
    licm_div_not_nonzero \
    licm_load_not_guaranteed \
    licm_volatile \
    licm_call \
    licm_load_clobbered \
    licm_sink_unsafe \
    sr_nonaffine \
    sr_wrap \
    unroll_trip_wrap \
    unroll_multi_exit \
    unroll_partial_unsupported \
    unroll_runtime_unsupported \
    unroll_pinned
do
    grep -R -Fq "\"$reason\"" src/opt
done

echo 's34_loop_driver: canonical/LICM/strength/unroll/wrap/volatile/FP gates green'
