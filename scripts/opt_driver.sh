#!/bin/sh
set -eu

cc=${1:-build/cgfried}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-opt-driver.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

src=tests/corpus/x86_64/int/fib_iter.c

"$cc" -emit-ir -O1 "$src" >"$work/off.ir" 2>"$work/off-ir.err"
"$cc" -emit-ir -O1 -ftime-report "$src" >"$work/on.ir" \
    2>"$work/on-ir.err"
cmp "$work/off.ir" "$work/on.ir"
test ! -s "$work/off-ir.err"
grep -q '^optimization time report:$' "$work/on-ir.err"
grep -q 'mem2reg' "$work/on-ir.err"

"$cc" -S -O1 "$src" -o "$work/off.s" 2>"$work/off-s.err"
"$cc" -S -O1 -ftime-report "$src" -o "$work/on.s" \
    2>"$work/on-s.err"
cmp "$work/off.s" "$work/on.s"
test ! -s "$work/off-s.err"
grep -q '^optimization time report:$' "$work/on-s.err"
grep -q 'mem2reg' "$work/on-s.err"

check_bail()
{
    fixture=$1
    reason=$2
    CGF_OPT_BAIL_LOG=1 "$cc" -emit-ir -O1 "$fixture" \
        >"$work/$reason.ir" 2>"$work/$reason.err"
    grep -q "^bail: mem2reg $reason func=@f$" "$work/$reason.err"
    grep -q 'alloca' "$work/$reason.ir"
}

check_bail tests/programs/opt/bail_addr_taken.cgfir addr_taken
check_bail tests/programs/opt/bail_volatile_access.cgfir volatile_access
check_bail tests/programs/opt/bail_nonscalar.cgfir nonscalar
check_bail tests/programs/opt/bail_mixed_access_type.cgfir mixed_access_type
check_bail tests/programs/opt/regress_setjmp_pins_locals.cgfir setjmp_caller

check_s32_ir()
{
    fixture=$1
    name=$2
    CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
        "$cc" -emit-ir -O2 "$fixture" >"$work/$name.ir" \
        2>"$work/$name.err"
}

check_s32_ir tests/programs/opt/s32_gvn_noalias.cgfir gvn_noalias
test "$(grep -Fc 'load i32' "$work/gvn_noalias.ir")" -eq 1

check_s32_ir tests/programs/opt/s32_gvn_mayalias.cgfir gvn_mayalias
test "$(grep -Fc 'load i32' "$work/gvn_mayalias.ir")" -eq 2
test "$(grep -Fxc \
    'bail: gvn gvn_load_intervening_may_store func=@f' \
    "$work/gvn_mayalias.err")" -eq 1

check_s32_ir tests/programs/opt/s32_gvn_barriers.cgfir gvn_barriers
test "$(grep -Fc 'load i32' "$work/gvn_barriers.ir")" -eq 1
test "$(grep -Fc 'volatile' "$work/gvn_barriers.ir")" -eq 1
test "$(grep -Fc 'atomicrmw' "$work/gvn_barriers.ir")" -eq 1
test "$(grep -Fc 'cmpxchg' "$work/gvn_barriers.ir")" -eq 1

check_s32_ir tests/programs/opt/s32_dse_full.cgfir dse_full
! grep -q 'memset' "$work/dse_full.ir"

check_s32_ir tests/programs/opt/s32_dse_partial.cgfir dse_partial
test "$(grep -Fc 'memset' "$work/dse_partial.ir")" -eq 1
test "$(grep -Fxc 'bail: dse dse_partial_overwrite func=@f' \
    "$work/dse_partial.err")" -eq 1

check_s32_ir tests/programs/opt/s32_jump_irreducible.cgfir jt_after
grep -Fq 'condbr %0, header.preheader(), other()' "$work/jt_after.ir"
grep -Fq '    br header.preheader()' "$work/jt_after.ir"
grep -Fq '    br header()' "$work/jt_after.ir"
test "$(grep -Fxc \
    'bail: jump_thread jt_would_create_irreducible func=@f' \
    "$work/jt_after.err")" -eq 1

echo "opt_driver: timing stable; 5 mem2reg bails; Sprint 32 exact IR/count/bail checks green"
