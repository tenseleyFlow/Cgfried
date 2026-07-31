#include "cg/cg.h"
#include "unit.h"

/* Sprint 21 units: the simm32 truth table (the silent-wrong-code trap)
 * and the addressing-fold decision table. */

void test_x64_imm_fits_simm32(TestCtx *t)
{
    T_ASSERT(t, x64_imm_fits_simm32(0));
    T_ASSERT(t, x64_imm_fits_simm32(1));
    T_ASSERT(t, x64_imm_fits_simm32(-1));
    T_ASSERT(t, x64_imm_fits_simm32(0x7fffffff));
    T_ASSERT(t, !x64_imm_fits_simm32(0x80000000ll));
    T_ASSERT(t, x64_imm_fits_simm32(-0x80000000ll));
    T_ASSERT(t, !x64_imm_fits_simm32(-0x80000001ll));
    T_ASSERT(t, !x64_imm_fits_simm32(0x100000000ll));
    T_ASSERT(t, !x64_imm_fits_simm32(0xffffffffll));
    T_ASSERT(t, x64_imm_fits_simm32(0x7ffffffe));
    T_ASSERT(t, !x64_imm_fits_simm32((i64)0x8000000000000000ull));
    T_ASSERT(t, x64_imm_fits_simm32(42));
}

void test_x64_fold_table(TestCtx *t)
{
    T_ASSERT(t, x64_fold_ok(1, false, 0));
    T_ASSERT(t, x64_fold_ok(2, false, 8));
    T_ASSERT(t, x64_fold_ok(4, false, -8));
    T_ASSERT(t, x64_fold_ok(8, false, 0x7fffffff));
    T_ASSERT(t, !x64_fold_ok(3, false, 0));            /* bad scale */
    T_ASSERT(t, !x64_fold_ok(16, false, 0));           /* bad scale */
    T_ASSERT(t, !x64_fold_ok(1, true, 0));             /* rsp index hole */
    T_ASSERT(t, !x64_fold_ok(4, false, 0x80000000ll)); /* disp unfit */
    T_ASSERT(t, !x64_fold_ok(4, false, -0x80000001ll));
    T_ASSERT(t, x64_fold_ok(8, false, -2147483648ll));
}
