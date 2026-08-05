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

#endif
