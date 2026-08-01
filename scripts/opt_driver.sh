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

echo "opt_driver: timing leaves IR/assembly byte-identical; 5 bails logged"
