#ifndef CGF_CG_SHARED_H
#define CGF_CG_SHARED_H

#include "util/base.h"

/* Target-neutral backend substrate. Backends expose their MIR through this
 * read-only view, then retain ownership of instruction constraints, physical
 * register policy, spill rewriting, and frame layout. Block targets are
 * one-based, matching the compiler IR and both machine IRs. */
typedef struct CgMirView {
    const void *ctx;
    u32 nvregs;
    u32 nblocks;
    u32 max_uses;
    u32 (*block_ninsts)(const void *ctx, u32 block);
    u32 (*inst_def)(const void *ctx, u32 block, u32 inst);
    u32 (*inst_uses)(const void *ctx, u32 block, u32 inst, u32 *out, u32 cap);
    u32 (*inst_targets)(const void *ctx, u32 block, u32 inst,
                        const u32 **targets);
} CgMirView;

typedef struct CgInterval {
    u32 vreg;
    u32 start, end; /* inclusive global instruction points */
    bool live;
    u8 fixed; /* backend physical register id + 1; zero = unconstrained */
    u8 phys;  /* backend physical register id + 1; zero = spilled */
    i32 slot; /* target frame displacement; zero = not spilled */
} CgInterval;

/* Caller-zeroed block bitsets, [nblocks * words]. */
u32 cg_liveness_words(const CgMirView *view);
void cg_liveness(const CgMirView *view, u64 *live_in, u64 *live_out);

/* Build one conservative, hole-free interval per vreg. `intervals` has
 * nvregs+1 entries and is cleared here. live_in/out are caller-owned. */
void cg_intervals_build(const CgMirView *view, u64 *live_in, u64 *live_out,
                        CgInterval *intervals);

typedef struct CgSpillSlots {
    i32 next_offset; /* grows downward from zero */
    u32 count;
} CgSpillSlots;

/* Allocate a downward-growing slot and return its negative displacement.
 * Size and alignment must be non-zero powers of two. */
i32 cg_spill_slot_assign(CgSpillSlots *slots, u32 size, u32 align);

/* Target policy for the shared simple linear scan. Register ids are compact
 * backend ids in [0,nphys_regs). pool returns the preferred order for vreg.
 * reg_usable owns call-clobber, fixed-color, and other target constraints. */
typedef struct CgLinearScanPolicy {
    void *ctx;
    u32 nphys_regs;
    bool spill_all;
    void (*pool)(void *ctx, u32 vreg, const u8 **regs, u32 *nregs);
    bool (*same_class)(void *ctx, u32 a, u32 b);
    bool (*reg_usable)(void *ctx, u32 vreg, u8 reg, u32 start, u32 end);
    u32 (*spill_size)(void *ctx, u32 vreg);
    u32 (*spill_align)(void *ctx, u32 vreg);
} CgLinearScanPolicy;

void cg_linear_scan(CgInterval *intervals, u32 nvregs,
                    const CgLinearScanPolicy *policy, CgSpillSlots *slots);

struct IrFunc;

/* A function's `.p2align` exponent: the backend's own default, RAISED by any
 * `aligned(N)` on the function. Shared so the two emitters cannot drift on a
 * rule that is neither of theirs -- it belongs to the attribute. `irf` may be
 * NULL (a function with no IR entry keeps the default). */
unsigned cg_func_p2align(const struct IrFunc *irf, unsigned dflt);

#endif
