#!/bin/sh
set -eu

cc=${1:-build/cgfried}
runner=${2:-build/cgf-test}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-s35-loops.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

emit_ir()
{
    emit_level=$1
    emit_source=$2
    emit_name=$3
    CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
        "$cc" -emit-ir "$emit_level" "$emit_source" \
        >"$work/$emit_name.ir" 2>"$work/$emit_name.err"
}

emit_ir_disabled()
{
    emit_toggle=$1
    emit_level=$2
    emit_source=$3
    emit_name=$4
    env "$emit_toggle=1" CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
        "$cc" -emit-ir "$emit_level" "$emit_source" \
        >"$work/$emit_name.ir" 2>"$work/$emit_name.err"
}

expect_bail()
{
    bail_file=$1
    bail_pass=$2
    bail_reason=$3
    bail_func=$4
    grep -Fqx "bail: $bail_pass $bail_reason func=@$bail_func" \
        "$bail_file"
}

run_toggle_corpus()
{
    toggle=$1
    log=$2

    env "$toggle=1" CGF_VERIFY_AFTER_EACH=1 CGF_TEST_CC="$cc" \
        "$runner" --profile linux-x86_64 tests/corpus >"$work/$log" 2>&1
    tail -1 "$work/$log"
    grep -q 'fail=0 xfail=0 xpass=0 skip=0 config=0$' "$work/$log"
}

# Unswitching must create two path-exclusive loop versions and leave the
# unsafe divisor inside the loop. The disable switch proves the structural
# difference rather than merely proving that the environment parses.
emit_ir -O3 tests/programs/opt/s35_unswitch_positive.cgfir unswitch
grep -q 'condbr 1,' "$work/unswitch.ir"
grep -q 'condbr 0,' "$work/unswitch.ir"
test "$(grep -Ec '^[A-Za-z_.][A-Za-z0-9_.]*\([^)]*\):$' \
    "$work/unswitch.ir")" -eq 16
emit_ir_disabled CGF_OPT_DISABLE_UNSWITCH -O3 \
    tests/programs/opt/s35_unswitch_positive.cgfir unswitch_disabled
! grep -q 'condbr 1,' "$work/unswitch_disabled.ir"
! grep -q 'condbr 0,' "$work/unswitch_disabled.ir"
test "$(grep -Ec '^[A-Za-z_.][A-Za-z0-9_.]*\([^)]*\):$' \
    "$work/unswitch_disabled.ir")" -eq 7
emit_ir -O3 tests/programs/opt/s35_unswitch_unsafe.cgfir unswitch_unsafe
expect_bail "$work/unswitch_unsafe.err" unswitch unswitch_unspeculatable \
    unswitch_unsafe
test "$(grep -Fc 'udiv i32' "$work/unswitch_unsafe.ir")" -eq 1

# The runtime fixture executes both specialized versions and carries volatile
# effects in each arm.  Pin the transformed function itself so a later
# front-end fold cannot make its OPT_EQ coverage vacuous.
emit_ir -O3 tests/programs/opt/s35_loop_runtime.c unswitch_runtime
sed -n '/^func i32 @unswitched/,/^}/p' "$work/unswitch_runtime.ir" \
    >"$work/unswitch_runtime_func.ir"
grep -q 'condbr 1,' "$work/unswitch_runtime_func.ir"
grep -q 'condbr 0,' "$work/unswitch_runtime_func.ir"
test "$(grep -Fc 'load i32, @observed, align 4, volatile' \
    "$work/unswitch_runtime_func.ir")" -eq 6

# BCE may erase only a proved comparison marker. A three-wide step against
# ten deliberately overshoots, so its marker and exact conservative reason
# must survive. Disabling BCE independently preserves the positive marker.
emit_ir -O2 tests/programs/opt/s35_bce_positive.cgfir bce
! grep -q ', bounds' "$work/bce.ir"
emit_ir_disabled CGF_OPT_DISABLE_BCE -O2 \
    tests/programs/opt/s35_bce_positive.cgfir bce_disabled
grep -q ', bounds' "$work/bce_disabled.ir"
emit_ir -O2 tests/programs/opt/s35_bce_overshoot.cgfir bce_overshoot
grep -q ', bounds' "$work/bce_overshoot.ir"
expect_bail "$work/bce_overshoot.err" bce bce_unproven bce_overshoot
CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
    "$cc" -fwrapv -emit-ir -O2 \
    tests/programs/opt/s35_bce_fwrapv.cgfir \
    >"$work/bce_fwrapv.ir" 2>"$work/bce_fwrapv.err"
grep -q ', bounds' "$work/bce_fwrapv.ir"
expect_bail "$work/bce_fwrapv.err" bce bce_wrap bce_fwrapv

# Fusion interleaves only the exact adjacent, independent shape. A negative
# dependence stays as two loops, and the switch preserves that same shape.
emit_ir -O3 tests/programs/opt/s35_fusion_positive.cgfir fusion
test "$(grep -Fc 'condbr ' "$work/fusion.ir")" -eq 1
! grep -q '^b\.h(' "$work/fusion.ir"
emit_ir_disabled CGF_OPT_DISABLE_FUSION -O3 \
    tests/programs/opt/s35_fusion_positive.cgfir fusion_disabled
test "$(grep -Fc 'condbr ' "$work/fusion_disabled.ir")" -eq 2
grep -q '^b\.h(' "$work/fusion_disabled.ir"
emit_ir -O3 tests/programs/opt/s35_fusion_negative.cgfir fusion_negative
test "$(grep -Fc 'condbr ' "$work/fusion_negative.ir")" -eq 2
grep -q '^b\.h(' "$work/fusion_negative.ir"
expect_bail "$work/fusion_negative.err" fusion fuse_negative_dep \
    fusion_negative

# The pass switches are a bisection contract, not merely parser coverage.
# Each disabled configuration runs the whole six-level OPT_EQ corpus.
run_toggle_corpus CGF_OPT_DISABLE_UNSWITCH no-unswitch.log
run_toggle_corpus CGF_OPT_DISABLE_BCE no-bce.log
run_toggle_corpus CGF_OPT_DISABLE_FUSION no-fusion.log

echo "s35_loop_driver: structural, bail, and pass-toggle matrix green"
