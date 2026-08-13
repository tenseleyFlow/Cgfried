#ifndef CGF_A64_PEEP_H
#define CGF_A64_PEEP_H

#include "cg/arm64/mir.h"

/* Post-register-allocation cleanup.  This is deliberately one pipeline entry
 * point: its component rewrites share a capped fixpoint and never span a
 * basic-block boundary. */
bool a64_peep_post_ra(A64Func *f);
bool a64_branch_delta_fits(u16 op, i64 delta);
bool a64_branch_target_fits(const A64Func *f, u32 bi, u32 at, u16 op,
                            u32 target);

#endif
