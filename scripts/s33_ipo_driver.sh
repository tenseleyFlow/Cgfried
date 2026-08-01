#!/bin/sh
set -eu

cc=${1:-build/cgfried}
host_cc=${CC:-cc}
work=$(mktemp -d "${TMPDIR:-/tmp}/cgf-s33-ipo.XXXXXX")
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

emit_ir -O0 tests/programs/opt/s33_static_local.c static_o0
emit_ir -O2 tests/programs/opt/s33_static_local.c static_o2
test "$(grep -Fc 'call i32 @next_counter' "$work/static_o0.ir")" -eq 2
test "$(grep -Fc 'call i32 @next_counter' "$work/static_o2.ir")" -eq 0
! grep -q '^func i32 @next_counter' "$work/static_o2.ir"
test "$(grep -Fc 'global @counter.0' "$work/static_o2.ir")" -eq 1

CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
    "$cc" -g -emit-ir -O2 tests/programs/opt/s33_static_local.c \
    >"$work/static_debug.ir" 2>"$work/static_debug.err"
test "$(grep -Fc 'call i32 @next_counter' "$work/static_debug.ir")" -eq 2
grep -q '^func i32 @next_counter' "$work/static_debug.ir"
grep -q '^bail: inline inl_debug_info func=@main$' "$work/static_debug.err"

emit_ir -O2 tests/programs/opt/s33_varargs.c varargs
test "$(grep -Fc 'call i32 @sum_args' "$work/varargs.ir")" -eq 1
grep -q '^bail: inline inl_va_start func=@main$' "$work/varargs.err"

emit_ir -O2 tests/programs/opt/s33_addr_table.c addr_table
grep -q '^func i32 @add_three' "$work/addr_table.ir"

# ABI-sensitive direct calls are genuinely inlined: byval keeps the one
# front-end copy (no duplicate), and pair/sret aggregate-return callees vanish
# while the full OPT_EQ corpus executes their results.
emit_ir -O2 tests/corpus/x86_64/int/struct_byval.c byval
! grep -q '^func i64 @eat' "$work/byval.ir"
test "$(grep -Fc 'memcpy' "$work/byval.ir")" -eq 1
emit_ir -O2 tests/corpus/x86_64/int/struct_ret.c aggregate_ret
! grep -q '^func .* @mkp' "$work/aggregate_ret.ir"
! grep -q '^func .* @mkb' "$work/aggregate_ret.ir"

for level in O2 O3; do
    emit_ir "-$level" tests/programs/opt/s33_alloca_loop.c "alloca_$level"
    test "$(grep -Fc 'call i32 @touch_vla' "$work/alloca_$level.ir")" \
        -eq 1
    grep -q '^bail: inline inl_alloca_in_loop func=@main$' \
        "$work/alloca_$level.err"
    CGF_AS=0 "$cc" "-$level" tests/programs/opt/s33_alloca_loop.c \
        -o "$work/alloca_$level"
    (ulimit -s 1024; "$work/alloca_$level")
done

CGF_VERIFY_AFTER_EACH=1 CGF_OPT_BAIL_LOG=1 \
    "$cc" -emit-ir -O2 -fcommon tests/tools/s33_common_reader.c \
    >"$work/common.ir" 2>"$work/common.err"
grep -q '^global @shared size 4 align 4 common' "$work/common.ir"
test "$(grep -Fc 'load i32, @shared' "$work/common.ir")" -eq 1

CGF_AS=0 "$cc" -O2 -fcommon -c tests/tools/s33_common_reader.c \
    -o "$work/reader.o"
CGF_AS=0 "$cc" -O2 -c tests/tools/s33_common_override.c \
    -o "$work/override.o"
"$host_cc" "$work/reader.o" "$work/override.o" -o "$work/common-link"
"$work/common-link"

echo "s33_ipo_driver: inline/ABI/static/varargs/alloca/address/common gates green"
