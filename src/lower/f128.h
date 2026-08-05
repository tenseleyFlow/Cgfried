#ifndef CGF_LOWER_F128_H
#define CGF_LOWER_F128_H

#include "ir/ir.h"
#include "target.h"
#include "util/base.h"

/* True when the target has no binary128 instructions and every f128
 * operation must become a call into libcgf_rt. */
bool lower_f128_needs_libcalls(TargetSpec t);

/* Rewrite every f128 arithmetic and conversion instruction in `m` into the
 * corresponding soft-float call. Runs AFTER the optimizer so constant f128
 * arithmetic still folds; a no-op on targets with native long double. */
void lower_legalize_f128(IrModule *m, TargetSpec t);

/* The soft-float call an f128 comparison with `pred` (an IrFcmp) is answered
 * by, or NULL if the predicate has no plan -- which the pass treats as an
 * ICE. Exposed so a unit test can assert that ALL FOURTEEN predicates are
 * covered: C's six relational operators reach only eight of them, so the
 * other six have no fixture and an ICE there would be a crash on valid
 * optimizer output. */
const char *lower_f128_compare_libcall(u8 pred);

#endif
