#include "cg/arm64/mir.h"
#include "cg/arm64/peep.h"

#include <stdlib.h>
#include <string.h>

#include "cg/shared.h"
#include "diag.h"
#include "driver/toolchain.h"

/* Sprint 48: the AArch64 instantiation of the shared linear scan.
 *
 * shared.c owns liveness, interval construction, the scan itself and spill
 * slot arithmetic; this file owns everything that is architectural — the
 * def/use table, the register file, the call-clobber model, spill rewriting
 * and (Sprint 48 deliverable 5) the frame.
 *
 * ONE code path at every opt level, exactly as x86_64 has had since Sprint
 * 22. There is deliberately no opt-level anything here.
 *
 * Register discipline (Sprint 48 deliverable 1's table, each row a bug
 * prevented):
 *
 *   x0-x11   allocatable. x0-x7 are the argument/result registers and x8 is
 *            the indirect-result register; none of that constrains ordinary
 *            temporaries, so they allocate freely and the call marshalling
 *            pass pre-colors what it needs.
 *   x12-x13  RESERVED for backend expansions. Atomic pseudo-ops use both;
 *            late frame-address materialization uses x12 for one LOAD/STORE.
 *   x14-x17  RESERVED as the four reload scratches. Four because MADD/MSUB
 *            have three register sources plus a destination, so a single
 *            instruction can need three reloads and a spilled-def home at
 *            once. x16/x17 are IP0/IP1, which a linker veneer may clobber
 *            across any bl — scratch values never live across a call, so
 *            using them here is precisely the sound use.
 *   x18      NEVER allocated. Platform register (macOS kernel, Windows TEB).
 *            One uniform rule on every target beats per-target divergence.
 *   x19-x28  allocatable, callee-saved: the prologue pays for them once.
 *   x29      frame pointer, reserved (no -fomit-frame-pointer in v0.1.0).
 *   x30      link register, clobbered by every bl; the prologue saves it.
 *   SP       never allocatable.
 *   v0-v27   allocatable; v8-v15 are callee-saved but ONLY their low 64 bits.
 *   v28-v31  RESERVED as FP reload scratches. FOUR, not three, for the same
 *            reason the GP side has four: FMADD/FMSUB are a destination plus
 *            THREE register sources, so one instruction can need three
 *            reloads and a spilled-def home at once. The pool was sized 3 for
 *            FCSEL (def + two sources) and stayed there when fp-contract
 *            started forming FMAs, so `a * b + c` at -Ofast under
 *            CGF_SPILL_ALL=1 ran the pool dry and ICEd.
 *
 * The d8-d15 rule: AAPCS64 obliges the callee to preserve d8-d15, i.e. bits
 * 0-63 of v8-v15. The upper halves are caller-saved. So (a) the prologue
 * saves 8-byte halves, and (b) a 128-bit value live across a call must never
 * be placed there expecting preservation. Every A64 vreg is currently 32 or
 * 64 bits wide, so (b) cannot yet be violated; a64_reg_preserved_across_call
 * states the rule anyway and is unit-tested directly, so Sprint 49's NEON
 * values inherit it rather than rediscovering it. */

#define A64_NGP_SCRATCH 4
#define A64_NFP_SCRATCH 4

static const u8 gp_scratch[A64_NGP_SCRATCH] = {A64_X16, A64_X17, A64_X15,
                                               A64_X14};
static const u8 fp_scratch[A64_NFP_SCRATCH] = {A64_V31, A64_V30, A64_V29,
                                               A64_V28};

/* Caller-saved first: they are free in a leaf range, and a call-crossing
 * interval is steered to the callee-saved tail by reg_usable anyway. */
/* x12/x13 are withheld for post-allocation expansions. Atomic pseudo-ops
 * expand at EMISSION into an ll/sc loop that needs a temporary and a
 * store-exclusive status register, and neither can come from the reload
 * scratches — a spilled operand of the very same instruction may already be
 * sitting in those. Frame finalization also uses x12 to form an otherwise
 * unencodable spill-slot address; that value lives only through the following
 * LOAD/STORE, so it never overlaps an atomic pseudo's internal x12 lifetime.
 * Two registers is the price of guaranteeing no spill code lands inside an
 * exclusive sequence, which would clear the monitor and spin forever. */
static const u8 gp_order[] = {
    A64_X0,  A64_X1,  A64_X2,  A64_X3,  A64_X4,  A64_X5,  A64_X6,  A64_X7,
    A64_X8,  A64_X9,  A64_X10, A64_X11, A64_X19, A64_X20, A64_X21, A64_X22,
    A64_X23, A64_X24, A64_X25, A64_X26, A64_X27, A64_X28,
};
#define A64_NGP ((u32)(sizeof(gp_order) / sizeof(gp_order[0])))

static const u8 fp_order[] = {
    A64_V0,  A64_V1,  A64_V2,  A64_V3,  A64_V4,  A64_V5,  A64_V6,
    A64_V7,  A64_V16, A64_V17, A64_V18, A64_V19, A64_V20, A64_V21,
    A64_V22, A64_V23, A64_V24, A64_V25, A64_V26, A64_V27, A64_V8,
    A64_V9,  A64_V10, A64_V11, A64_V12, A64_V13, A64_V14, A64_V15,
};
#define A64_NFP ((u32)(sizeof(fp_order) / sizeof(fp_order[0])))

bool a64_reg_is_callee_saved_gp(u8 reg)
{
    return reg >= A64_X19 && reg <= A64_X28;
}

bool a64_reg_preserved_across_call(u8 reg, bool wide128)
{
    if (reg <= A64_X30)
        return a64_reg_is_callee_saved_gp(reg);
    if (reg >= A64_V8 && reg <= A64_V15)
        return !wide128; /* only d8-d15, the low halves, survive */
    return false;
}

/* --- def/use table ---------------------------------------------------------
 *
 * Getting this wrong does not crash; it silently produces wrong liveness,
 * which is a miscompile. Two traps in particular:
 *
 *   - a MEM operand's base and index are USES on every instruction, including
 *     one whose ops[0] is a definition (`load dst, [base, idx]`);
 *   - a call keeps its arguments on the A64CallInfo side record rather than
 *     in the four inline operands, so walking ops[] alone sees no uses at all
 *     and every argument looks dead. */

typedef enum A64DefShape {
    A64_DEF_NONE,  /* no register destination */
    A64_DEF_OP0,   /* ops[0] defines; ops[1..] are sources */
    A64_DEF_OP0RMW /* ops[0] defines AND reads (movk) */
} A64DefShape;

static A64DefShape def_shape(u16 op)
{
    switch (op) {
    case A64_OP_MOVK:
        return A64_DEF_OP0RMW;
    case A64_OP_STORE:
    case A64_OP_STP:
    case A64_OP_FCMP:
    case A64_OP_B:
    case A64_OP_BCOND:
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
    case A64_OP_CALL:
    case A64_OP_RET:
    case A64_OP_BR:
    case A64_OP_UNREACHABLE:
    case A64_OP_STACKRESTORE:
    case A64_OP_VASTART:
        return A64_DEF_NONE;
    default:
        return A64_DEF_OP0;
    }
}

/* Where the source operands begin. Everything else in ops[] is a source. */
static u32 first_use_operand(u16 op)
{
    return def_shape(op) == A64_DEF_OP0 ? 1u : 0u;
}

static bool reg_is_virtual(A64Reg r)
{
    return r.id != 0 && !r.physical;
}

static u32 vreg_of(A64Reg r)
{
    return reg_is_virtual(r) ? r.id : 0u;
}

static void push_use(u32 *out, u32 cap, u32 *n, A64Reg r)
{
    u32 v = vreg_of(r);

    if (!v)
        return;
    if (*n >= cap)
        CGF_ICE("arm64 regalloc: use capacity %u exceeded", cap);
    out[(*n)++] = v;
}

static void mem_uses(const A64Operand *op, u32 *out, u32 cap, u32 *n)
{
    if (op->kind != A64O_MEM)
        return;
    push_use(out, cap, n, op->mem.base);
    push_use(out, cap, n, op->mem.index);
}

static u32 a64_inst_def(const A64Inst *in)
{
    if (in->op == A64_OP_CALL)
        return in->call ? vreg_of(in->call->result) : 0u;
    /* An asm's operands live in its side record, exactly like a call's.
     * Miss this and its output looks defined by nothing while its inputs
     * look used by nothing -- the liveness is quietly wrong rather than
     * absent, which is the trap Sprint 48 recorded for A64CallInfo. */
    if (in->op == A64_OP_ASM)
        return in->asm_info && in->asm_info->has_out
                   ? vreg_of(in->asm_info->out)
                   : 0u;
    if (in->op == A64_OP_LDP) {
        /* LDP defines two registers and the shared view carries one. The
         * pairing peephole is not in the compilation pipeline (Sprint 47
         * shipped it as a seed exercised by units), so a virtual pair here
         * would be a silent liveness hole rather than a missing feature. */
        if (in->nops > 1 && reg_is_virtual(in->ops[1].reg))
            CGF_ICE("arm64 regalloc: ldp over virtual registers is not "
                    "allocatable");
    }
    if (def_shape(in->op) == A64_DEF_NONE || !in->nops)
        return 0;
    if (in->ops[0].kind != A64O_REG)
        return 0;
    return vreg_of(in->ops[0].reg);
}

static u32 a64_inst_uses(const A64Inst *in, u32 *out, u32 cap)
{
    u32 n = 0, i;

    if (in->op == A64_OP_CALL) {
        if (in->call) {
            u32 k;

            push_use(out, cap, &n, in->call->indirect);
            for (k = 0; k < in->call->nargs; k++)
                push_use(out, cap, &n, in->call->args[k].value);
        }
        return n;
    }
    if (in->op == A64_OP_ASM) {
        if (in->asm_info) {
            u32 k;

            for (k = 0; k < in->asm_info->nops; k++) {
                const A64AsmOp *o = &in->asm_info->ops[k];

                /* The output is a DEF, not a use; a memory operand's
                 * address register and every input are uses. */
                if (o->is_output && o->cls != ASM_CLS_MEM)
                    continue;
                push_use(out, cap, &n, o->reg);
            }
        }
        return n;
    }
    if (def_shape(in->op) == A64_DEF_OP0RMW && in->nops &&
        in->ops[0].kind == A64O_REG)
        push_use(out, cap, &n, in->ops[0].reg);
    for (i = first_use_operand(in->op); i < in->nops; i++)
        if (in->ops[i].kind == A64O_REG)
            push_use(out, cap, &n, in->ops[i].reg);
    /* Addresses are sources wherever they appear, including operand 1 of a
     * defining load. */
    for (i = 0; i < in->nops; i++)
        mem_uses(&in->ops[i], out, cap, &n);
    return n;
}

static bool inst_is_terminator(const A64Inst *in)
{
    switch (in->op) {
    case A64_OP_B:
    case A64_OP_BCOND:
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
    case A64_OP_RET:
    case A64_OP_BR:
    case A64_OP_UNREACHABLE:
        return true;
    default:
        return false;
    }
}

/* --- shared MIR view ------------------------------------------------------ */

typedef struct Ra {
    A64Func *f;
    Arena *arena;
    CgInterval *iv;
    u64 *live_in;
    u64 *live_out;
    u32 words;
    u32 max_uses;
    CgSpillSlots slots;
    u32 targets[4];
} Ra;

static u32 view_block_ninsts(const void *ctx, u32 block)
{
    const Ra *ra = ctx;

    return ra->f->blocks[block].n;
}

static u32 view_inst_def(const void *ctx, u32 block, u32 inst)
{
    const Ra *ra = ctx;

    return a64_inst_def(&ra->f->blocks[block].insts[inst]);
}

static u32 view_inst_uses(const void *ctx, u32 block, u32 inst, u32 *out,
                          u32 cap)
{
    const Ra *ra = ctx;

    return a64_inst_uses(&ra->f->blocks[block].insts[inst], out, cap);
}

static u32 view_inst_targets(const void *ctx, u32 block, u32 inst,
                             const u32 **targets)
{
    Ra *ra = (Ra *)ctx;
    const A64Block *b = &ra->f->blocks[block];
    const A64Inst *in = &b->insts[inst];
    u32 n = 0, i;

    *targets = ra->targets;
    if (!inst_is_terminator(in)) {
        /* Interior instructions fall through to the next one, which the
         * shared walk already models by scanning a block backwards. */
        return 0;
    }
    for (i = 0; i < in->nops; i++)
        if (in->ops[i].kind == A64O_LABEL) {
            if (n == 4)
                CGF_ICE("arm64 regalloc: terminator has too many targets");
            ra->targets[n++] = in->ops[i].id;
        }
    /* A conditional branch whose false edge is implicit still falls through;
     * A64 selection always records both labels, so an unlabelled fallthrough
     * would be a selection bug rather than an allocator assumption. */
    if (n == 0 && in->op != A64_OP_RET && in->op != A64_OP_BR &&
        in->op != A64_OP_UNREACHABLE)
        CGF_ICE("arm64 regalloc: branch without a recorded target");
    return n;
}

static bool view_inst_clobbers_call(const void *ctx, u32 block, u32 inst)
{
    const Ra *ra = ctx;

    return ra->f->blocks[block].insts[inst].op == A64_OP_CALL;
}

static u32 compute_max_uses(const A64Func *f)
{
    u32 bi, ii, max = 4;

    for (bi = 0; bi < f->nblocks; bi++) {
        const A64Block *b = &f->blocks[bi];

        for (ii = 0; ii < b->n; ii++) {
            const A64Inst *in = &b->insts[ii];
            u32 n = 0;

            if (in->op == A64_OP_CALL && in->call)
                n = in->call->nargs + 1;
            else if (in->op == A64_OP_ASM && in->asm_info)
                n = in->asm_info->nops;
            else
                n = 4 + 2 * 4; /* four operands, each possibly a MEM */
            if (n > max)
                max = n;
        }
    }
    return max;
}

static CgMirView a64_mir_view(Ra *ra)
{
    CgMirView view;

    memset(&view, 0, sizeof(view));
    view.ctx = ra;
    view.nvregs = ra->f->nvregs;
    view.nblocks = ra->f->nblocks;
    view.max_uses = ra->max_uses;
    view.block_ninsts = view_block_ninsts;
    view.inst_def = view_inst_def;
    view.inst_uses = view_inst_uses;
    view.inst_targets = view_inst_targets;
    view.inst_clobbers_call = view_inst_clobbers_call;
    return view;
}

u32 a64_liveness_words(const A64Func *f)
{
    Ra ra;
    CgMirView view;

    memset(&ra, 0, sizeof(ra));
    ra.f = (A64Func *)f;
    ra.max_uses = compute_max_uses(f);
    view = a64_mir_view(&ra);
    return cg_liveness_words(&view);
}

void a64_liveness(const A64Func *f, u64 *live_in, u64 *live_out)
{
    Ra ra;
    CgMirView view;

    memset(&ra, 0, sizeof(ra));
    ra.f = (A64Func *)f;
    ra.max_uses = compute_max_uses(f);
    view = a64_mir_view(&ra);
    cg_liveness(&view, live_in, live_out);
}

/* --- call points ---------------------------------------------------------- */

/* An asm operand constrained to a register MUST get one: the template names
 * it by number, and no reload can stand in, because the scratch registers a
 * spill path reloads through are a small fixed set and several operands
 * would land in the same one. Marking the intervals unspillable is the
 * allocator's answer -- see CgInterval.no_spill. */
static void collect_no_spill(Ra *ra)
{
    u32 bi, ii, k;

    for (bi = 0; bi < ra->f->nblocks; bi++) {
        const A64Block *b = &ra->f->blocks[bi];

        for (ii = 0; ii < b->n; ii++) {
            const A64Inst *in = &b->insts[ii];
            const A64AsmInfo *info;

            if (in->op != A64_OP_ASM || !in->asm_info)
                continue;
            info = in->asm_info;
            if (info->has_out) {
                u32 v = vreg_of(info->out);

                if (v)
                    ra->iv[v].no_spill = true;
            }
            for (k = 0; k < info->nops; k++) {
                u32 v = vreg_of(info->ops[k].reg);

                if (v)
                    ra->iv[v].no_spill = true;
            }
        }
    }
}

/* --- linear scan policy --------------------------------------------------- */

static bool vreg_is_fp(const A64Func *f, u32 vreg)
{
    return a64_vclass(f, (A64Reg){vreg, 0}) == A64RC_FP;
}

/* A whole-q value. AAPCS64 preserves only the LOW 64 bits of v8-v15, so a
 * 128-bit value living across a call cannot go there — the rule Sprint 48
 * wrote down and had nothing wide enough to exercise until NEON arrived. */
static bool vreg_is_wide(const A64Func *f, u32 vreg)
{
    return a64_vwidth(f, (A64Reg){vreg, 0}) == A64_SF128;
}

static void a64_scan_pool(void *ctx, u32 vreg, const u8 **regs, u32 *nregs)
{
    Ra *ra = ctx;

    if (vreg_is_fp(ra->f, vreg)) {
        *regs = fp_order;
        *nregs = A64_NFP;
    } else {
        *regs = gp_order;
        *nregs = A64_NGP;
    }
}

static bool a64_scan_same_class(void *ctx, u32 a, u32 b)
{
    Ra *ra = ctx;

    return vreg_is_fp(ra->f, a) == vreg_is_fp(ra->f, b);
}

/* A pre-coloured interval takes its register unconditionally, so an ordinary
 * interval must never be handed a colour that a FIXED interval claims over an
 * overlapping range. Without this an argument copy into x0 and an unrelated
 * temporary the allocator also put in x0 silently share the register: the
 * copy destroys the temporary, a later copy reads the wrong value, and the
 * allocator reports success. Nothing crashes — the callee simply receives the
 * wrong argument.
 *
 * Endpoint sharing counts as a conflict. A pre-coloured value is live from
 * its defining copy until the call reads it, and an ordinary value whose last
 * use is that same copy still needs its register intact when it executes. */
static bool fixed_clash(const Ra *ra, u32 vreg, u8 reg, u32 start, u32 end)
{
    u32 v;

    for (v = 1; v <= ra->f->nvregs; v++) {
        const CgInterval *it = &ra->iv[v];

        if (v == vreg || !it->live || !it->fixed || it->fixed != (u8)(reg + 1))
            continue;
        if (start <= it->end && it->start <= end)
            return true;
    }
    return false;
}

static bool a64_scan_reg_usable(void *ctx, u32 vreg, u8 reg, u32 start, u32 end)
{
    Ra *ra = ctx;

    if (vreg_is_fp(ra->f, vreg) != (reg >= A64_V0 && reg <= A64_V31))
        return false;
    if (fixed_clash(ra, vreg, reg, start, end))
        return false;
    if (ra->iv[vreg].live_across_call)
        return a64_reg_preserved_across_call(reg, vreg_is_wide(ra->f, vreg));
    return true;
}

/* Eight bytes covers every value the A64 backend has today: a w-register
 * def zero-extends into its x view, and an s-register def zeroes the rest of
 * the d view, so the slot always captures the whole architectural value.
 * Sprint 49's 128-bit vectors widen this to 16. */
static u32 a64_scan_spill_size(void *ctx, u32 vreg)
{
    Ra *ra = ctx;

    return vreg_is_wide(ra->f, vreg) ? 16u : 8u;
}

static u32 a64_scan_spill_align(void *ctx, u32 vreg)
{
    Ra *ra = ctx;

    return vreg_is_wide(ra->f, vreg) ? 16u : 8u;
}

static void linear_scan(Ra *ra)
{
    CgLinearScanPolicy policy;

    memset(&policy, 0, sizeof(policy));
    policy.ctx = ra;
    policy.nphys_regs = A64_REG_COUNT;
    policy.spill_all = cgf_env("CGF_SPILL_ALL") != NULL;
    policy.pool = a64_scan_pool;
    policy.same_class = a64_scan_same_class;
    policy.reg_usable = a64_scan_reg_usable;
    policy.spill_size = a64_scan_spill_size;
    policy.spill_align = a64_scan_spill_align;
    cg_linear_scan(ra->iv, ra->f->nvregs, &policy, &ra->slots);
}

/* --- instruction stream rebuilding ---------------------------------------
 *
 * Inserting reloads in place while walking would re-visit the inserted code
 * as if it were user code; every pass therefore rebuilds the block and
 * remaps flags_src through an old-index-to-new-index table. */

typedef struct Rb {
    Arena *arena;
    A64Inst *v;
    u32 n, cap;
    u32 *map;
    u32 source_loc;
} Rb;

static void rb_init(Rb *rb, Arena *a, u32 old_n)
{
    memset(rb, 0, sizeof(*rb));
    rb->arena = a;
    rb->map = arena_alloc(a, (old_n ? old_n : 1) * sizeof(u32), 4);
}

static void rb_put(Rb *rb, const A64Inst *in)
{
    A64Inst copy = *in;

    if (rb->n == rb->cap) {
        u32 nc = rb->cap ? rb->cap * 2 : 16;
        A64Inst *nv =
            arena_alloc(rb->arena, nc * sizeof(A64Inst), _Alignof(A64Inst));

        if (rb->n)
            memcpy(nv, rb->v, rb->n * sizeof(A64Inst));
        rb->v = nv;
        rb->cap = nc;
    }
    if (!copy.loc)
        copy.loc = rb->source_loc;
    rb->v[rb->n++] = copy;
}

static void rb_commit(Rb *rb, A64Block *b)
{
    u32 i;

    for (i = 0; i < rb->n; i++) {
        A64Inst *in = &rb->v[i];

        if (in->flags & A64IF_USES_NZCV) {
            if (in->flags_src >= b->n)
                CGF_ICE("arm64 regalloc: NZCV producer index out of range");
            in->flags_src = rb->map[in->flags_src];
        }
    }
    b->insts = rb->v;
    b->n = rb->n;
    b->cap = rb->cap;
}

/* --- rewrite -------------------------------------------------------------- */

static A64Reg phys_reg(u8 reg)
{
    A64Reg r;

    r.id = (u32)reg + 1;
    r.physical = 1;
    return r;
}

static A64Inst mk_mem_op(u16 op, u8 sf, A64Reg value, i32 offset)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.sf = sf;
    in.nops = 2;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = value;
    in.ops[1].kind = A64O_MEM;
    in.ops[1].mem.base = phys_reg(A64_X29);
    in.ops[1].mem.offset = offset;
    /* The access size follows the width: a spilled vector moves as a whole
     * q register, and an 8-byte slot would silently drop its upper half. */
    in.ops[1].mem.size = (u8)(sf == A64_SF128 ? 16 : 8);
    in.ops[1].mem.mode = a64_isel_addr(offset, in.ops[1].mem.size, false,
                                       false) == A64_ADDR_SCALED
                             ? A64_ADDR_SCALED
                             : A64_ADDR_UNSCALED;
    return in;
}

typedef struct Rewriter {
    Ra *ra;
    u8 gp_next;
    u8 fp_next;
} Rewriter;

static u8 take_scratch(Rewriter *rw, bool fp)
{
    if (fp) {
        if (rw->fp_next >= A64_NFP_SCRATCH)
            CGF_ICE("arm64 regalloc: out of FP reload scratches");
        return fp_scratch[rw->fp_next++];
    }
    if (rw->gp_next >= A64_NGP_SCRATCH)
        CGF_ICE("arm64 regalloc: out of GP reload scratches");
    return gp_scratch[rw->gp_next++];
}

/* Substitute one register operand. A live vreg becomes its colour; a spilled
 * one becomes a scratch, and the caller has already emitted the reload. */
static void sub_reg(Rewriter *rw, A64Reg *r, u8 scratch)
{
    u32 v = vreg_of(*r);

    if (!v)
        return;
    if (rw->ra->iv[v].phys)
        *r = phys_reg((u8)(rw->ra->iv[v].phys - 1));
    else
        *r = phys_reg(scratch);
}

static void rewrite_block(Rewriter *rw, A64Block *b)
{
    Ra *ra = rw->ra;
    Rb rb;
    u32 ii, i;

    rb_init(&rb, ra->arena, b->n);
    for (ii = 0; ii < b->n; ii++) {
        A64Inst in = b->insts[ii];
        u32 def = a64_inst_def(&in);
        u8 def_scratch = 0;
        bool def_spilled = false;

        rb.source_loc = in.loc;
        rw->gp_next = 0;
        rw->fp_next = 0;

        if (in.op == A64_OP_ASM && in.asm_info) {
            /* An asm's operands live on its side record, like a call's.
             * Ordinary AAPCS64 constraint letters do not name a register;
             * GNU local register variables are the exception and isel
             * localizes those to pre-coloured copies at this asm site.
             *
             * THEY ARE NOT RELOADED EITHER, and that took a bug to learn.
             * The first draft reloaded each spilled operand into its own
             * scratch, which is already better than sharing one -- and still
             * wrong, because the scratch set is small and fixed and an asm
             * may name thirty operands. `"r"` means "in a register", so the
             * answer is that these intervals never spill at all:
             * collect_no_spill marks them and CgInterval.no_spill makes the
             * shared allocator honour it. What is left here is the check,
             * because an operand that reached this point without a register
             * would otherwise be substituted to x0 -- silently, and only
             * under CGF_SPILL_ALL. */
            A64AsmInfo *info = in.asm_info;
            u32 k;

            for (k = 0; k < info->nops; k++) {
                A64AsmOp *o = &info->ops[k];
                u32 v = vreg_of(o->reg);

                if (v && !ra->iv[v].phys)
                    CGF_ICE("arm64 regalloc: asm operand v%u was spilled; asm "
                            "operand intervals must be no_spill",
                            v);
            }
            for (k = 0; k < info->nops; k++)
                sub_reg(rw, &info->ops[k].reg, 0);
            sub_reg(rw, &info->out, 0);
            rb.map[ii] = rb.n;
            rb_put(&rb, &in);
            continue;
        }

        if (in.op == A64_OP_CALL) {
            /* Arguments and the result live on the side record; every one of
             * them is a pre-coloured vreg by now, so substitution is the same
             * lookup as any other operand. */
            A64CallInfo *call = in.call;
            u32 k;

            if (!call)
                CGF_ICE("arm64 regalloc: call without ABI metadata");
            /* The arguments and the result ARE pre-coloured -- marshal_calls
             * runs before allocation and linear scan honours `fixed` ahead of
             * the spill-everything policy -- but the INDIRECT CALLEE is not.
             * It is an ordinary vreg, so it can spill, and sub_reg with no
             * scratch would quietly rewrite it to x0: `blr x0`, branching to
             * whatever the first argument left there. Reloading into x16 is
             * safe here because IP0 is not an argument register and the
             * marshalling moves are already behind us. */
            {
                u32 v = vreg_of(call->indirect);
                u8 scratch = 0;

                if (v && !ra->iv[v].phys) {
                    A64Inst reload;

                    if (!ra->iv[v].slot)
                        CGF_ICE("arm64 regalloc: spilled indirect callee %u "
                                "has no slot",
                                v);
                    scratch = take_scratch(rw, false);
                    reload = mk_mem_op(A64_OP_LOAD, A64_SF64, phys_reg(scratch),
                                       ra->iv[v].slot);
                    rb_put(&rb, &reload);
                }
                sub_reg(rw, &call->indirect, scratch);
            }
            sub_reg(rw, &call->result, 0);
            for (k = 0; k < call->nargs; k++)
                sub_reg(rw, &call->args[k].value, 0);
            rb.map[ii] = rb.n;
            rb_put(&rb, &in);
            continue;
        }

        /* Reloads first, so a spilled source is in a scratch before the
         * instruction that reads it. */
        for (i = 0; i < in.nops; i++) {
            A64Reg *slots[3];
            u32 nslots = 0, k;

            if (in.ops[i].kind == A64O_REG) {
                if (i >= first_use_operand(in.op) ||
                    def_shape(in.op) == A64_DEF_OP0RMW)
                    slots[nslots++] = &in.ops[i].reg;
            } else if (in.ops[i].kind == A64O_MEM) {
                slots[nslots++] = &in.ops[i].mem.base;
                slots[nslots++] = &in.ops[i].mem.index;
            }
            for (k = 0; k < nslots; k++) {
                u32 v = vreg_of(*slots[k]);
                bool fp;
                u8 scratch;
                A64Inst reload;

                if (!v || ra->iv[v].phys)
                    continue;
                if (!ra->iv[v].slot)
                    CGF_ICE("arm64 regalloc: vreg %u has neither a register "
                            "nor a slot",
                            v);
                fp = vreg_is_fp(ra->f, v);
                scratch = take_scratch(rw, fp);
                /* One opcode serves both banks: the register operand names
                 * the bank, exactly as the printer reads it. A vector value
                 * moves as a whole q register or its upper half is lost. */
                reload = mk_mem_op(
                    A64_OP_LOAD, vreg_is_wide(ra->f, v) ? A64_SF128 : A64_SF64,
                    phys_reg(scratch), ra->iv[v].slot);
                rb_put(&rb, &reload);
            }
        }
        /* Second pass substitutes, consuming the scratches in the same
         * order the reloads produced them. */
        rw->gp_next = 0;
        rw->fp_next = 0;
        for (i = 0; i < in.nops; i++) {
            A64Reg *slots[3];
            u32 nslots = 0, k;

            if (in.ops[i].kind == A64O_REG) {
                if (i >= first_use_operand(in.op) ||
                    def_shape(in.op) == A64_DEF_OP0RMW)
                    slots[nslots++] = &in.ops[i].reg;
            } else if (in.ops[i].kind == A64O_MEM) {
                slots[nslots++] = &in.ops[i].mem.base;
                slots[nslots++] = &in.ops[i].mem.index;
            }
            for (k = 0; k < nslots; k++) {
                u32 v = vreg_of(*slots[k]);
                u8 scratch = 0;

                if (!v)
                    continue;
                if (!ra->iv[v].phys)
                    scratch = take_scratch(rw, vreg_is_fp(ra->f, v));
                sub_reg(rw, slots[k], scratch);
            }
        }
        if (def) {
            def_spilled = ra->iv[def].phys == 0;
            if (def_spilled) {
                if (!ra->iv[def].slot)
                    CGF_ICE("arm64 regalloc: spilled def %u has no slot", def);
                if (def_shape(in.op) == A64_DEF_OP0RMW) {
                    /* A read-modify-write MERGES into its destination, so the
                     * def and the use are ONE register. The substitution pass
                     * above already reloaded the old value into a scratch and
                     * pointed ops[0] at it; taking a fresh scratch here would
                     * merge the immediate into whatever the OTHER scratch
                     * happened to hold.
                     *
                     * movk is the only RMW, and it exists only inside a
                     * multi-instruction constant materialization -- so the
                     * symptom is a silently WRONG CONSTANT, with no crash and
                     * no diagnostic. `movz x16, ...; movk x17, ...` built
                     * 3.187264 where the program said 3.140625. */
                    def_scratch = (u8)(in.ops[0].reg.id - 1);
                } else {
                    def_scratch = take_scratch(rw, vreg_is_fp(ra->f, def));
                    in.ops[0].reg = phys_reg(def_scratch);
                }
            } else {
                in.ops[0].reg = phys_reg((u8)(ra->iv[def].phys - 1));
            }
        }
        /* Recorded HERE, not at the top of the loop: this pass emits RELOADS
         * ahead of the instruction, so an entry taken before them names the
         * first reload instead of the instruction itself. An NZCV producer
         * whose own operands spilled then had every consumer re-aimed at a
         * load, which defines no flags -- 'NZCV consumer does not name an
         * earlier producer'. The other four rebuild loops already record the
         * entry immediately before their rb_put for exactly this reason. */
        rb.map[ii] = rb.n;
        rb_put(&rb, &in);
        if (def_spilled) {
            A64Inst store = mk_mem_op(
                A64_OP_STORE, vreg_is_wide(ra->f, def) ? A64_SF128 : A64_SF64,
                phys_reg(def_scratch), ra->iv[def].slot);

            rb_put(&rb, &store);
        }
    }
    rb_commit(&rb, b);
}

/* --- AAPCS64 call marshalling ---------------------------------------------
 *
 * Sprint 47 left calls ABI-neutral on purpose: selection recorded every
 * logical argument and its IR annotation but assigned no register. This pass
 * runs the AAPCS64 stage-C walk and pre-colours the copies, so the shared
 * linear scan force-colours them rather than needing a separate marshaller.
 *
 * The IR the lowering hands us is already classified (src/lower/abi.c), so a
 * composite has become either bit-carrying doublewords, HFA leaves, or — for
 * anything over 16 bytes — a POINTER to a caller-made copy. That last row is
 * the #1 cross-ABI porting bug: on AAPCS64 the pointer costs one GPR, where
 * SysV would have copied the pointee onto the stack. Here it simply arrives
 * as an ordinary pointer-typed argument and needs no special case at all.
 *
 * Two counters, not one: NGRN and NSRN advance independently, and each is
 * driven to 8 the moment its bank stacks an argument, so a later argument of
 * that class cannot sneak back into a register (the NAF rule). */

typedef struct ArgWalk {
    u32 ngrn; /* next general register number */
    u32 nsrn; /* next SIMD register number */
    u32 nsaa; /* next stacked argument offset, from the outgoing area base */
} ArgWalk;

/* f128 belongs here even though the architecture cannot compute with it:
 * AAPCS64 passes and returns binary128 in a q register exactly like any
 * other floating type, and lower/f128.c's libcalls are ordinary calls whose
 * arguments happen to be f128. Leaving it out classified those arguments as
 * integers, which marshalled them with `mov x0, d0` -- a mixed-bank move
 * that names one register in each file. */
static bool a64_type_is_fp(u8 type)
{
    return type == IRT_F32 || type == IRT_F64 || type == IRT_F128;
}

static A64Sf a64_type_sf(u8 type)
{
    if (type == IRT_F128)
        return A64_SF128;
    return type == IRT_I8 || type == IRT_I16 || type == IRT_I32 ||
                   type == IRT_F32
               ? A64_SF32
               : A64_SF64;
}

static A64Reg fixed_vreg(A64Func *f, A64RegClass rc, A64Sf sf, u8 phys)
{
    return a64_newv_fixed(f, rc, sf, phys);
}

static A64Inst mk_move(bool fp, A64Sf sf, A64Reg dst, A64Reg src)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = fp ? A64_OP_FMOV : A64_OP_MOV;
    in.sf = (u8)sf;
    in.nops = 2;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = dst;
    in.ops[1].kind = A64O_REG;
    in.ops[1].reg = src;
    return in;
}

/* Apple row 2: widen a sub-32-bit argument into its argument register.
 * Unsigned is one AND with a byte/halfword mask; signed is the LSL/ASR pair
 * the selector already uses for IR_SEXT, through a scratch so neither
 * instruction reads its own destination. Both run before liveness, so a
 * fresh vreg here is ordinary. */
static void emit_arg_extend(A64Func *f, Rb *rb, A64Reg slot,
                            const A64CallArg *arg)
{
    u32 bits = ir_type_size((IrType)arg->type) * 8;
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.nops = 3;
    in.sf = A64_SF32; /* a w-register write zeroes bits 32..63 */
    in.ops[0].kind = A64O_REG;
    in.ops[1].kind = A64O_REG;
    in.ops[2].kind = A64O_IMM;
    if (arg->argflags & IROPF_ZEXT) {
        in.op = A64_OP_AND;
        in.ops[0].reg = slot;
        in.ops[1].reg = arg->value;
        in.ops[2].imm = (i64)(((u64)1 << bits) - 1);
        rb_put(rb, &in);
        return;
    }
    {
        A64Reg tmp = a64_newv_width(f, A64RC_GP, A64_SF32);

        in.op = A64_OP_LSL;
        in.ops[0].reg = tmp;
        in.ops[1].reg = arg->value;
        in.ops[2].imm = (i64)(32u - bits);
        rb_put(rb, &in);
        in.op = A64_OP_ASR;
        in.ops[0].reg = slot;
        in.ops[1].reg = tmp;
        rb_put(rb, &in);
    }
}

/* An outgoing stack argument is addressed from SP, and SP has not moved yet:
 * the offsets are relative to the base of the outgoing area, which frame
 * finalization places at SP+0 after the prologue's single subtraction. */
static A64Inst mk_out_arg_store(A64Reg value, A64Sf sf, u32 offset, u32 bytes)
{
    A64Inst in;
    u8 size = (u8)bytes;

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_STORE;
    in.sf = (u8)sf;
    in.nops = 2;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = value;
    in.ops[1].kind = A64O_MEM;
    in.ops[1].mem.base = phys_reg(A64_SP);
    in.ops[1].mem.offset = (i64)offset;
    in.ops[1].mem.size = size;
    in.ops[1].mem.mode = (u8)a64_isel_addr((i64)offset, size, false, false);
    return in;
}

static void marshal_call(A64Func *f, Rb *rb, A64Inst *in, u32 *out_args)
{
    A64CallInfo *call = in->call;
    ArgWalk w;
    A64CallArg *kept;
    A64Reg pair_dest = {0, 0};
    u32 hfa_leaves = 0; /* nonzero: pair_dest is filled from v0-v(n-1) */
    u32 hfa_leaf = 0;   /* ...each this many bytes wide */
    /* AAPCS64 fills the register queues with named and anonymous arguments
     * alike; Apple stops at the last named one. Sprint 51 replaces the host
     * sniff with the driver's selected target. */
    bool apple = cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS;
    u32 nkept = 0, i;

    if (!call)
        CGF_ICE("arm64 regalloc: call without ABI metadata");
    memset(&w, 0, sizeof(w));
    kept =
        arena_alloc(f->arena, (call->nargs ? call->nargs : 1) * sizeof(*kept),
                    _Alignof(A64CallArg));

    for (i = 0; i < call->nargs; i++) {
        A64CallArg *arg = &call->args[i];
        u32 kind = ir_arg_kind(arg->abi_annot);
        bool fp = a64_type_is_fp(arg->type);
        A64Sf sf = a64_type_sf(arg->type);
        u8 phys = A64_REG_NONE;

        if (kind == IR_ARG_HFA) {
            /* Same shape as a pair -- the destination pointer is IR
             * bookkeeping and consumes no register -- but the value comes
             * back in v0-v(n-1) at the LEAF width, not in x0:x1. Letting
             * this fall into the pair branch below is what made our CALLER
             * read the wrong registers while our CALLEE was already right,
             * which only a mixed link in BOTH directions can catch. */
            pair_dest = arg->value;
            hfa_leaves = ir_arg_hfa_n(arg->abi_annot);
            hfa_leaf =
                hfa_leaves ? ir_arg_size(arg->abi_annot) / hfa_leaves : 0;
            continue;
        }
        if (kind >= IR_ARG_PAIR_II) {
            /* The IR keeps a 9-16 byte composite return sret-SHAPED, but
             * AAPCS64 passes no pointer for it: the pair comes back in
             * x0:x1 and the caller stores it into the temporary itself.
             * The pointer therefore consumes no argument register and its
             * value must survive the call, which the clobber model already
             * forces into a callee-saved register. */
            pair_dest = arg->value;
            continue;
        }
        if (kind == IR_ARG_SRET) {
            /* x8 is NOT argument nine: a function returning a large object
             * still receives its first real argument in x0, so the indirect
             * result register consumes no NGRN. */
            phys = A64_X8;
        } else if (!apple && ir_abi_even_gpr(arg->abi_annot)) {
            /* IR-C-09: lowering marked the first leaf of one naturally
             * 16-byte-aligned Linux composite. Skip odd NGRN before placing
             * that leaf; Apple deliberately keeps consecutive x registers. */
            w.ngrn = (w.ngrn + 1u) & ~1u;
            if (w.ngrn < 8)
                phys = (u8)(A64_X0 + w.ngrn++);
            else
                w.ngrn = 8;
        } else if (arg->argflags & IROPF_ONSTACK) {
            /* AAPCS64 C.4/C.12: an aggregate that did not fit ENTIRELY in
             * the remaining registers is passed entirely on the stack, and
             * the exhausted bank is then PINNED at 8 so every later argument
             * of that class stacks too. Lowering decided the placement (it
             * is the only thing that sees a whole C argument rather than its
             * eightbyte leaves); this honours it and pins the same bank,
             * which the leaf TYPE identifies. */
            phys = A64_REG_NONE;
            if (fp)
                w.nsrn = 8;
            else
                w.ngrn = 8;
        } else if (apple && (arg->argflags & IROPF_ANON)) {
            /* Apple: an anonymous argument NEVER takes a register, and it
             * does not consume one either — a named argument after it is
             * impossible, but the queues stay untouched on principle. This
             * is the whole reason a callee there needs no save area. */
            phys = A64_REG_NONE;
        } else if (fp) {
            if (w.nsrn < 8)
                phys = (u8)(A64_V0 + w.nsrn++);
            else
                w.nsrn = 8;
        } else {
            if (w.ngrn < 8)
                phys = (u8)(A64_X0 + w.ngrn++);
            else
                w.ngrn = 8;
        }
        if (phys == A64_REG_NONE) {
            A64Inst store;
            /* AAPCS64 gives every stack argument a full eightbyte, so only
             * a 16-byte binary128 needs more. Apple's ABI instead packs each
             * one at its NATURAL size and alignment, which is why a
             * (char, char, short) tail there is four bytes and not
             * twenty-four. A composite is already an eightbyte scalar or a
             * pointer by the time it reaches here, so its own alignment
             * never enters -- AAPCS64 C.12/C.13 are unamended and lowering
             * has applied them. Measured against clang.
             *
             * ANONYMOUS arguments are exempt and keep their eightbyte:
             * rows 1 and 3 are separate rules and packing the varargs area
             * puts every argument after the first where the callee will not
             * look. Landing row 3 without this exemption silently regressed
             * row 1, which no test caught because the macOS programs have
             * no automated lane yet -- see tests/macos/run.sh. */
            u32 slot_bytes = apple && !(arg->argflags & IROPF_ANON)
                                 ? ir_type_size(arg->type)
                                 : (sf == A64_SF128 ? 16u : 8u);

            if (ir_abi_stack_align16(arg->abi_annot))
                w.nsaa = (w.nsaa + 15u) & ~15u;
            w.nsaa = (w.nsaa + slot_bytes - 1u) & ~(slot_bytes - 1u);
            store = mk_out_arg_store(arg->value, sf, w.nsaa, slot_bytes);
            rb_put(rb, &store);
            w.nsaa += slot_bytes;
            continue;
        }
        {
            A64Reg slot = fixed_vreg(f, fp ? A64RC_FP : A64RC_GP, sf, phys);

            /* Apple makes the CALLER widen an argument narrower than 32
             * bits; AAPCS64 leaves the high bits unspecified and puts that
             * duty on the callee. A cgf callee still widens defensively,
             * which is correct under both -- but a clang callee on macos
             * reads w0 raw, so skipping this is a silent wrong answer. */
            if (apple && (arg->argflags & (IROPF_SEXT | IROPF_ZEXT))) {
                emit_arg_extend(f, rb, slot, arg);
            } else {
                A64Inst move = mk_move(fp, sf, slot, arg->value);

                rb_put(rb, &move);
            }
            kept[nkept] = *arg;
            kept[nkept].value = slot;
            nkept++;
        }
    }
    call->args = kept;
    call->nargs = nkept;
    if (w.nsaa > *out_args)
        *out_args = w.nsaa;

    if (pair_dest.id) {
        /* Reading x0/x1 physically is safe exactly here: an interval that
         * crosses a call is already confined to callee-saved registers, and
         * these stores precede every definition after the call. */
        u32 n = hfa_leaves ? hfa_leaves : 2;
        u32 width = hfa_leaves ? hfa_leaf : 8;
        u8 base = hfa_leaves ? A64_V0 : A64_X0;
        A64Sf lsf = width == 4 ? A64_SF32 : width == 16 ? A64_SF128 : A64_SF64;
        u32 k;

        rb_put(rb, in);
        for (k = 0; k < n; k++) {
            A64Inst st;

            memset(&st, 0, sizeof(st));
            st.op = A64_OP_STORE;
            st.sf = (u8)lsf;
            st.nops = 2;
            st.ops[0].kind = A64O_REG;
            st.ops[0].reg = phys_reg((u8)(base + k));
            st.ops[1].kind = A64O_MEM;
            st.ops[1].mem.base = pair_dest;
            st.ops[1].mem.offset = (i64)(width * k);
            st.ops[1].mem.size = (u8)width;
            st.ops[1].mem.mode = A64_ADDR_SCALED;
            rb_put(rb, &st);
        }
        return;
    }
    if (call->result.id) {
        bool fp = a64_type_is_fp(call->result_type);
        A64Sf sf = a64_type_sf(call->result_type);
        A64Reg ret = fixed_vreg(f, fp ? A64RC_FP : A64RC_GP, sf,
                                (u8)(fp ? A64_V0 : A64_X0));
        A64Reg original = call->result;
        A64Inst copy = mk_move(fp, sf, original, ret);

        call->result = ret;
        rb_put(rb, in);
        rb_put(rb, &copy);
        return;
    }
    rb_put(rb, in);
}

static void marshal_calls(A64Func *f)
{
    u32 out_args = 0, bi, ii;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;
        bool any = false;

        for (ii = 0; ii < b->n; ii++)
            if (b->insts[ii].op == A64_OP_CALL)
                any = true;
        if (!any)
            continue;
        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            rb.map[ii] = rb.n;
            rb.source_loc = b->insts[ii].loc;
            if (b->insts[ii].op == A64_OP_CALL) {
                /* The marshalling copies precede the call, so the map entry
                 * must name the CALL itself rather than the first copy; no
                 * NZCV consumer can name a call anyway (a call clobbers the
                 * flags), so recording the block-relative start is correct
                 * and the verifier re-checks it. */
                marshal_call(f, &rb, &b->insts[ii], &out_args);
            } else {
                rb_put(&rb, &b->insts[ii]);
            }
        }
        rb_commit(&rb, b);
    }
    f->out_args = (out_args + 15) & ~15u;
}

/* --- frame ----------------------------------------------------------------
 *
 * Canonical AAPCS64 shape, which Sprint 49's CFI reads literally:
 *
 *     stp x29, x30, [sp, #-FRAME]!     // one instruction: allocate and save
 *     add x29, sp, #0                  // the MOV-from-SP alias
 *     stp x19, x20, [sp, #16]          // callee-saved pairs
 *     stp d8,  d9,  [sp, #32]
 *     ...
 *     ldp x19, x20, [sp, #16]          // epilogue is the exact mirror
 *     ldp x29, x30, [sp], #FRAME
 *     ret
 *
 * The pre-index form scales a signed 7-bit immediate by 8, so it reaches
 * -512; a larger frame subtracts from SP first and stores at #0. When the
 * outgoing area itself exceeds the pair instruction's +504 limit, x16 first
 * materializes SP+base and the pair is stored through it.
 *
 * Layout from the new SP upward: x29/x30, callee-saved GP, callee-saved
 * d-halves, then spill slots and static allocas. SP stays 16-byte aligned
 * because AArch64 CHECKS it in hardware at every SP-based access — a
 * misaligned SP faults at the next load rather than limping the way a
 * misaligned rsp does. a64_frame_total rounds and the verifier re-checks. */

/* stp/ldp index and scaled offsets share ONE field: a signed 7-bit value
 * scaled by 8, so the range is [-512, +504] -- ASYMMETRIC. 512 is legal for
 * the prologue's `stp x29, x30, [sp, #-512]!` and illegal for the epilogue's
 * matching `ldp x29, x30, [sp], #512`, which is exactly how a 512-byte frame
 * assembled the store and then failed on the load. The bound has to be the
 * one both directions satisfy. */
#define A64_FRAME_PREINDEX_MAX 504

u32 a64_frame_total(u32 csr_bytes, u32 local_bytes, u32 out_args)
{
    u64 raw = (u64)csr_bytes + local_bytes + out_args;

    return (u32)((raw + 15) & ~(u64)15);
}

typedef struct Frame {
    u8 gp[16];
    u32 ngp;
    u8 fp[8];
    u32 nfp;
    u32 base;        /* SP offset of the x29/x30 pair == the outgoing area */
    u32 csr_size;    /* 16 + saved registers, measured from `base` */
    u32 total;       /* whole frame, a multiple of 16 */
    u32 local_top;   /* bytes of spill/alloca area */
    bool dynamic_sp; /* a VLA or stackrestore moves SP inside the body */
} Frame;

/* Does anything in this function move SP after the prologue? Spill slots and
 * static allocas are addressed from x29 and do not care, but the EPILOGUE is
 * written entirely against SP, so it has to recover SP from x29 first. */
static bool frame_has_dynamic_sp(const A64Func *f)
{
    u32 bi, ii;

    for (bi = 0; bi < f->nblocks; bi++)
        for (ii = 0; ii < f->blocks[bi].n; ii++) {
            u16 op = f->blocks[bi].insts[ii].op;

            if (op == A64_OP_ALLOCA_DYN || op == A64_OP_STACKRESTORE)
                return true;
        }
    return false;
}

static void frame_collect_saved(const A64Func *f, Frame *fr)
{
    bool used[A64_REG_COUNT];
    u32 bi, ii, i, k;

    memset(used, 0, sizeof(used));
    for (bi = 0; bi < f->nblocks; bi++) {
        const A64Block *b = &f->blocks[bi];

        for (ii = 0; ii < b->n; ii++) {
            const A64Inst *in = &b->insts[ii];

            for (i = 0; i < in->nops; i++) {
                const A64Operand *op = &in->ops[i];

                if (op->kind == A64O_REG && op->reg.physical &&
                    op->reg.id - 1 < A64_REG_COUNT)
                    used[op->reg.id - 1] = true;
                if (op->kind == A64O_MEM) {
                    if (op->mem.base.physical &&
                        op->mem.base.id - 1 < A64_REG_COUNT)
                        used[op->mem.base.id - 1] = true;
                    if (op->mem.index.physical &&
                        op->mem.index.id - 1 < A64_REG_COUNT)
                        used[op->mem.index.id - 1] = true;
                }
            }
        }
    }
    for (k = A64_X19; k <= A64_X28; k++)
        if (used[k])
            fr->gp[fr->ngp++] = (u8)k;
    for (k = A64_V8; k <= A64_V15; k++)
        if (used[k])
            fr->fp[fr->nfp++] = (u8)k;
}

static A64Inst mk_pair(u16 op, A64PhysReg a, A64PhysReg b, A64PhysReg base,
                       i64 offset, A64AddrMode mode)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.sf = A64_SF64;
    in.nops = 3;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = phys_reg((u8)a);
    in.ops[1].kind = A64O_REG;
    in.ops[1].reg = phys_reg((u8)b);
    in.ops[2].kind = A64O_MEM;
    in.ops[2].mem.base = phys_reg((u8)base);
    in.ops[2].mem.offset = offset;
    in.ops[2].mem.mode = (u8)mode;
    in.ops[2].mem.size = 8;
    return in;
}

static A64Inst mk_addsub_sp(u16 op, i64 imm)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.sf = A64_SF64;
    in.nops = 3;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = phys_reg(A64_SP);
    in.ops[1].kind = A64O_REG;
    in.ops[1].reg = phys_reg(A64_SP);
    in.ops[2].kind = A64O_IMM;
    in.ops[2].imm = imm;
    return in;
}

/* ADD/SUB immediate is a uimm12 optionally shifted left by 12. Consume an
 * arbitrary u32 adjustment in encodable chunks, largest first. SP stays
 * monotone in the direction of travel, which matters for a signal arriving
 * between instructions; this also lets an object at the supported alignment
 * boundary coexist with the saved-register area and ordinary locals. */
static void emit_sp_adjust(Rb *rb, u16 op, u32 amount)
{
    do {
        u32 hi = amount >> 12;
        u32 chunk;
        A64Inst in;

        if (hi > 0xfffu)
            hi = 0xfffu;
        chunk = hi ? hi << 12 : amount;
        in = mk_addsub_sp(op, chunk);
        rb_put(rb, &in);
        amount -= chunk;
    } while (amount);
}

/* Static allocas become ordinary frame objects. Their sizes are arbitrary, so
 * they cannot go through cg_spill_slot_assign, whose power-of-two contract
 * exists for register spills; the bump arithmetic shares the same
 * downward-growing cursor, which keeps allocas and spills disjoint.
 *
 * x29 is only guaranteed 16-byte alignment. For a stronger alignment reserve
 * align-1 bytes of slack and align the returned address inside that region;
 * realigning the whole frame would instead disturb unwind state, outgoing
 * arguments and every fixed spill offset. The marker records the region start
 * and requested alignment; its size is needed only while reserving space. */
static i32 frame_object_assign(CgSpillSlots *slots, u32 size, u32 align)
{
    u64 top, start, end;

    if (!align || (align & (align - 1)))
        CGF_ICE("arm64 regalloc: frame object alignment %u is not a power of "
                "two",
                align);
    if (!size)
        size = 1;
    top = (u32)(-slots->next_offset);
    if (align <= 16) {
        start = (top + align - 1) & ~(u64)(align - 1);
        end = start + size;
    } else {
        start = top;
        end = top + size + (u64)align - 1;
    }
    if (end > 0x7fffffffu)
        CGF_ICE("arm64 regalloc: frame object area exceeds 2 GiB");
    slots->next_offset = -(i32)end;
    slots->count++;
    return (i32)start;
}

static void frame_assign_allocas(A64Func *f, CgSpillSlots *slots)
{
    u32 bi, ii;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];

        for (ii = 0; ii < b->n; ii++) {
            A64Inst *in = &b->insts[ii];
            u32 size, align;

            if (in->op != A64_OP_ALLOCA)
                continue;
            if (in->nops != 3 || in->ops[1].kind != A64O_IMM ||
                in->ops[2].kind != A64O_IMM)
                CGF_ICE("arm64 regalloc: malformed static alloca marker");
            if (in->ops[1].imm < 0 || in->ops[1].imm > 0xffffffffu ||
                in->ops[2].imm <= 0 || in->ops[2].imm > 0xffffffffu)
                CGF_ICE("arm64 regalloc: static alloca size/alignment is out "
                        "of range");
            size = (u32)in->ops[1].imm;
            align = (u32)in->ops[2].imm;
            /* Stash the reserved region's start where the size was. */
            in->ops[1].imm =
                frame_object_assign(slots, size, align ? align : 8);
        }
    }
}

static A64Inst mk_add_imm(A64Reg dst, A64Reg base, i64 imm)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_ADD;
    in.sf = A64_SF64;
    in.nops = 3;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = dst;
    in.ops[1].kind = A64O_REG;
    in.ops[1].reg = base;
    in.ops[2].kind = A64O_IMM;
    in.ops[2].imm = imm;
    return in;
}

static void emit_add_imm_split(Rb *rb, A64Reg dst, A64Reg base, i64 imm,
                               const char *what);

/* A frame object's address is `x29 + off`, and ADD-immediate carries a
 * uimm12 optionally shifted left by 12 -- so an `off` that is neither <= 4095
 * nor a multiple of 4096 does not fit ONE instruction. An 8000-byte local
 * array reaches that with no help from register pressure, and the frame is
 * final here, so the offset cannot be made smaller.
 *
 * The adds run high chunks first, exactly as emit_sp_adjust splits the stack
 * adjustment. No scratch register is needed and none is available this late:
 * `dst` is being DEFINED by this instruction, so accumulating into it cannot
 * clobber anything live. */
static void frame_expand_allocas(A64Func *f, const Frame *fr)
{
    u32 bi, ii;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;
        bool any = false;

        for (ii = 0; ii < b->n; ii++)
            if (b->insts[ii].op == A64_OP_ALLOCA)
                any = true;
        if (!any)
            continue;
        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            A64Inst *in = &b->insts[ii];
            i64 start, align, off;
            A64Reg dst;

            rb.map[ii] = rb.n;
            rb.source_loc = in->loc;
            if (in->op != A64_OP_ALLOCA) {
                rb_put(&rb, in);
                continue;
            }
            start = in->ops[1].imm;
            align = in->ops[2].imm;
            off = (i64)fr->csr_size + start;
            dst = in->ops[0].reg;
            if (off < 0)
                CGF_ICE("arm64 regalloc: frame object at negative offset %lld",
                        (long long)off);
            emit_add_imm_split(&rb, dst, phys_reg(A64_X29), off,
                               "frame object");
            if (align > 16) {
                A64Inst mask;

                emit_add_imm_split(&rb, dst, dst, align - 1,
                                   "frame object alignment");
                memset(&mask, 0, sizeof(mask));
                mask.op = A64_OP_AND;
                mask.sf = A64_SF64;
                mask.nops = 3;
                mask.ops[0].kind = A64O_REG;
                mask.ops[0].reg = dst;
                mask.ops[1].kind = A64O_REG;
                mask.ops[1].reg = dst;
                mask.ops[2].kind = A64O_IMM;
                mask.ops[2].imm = -align;
                rb_put(&rb, &mask);
            }
        }
        rb_commit(&rb, b);
    }
}

/* `dst = base + imm`, accumulated in encodable ADD-immediate chunks. `dst` is
 * being defined, so it is safe to become the base after the first chunk and
 * no late scratch register is required. */
static void emit_add_imm_split(Rb *rb, A64Reg dst, A64Reg base, i64 imm,
                               const char *what)
{
    u64 left;
    A64Reg src = base;

    if (imm < 0)
        CGF_ICE("arm64 regalloc: negative %s offset %lld", what,
                (long long)imm);
    left = (u64)imm;
    do {
        u64 hi = left >> 12;
        u64 chunk;
        A64Inst add;

        if (hi > 0xfffu)
            hi = 0xfffu;
        chunk = hi ? hi << 12 : left;
        add = mk_add_imm(dst, src, (i64)chunk);
        rb_put(rb, &add);
        left -= chunk;
        src = dst;
    } while (left);
}

/* Dynamic stack: VLAs and the stacksave/stackrestore pair that scopes them.
 *
 * A dynamic alloca lowers to `sub sp, sp, rounded_size`, and the object it
 * returns must sit ABOVE the outgoing-argument area rather than at the new
 * SP. Stack arguments are stored at [sp + k] against the CURRENT SP, so
 * handing back a bare SP puts the fresh object exactly where the next call
 * writes its arguments -- a VLA silently overwritten by an argument list.
 * x86 shipped that bug; this backend never got the chance, because it ICEd
 * on the whole construct instead.
 *
 * `out` is a multiple of 16, so folding it into the round-up addend is exact
 * and SP stays 16-aligned -- which AArch64 checks in hardware at every
 * SP-based access, so getting it wrong faults rather than limping.
 *
 * `dst` is DEFINED here, so using it as the scratch cannot clobber anything
 * live; and `add dst, count, #n` reads count before writing dst, so the two
 * being the same register (count dying at this instruction) is fine.
 *
 * SP still needs a second register. Encoding 31 names SP or XZR from the
 * instruction POSITION, and the only add/sub shapes that read it as SP are
 * the immediate and extended-register forms -- the emitter has no extended
 * form, so `sub sp, sp, xN` is unrepresentable and the subtraction detours
 * through a scratch. The verifier rejects the direct spelling outright,
 * which is how this was caught rather than assembled into nonsense. */

/* A reload scratch that is neither `dst` nor `count`. Two candidates would
 * do; three is the same belt-and-braces as A64_VA_SCRATCH_ALT. Every scratch
 * is dead here: the rewrite emits a reload and its single consumer together,
 * and this expansion replaces that consumer. */
static A64PhysReg dyn_scratch(A64Reg dst, A64Reg count)
{
    static const u8 cand[3] = {A64_X16, A64_X17, A64_X15};
    u32 i;

    for (i = 0; i < 3; i++) {
        u32 id = (u32)cand[i] + 1;

        if ((!dst.physical || dst.id != id) &&
            (!count.physical || count.id != id))
            return (A64PhysReg)cand[i];
    }
    CGF_ICE("arm64 regalloc: no free scratch for a dynamic alloca");
}
static void frame_expand_dynamic(A64Func *f, const Frame *fr)
{
    u32 bi, ii;
    i64 out = (i64)fr->base;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;
        bool any = false;

        for (ii = 0; ii < b->n; ii++) {
            u16 op = b->insts[ii].op;

            if (op == A64_OP_ALLOCA_DYN || op == A64_OP_STACKSAVE ||
                op == A64_OP_STACKRESTORE)
                any = true;
        }
        if (!any)
            continue;
        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            A64Inst *in = &b->insts[ii];
            A64Inst x;
            A64Reg dst, tmp;

            rb.map[ii] = rb.n;
            rb.source_loc = in->loc;
            if (in->op == A64_OP_STACKSAVE) {
                /* `mov dst, sp` is the ADD-immediate alias. */
                x = mk_add_imm(in->ops[0].reg, phys_reg(A64_SP), 0);
                rb_put(&rb, &x);
                continue;
            }
            if (in->op == A64_OP_STACKRESTORE) {
                x = mk_add_imm(phys_reg(A64_SP), in->ops[0].reg, 0);
                rb_put(&rb, &x);
                continue;
            }
            if (in->op != A64_OP_ALLOCA_DYN) {
                rb_put(&rb, in);
                continue;
            }
            if (in->nops != 3 || in->ops[1].kind != A64O_REG ||
                in->ops[2].kind != A64O_IMM)
                CGF_ICE("arm64 regalloc: malformed dynamic alloca marker");
            if (in->ops[2].imm <= 0 ||
                ((u64)in->ops[2].imm & ((u64)in->ops[2].imm - 1)))
                CGF_ICE("arm64 regalloc: dynamic alloca alignment %lld is not "
                        "a positive power of two",
                        (long long)in->ops[2].imm);
            dst = in->ops[0].reg;
            tmp = phys_reg(dyn_scratch(dst, in->ops[1].reg));
            /* Reserve count + outgoing area + alignment slack, rounded so SP
             * retains its mandatory 16-byte alignment. */
            emit_add_imm_split(
                &rb, dst, in->ops[1].reg,
                15 + out + (in->ops[2].imm > 16 ? in->ops[2].imm - 1 : 0),
                "dynamic alloca round-up");
            memset(&x, 0, sizeof(x));
            x.op = A64_OP_AND;
            x.sf = A64_SF64;
            x.nops = 3;
            x.ops[0].kind = A64O_REG;
            x.ops[0].reg = dst;
            x.ops[1].kind = A64O_REG;
            x.ops[1].reg = dst;
            x.ops[2].kind = A64O_IMM;
            x.ops[2].imm = -16;
            rb_put(&rb, &x);
            /* sp = sp - dst, through the scratch */
            x = mk_add_imm(tmp, phys_reg(A64_SP), 0);
            rb_put(&rb, &x);
            memset(&x, 0, sizeof(x));
            x.op = A64_OP_SUB;
            x.sf = A64_SF64;
            x.nops = 3;
            x.ops[0].kind = A64O_REG;
            x.ops[0].reg = tmp;
            x.ops[1].kind = A64O_REG;
            x.ops[1].reg = tmp;
            x.ops[2].kind = A64O_REG;
            x.ops[2].reg = dst;
            rb_put(&rb, &x);
            x = mk_add_imm(phys_reg(A64_SP), tmp, 0);
            rb_put(&rb, &x);
            /* Return an aligned address above the outgoing argument area. */
            emit_add_imm_split(
                &rb, dst, phys_reg(A64_SP),
                out + (in->ops[2].imm > 16 ? in->ops[2].imm - 1 : 0),
                "dynamic alloca outgoing area");
            if (in->ops[2].imm > 16) {
                memset(&x, 0, sizeof(x));
                x.op = A64_OP_AND;
                x.sf = A64_SF64;
                x.nops = 3;
                x.ops[0].kind = A64O_REG;
                x.ops[0].reg = dst;
                x.ops[1].kind = A64O_REG;
                x.ops[1].reg = dst;
                x.ops[2].kind = A64O_IMM;
                x.ops[2].imm = -in->ops[2].imm;
                rb_put(&rb, &x);
            }
        }
        rb_commit(&rb, b);
    }
}

/* Slot references leave the rewrite as [x29, #negative]; nothing else in the
 * A64 stream can produce one, so recognizing them is unambiguous. */
static void frame_fixup_slots(A64Func *f, const Frame *fr)
{
    u32 bi, ii, i;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];

        for (ii = 0; ii < b->n; ii++) {
            A64Inst *in = &b->insts[ii];

            for (i = 0; i < in->nops; i++) {
                A64Operand *op = &in->ops[i];
                i64 slot;

                if (op->kind != A64O_MEM || !op->mem.base.physical ||
                    op->mem.base.id != (u32)A64_X29 + 1)
                    continue;
                if (op->mem.mode == A64_ADDR_INCOMING) {
                    /* Incoming arguments sit immediately above the frame, and
                     * x29 is `base` bytes into it. */
                    op->mem.offset += (i64)fr->total - (i64)fr->base;
                    op->mem.mode = (u8)a64_isel_addr(
                        op->mem.offset, op->mem.size, false, false);
                    continue;
                }
                if (op->mem.offset >= 0)
                    continue;
                slot = -op->mem.offset; /* 8, 16, 24, ... */
                /* x29 sits at SP+base, so the base cancels: a slot is
                 * addressed from the frame pointer by its distance above the
                 * saved-register area alone. */
                op->mem.offset = (i64)fr->csr_size + slot -
                                 (i64)(op->mem.size ? op->mem.size : 8);
                op->mem.mode = (u8)a64_isel_addr(op->mem.offset, op->mem.size,
                                                 false, false);
            }
        }
    }
}

/* A scaled 64-bit load/store reaches 4095 * 8 bytes from its base. Large
 * functions can exceed that in three frame-generated places: x29-relative
 * spill/incoming slots, SP-relative outgoing arguments, and the variadic
 * register-save area. Curl's generated lib1521.c first exposed the spill case
 * at about 37 KiB. Their builders deliberately ask a64_isel_addr for the
 * honest answer, MATERIALIZE, and this final frame pass pays every such debt
 * before verification.
 *
 * x12 is backend-owned: ordinary allocation, fixed-register construction,
 * and GNU local-register binding all reject it. The only frame-slot
 * references produced by the rewrite are LOAD/STORE instructions, so its
 * address lifetime ends at that one memory instruction; an atomic pseudo is a
 * different MIR instruction and may safely reuse x12 while it expands at
 * emission. Keeping this restriction explicit is stronger than choosing an
 * apparently unused reload scratch, which could still hold an earlier reload
 * until the original instruction executes. */
#define A64_FRAME_ADDR_SCRATCH A64_X12

static void frame_expand_mem_addresses(A64Func *f)
{
    u32 bi, ii, i;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;
        bool any = false;

        for (ii = 0; ii < b->n; ii++)
            for (i = 0; i < b->insts[ii].nops; i++)
                if (b->insts[ii].ops[i].kind == A64O_MEM &&
                    b->insts[ii].ops[i].mem.mode == A64_ADDR_MATERIALIZE)
                    any = true;
        if (!any)
            continue;

        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            A64Inst in = b->insts[ii];
            A64Operand *mem = NULL;

            rb.source_loc = in.loc;
            for (i = 0; i < in.nops; i++) {
                if (in.ops[i].kind != A64O_MEM ||
                    in.ops[i].mem.mode != A64_ADDR_MATERIALIZE)
                    continue;
                if (mem)
                    CGF_ICE("arm64 regalloc: instruction has multiple "
                            "materialized frame references");
                mem = &in.ops[i];
            }
            if (mem) {
                A64Reg scratch = phys_reg(A64_FRAME_ADDR_SCRATCH);

                if ((in.op != A64_OP_LOAD && in.op != A64_OP_STORE) ||
                    in.nops != 2 || mem != &in.ops[1] ||
                    in.ops[0].kind != A64O_REG || !in.ops[0].reg.physical ||
                    in.ops[0].reg.id == scratch.id || !mem->mem.base.physical ||
                    (mem->mem.base.id != (u32)A64_X29 + 1 &&
                     mem->mem.base.id != (u32)A64_SP + 1) ||
                    mem->mem.offset < 0)
                    CGF_ICE("arm64 regalloc: malformed materialized frame "
                            "reference");
                emit_add_imm_split(&rb, scratch, mem->mem.base, mem->mem.offset,
                                   "frame memory");
                mem->mem.base = scratch;
                mem->mem.offset = 0;
                mem->mem.mode = A64_ADDR_SCALED;
            }
            rb.map[ii] = rb.n;
            rb_put(&rb, &in);
        }
        rb_commit(&rb, b);
    }
}

/* The AAPCS64 register save area, which only a variadic function needs.
 * Registers the named parameters already consumed are not saved — those
 * values are dead here — but both banks are otherwise dumped in full. gcc
 * additionally drops the whole vector half when it can prove the callee
 * takes no floating variadic argument; that is an optimization, and getting
 * the proof wrong yields garbage rather than slow code, so it waits.
 *
 * Geometry, from the top of the frame down, because __gr_top and __vr_top
 * name the ENDS of their areas and the offsets reach back from there:
 *
 *     total          <- __vr_top, and the first incoming stack argument
 *     total - 128    <- __gr_top; q0-q7 live above this
 *     total - 192       x0-x7 live above this
 *
 * gcc orders the two areas the other way round (vector below general). The
 * order is unobservable — both tops are stored into the va_list as plain
 * pointers and va_arg only ever computes top+offset — so this is a layout
 * choice, not a compatibility one. Verified self-consistent: the first
 * unnamed general argument sits at gr_top - 8*(8-named), which is exactly
 * where __gr_offs starts.
 *
 * The vector slots are 16 bytes apart and the prologue writes the whole q
 * register. A variadic float or double still occupies only the low part of
 * that slot on our little-endian targets, while binary128 needs all 16 bytes.
 * Saving only d0-d7 silently truncated `_Float64x`/`_Float128` va_args. */
#define A64_VA_GP_BYTES 64
#define A64_VA_FP_BYTES 128
#define A64_VA_SAVE_BYTES (A64_VA_GP_BYTES + A64_VA_FP_BYTES)

/* `add dst, src, #imm` in at most two instructions, mirroring the SP
 * adjustment's uimm12-plus-shifted-uimm12 reach. */
static void emit_add_imm(Rb *rb, A64PhysReg dst, A64PhysReg src, u32 imm)
{
    u32 hi = imm >> 12;
    u32 lo = imm & 0xfffu;
    A64Inst in;

    if (hi > 0xfffu)
        CGF_ICE("arm64 regalloc: frame offset %u exceeds two-instruction "
                "address formation",
                imm);
    memset(&in, 0, sizeof(in));
    in.op = A64_OP_ADD;
    in.sf = A64_SF64;
    in.nops = 3;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = phys_reg((u8)dst);
    in.ops[1].kind = A64O_REG;
    in.ops[1].reg = phys_reg((u8)src);
    in.ops[2].kind = A64O_IMM;
    if (hi) {
        in.ops[2].imm = (i64)(hi << 12);
        rb_put(rb, &in);
        if (!lo)
            return;
        in.ops[1].reg = phys_reg((u8)dst);
    }
    in.ops[2].imm = (i64)lo;
    rb_put(rb, &in);
}

static A64Inst mk_store_at(A64PhysReg value, A64PhysReg base, u32 offset,
                           bool fp)
{
    A64Inst in;
    u8 size = fp ? 16 : 8;

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_STORE;
    in.sf = fp ? A64_SF128 : A64_SF64;
    in.nops = 2;
    in.ops[0].kind = A64O_REG;
    in.ops[0].reg = phys_reg((u8)value);
    in.ops[1].kind = A64O_MEM;
    in.ops[1].mem.base = phys_reg((u8)base);
    in.ops[1].mem.offset = (i64)offset;
    in.ops[1].mem.size = size;
    in.ops[1].mem.mode = (u8)a64_isel_addr((i64)offset, size, false, false);
    return in;
}

/* x16 is IP0, a reload scratch: nothing is live in it BETWEEN instructions,
 * which is what a post-allocation expansion needs -- but the va_list pointer
 * this expansion writes THROUGH is an operand of the va_start itself, so it
 * is live INTO it, and if it spilled the rewrite pass reloaded it into that
 * very scratch. `add x16, x29, #336` then destroyed the base and the stores
 * went to `[x16, #8]` relative to the value just computed. x17 is IP1 and is
 * the b-side reload scratch, so it is equally free and cannot be the same
 * register. */
#define A64_VA_SCRATCH A64_X16
#define A64_VA_SCRATCH_ALT A64_X17

static A64PhysReg va_scratch_for(A64PhysReg va_list_ptr)
{
    return va_list_ptr == A64_VA_SCRATCH ? A64_VA_SCRATCH_ALT : A64_VA_SCRATCH;
}

static void frame_expand_vastart(A64Func *f, const Frame *fr)
{
    u32 bi, ii;
    bool any = false;

    for (bi = 0; bi < f->nblocks && !any; bi++)
        for (ii = 0; ii < f->blocks[bi].n; ii++)
            if (f->blocks[bi].insts[ii].op == A64_OP_VASTART)
                any = true;
    if (!any)
        return;
    if (!f->variadic)
        CGF_ICE("arm64 regalloc: va_start in a non-variadic function");

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;

        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            A64Inst *in = &b->insts[ii];
            A64PhysReg ap, scratch;
            u32 vr_top, gr_top;

            rb.map[ii] = rb.n;
            rb.source_loc = in->loc;
            if (in->op != A64_OP_VASTART) {
                rb_put(&rb, in);
                continue;
            }
            if (!in->nops || in->ops[0].kind != A64O_REG ||
                !in->ops[0].reg.physical)
                CGF_ICE("arm64 regalloc: va_start lost its va_list pointer");
            ap = (A64PhysReg)(in->ops[0].reg.id - 1);
            scratch = va_scratch_for(ap);
            /* x29 sits `base` bytes into the frame. */
            vr_top = fr->total - fr->base;
            gr_top = vr_top - A64_VA_FP_BYTES;

            /* Apple: one cursor, pointed at the first incoming stack
             * argument, which is the top of the frame. No save area exists
             * to describe, so there are no tops and no offsets -- the whole
             * five-field dance below is AAPCS64's alone. */
            if (cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS) {
                A64Inst st;

                emit_add_imm(&rb, scratch, A64_X29, vr_top + f->va_named_stack);
                st = mk_store_at(scratch, ap, 0, false);
                rb_put(&rb, &st);
                continue;
            }
            emit_add_imm(&rb, scratch, A64_X29, gr_top);
            {
                A64Inst st = mk_store_at(scratch, ap, 8, false);

                rb_put(&rb, &st);
            }
            emit_add_imm(&rb, scratch, A64_X29, vr_top);
            {
                A64Inst st = mk_store_at(scratch, ap, 16, false);

                rb_put(&rb, &st);
                /* __stack is the first ANONYMOUS incoming stack argument.
                 * That is the top of the frame only when no named parameter
                 * was stacked; when one was, it sits past them. Selection
                 * used to hard-error here instead. */
                if (f->va_named_stack)
                    emit_add_imm(&rb, scratch, A64_X29,
                                 vr_top + f->va_named_stack);
                st = mk_store_at(scratch, ap, 0, false);
                rb_put(&rb, &st);
            }
        }
        rb_commit(&rb, b);
    }
}

static void frame_emit_save_area(A64Func *f, const Frame *fr, Rb *rb)
{
    u32 base = fr->total - A64_VA_SAVE_BYTES;
    u32 i;

    for (i = f->va_named_gp; i < 8; i++) {
        A64Inst in =
            mk_store_at((A64PhysReg)(A64_X0 + i), A64_SP, base + 8 * i, false);

        rb_put(rb, &in);
    }
    for (i = f->va_named_fp; i < 8; i++) {
        A64Inst in = mk_store_at((A64PhysReg)(A64_V0 + i), A64_SP,
                                 base + A64_VA_GP_BYTES + 16 * i, true);

        rb_put(rb, &in);
    }
}

static void frame_emit_prologue(A64Func *f, const Frame *fr)
{
    Rb rb;
    A64Block *b = &f->blocks[0];
    u32 off, i;
    A64PhysReg csr_base;

    rb_init(&rb, f->arena, b->n);
    /* The one-instruction pre-index form is only available when the saved
     * pair belongs at SP+0. As soon as this function passes an argument on
     * the stack, the outgoing area owns SP+0 — the callee reads its stack
     * arguments from exactly there — so the pair moves up and the frame is
     * allocated by a separate subtraction. */
    /* Recorded for .eh_frame: which shape ran, and how many instructions
     * precede the pair store. The pre-indexed form adjusts SP and saves in
     * ONE instruction, so the CFA change and the register rules take effect
     * together; the separate form moves SP first and the two steps need
     * different advance_loc values. See a64_emit_eh_frame. */
    f->cfi_frame = fr->total;
    f->cfi_pair_off = fr->base;
    f->cfi_pair_pre_insns = 0;
    f->cfi_sp_offsets = NULL;
    if (fr->base == 0 && fr->total <= A64_FRAME_PREINDEX_MAX) {
        A64Inst in = mk_pair(A64_OP_STP, A64_X29, A64_X30, A64_SP,
                             -(i64)fr->total, A64_ADDR_PRE);

        f->cfi_pre_insns = 0;
        rb_put(&rb, &in);
        emit_add_imm(&rb, A64_X29, A64_SP, 0);
    } else {
        u32 before = rb.n;

        emit_sp_adjust(&rb, A64_OP_SUB, fr->total);
        f->cfi_pre_insns = rb.n - before;
        f->cfi_sp_offsets = arena_alloc(
            f->arena, (size_t)f->cfi_pre_insns * sizeof(*f->cfi_sp_offsets),
            _Alignof(u32));
        for (i = 0; i < f->cfi_pre_insns; i++) {
            const A64Inst *adj = &rb.v[before + i];

            if (adj->op != A64_OP_SUB || adj->nops != 3 ||
                adj->ops[0].kind != A64O_REG ||
                adj->ops[0].reg.id != (u32)A64_SP + 1 ||
                adj->ops[2].kind != A64O_IMM || adj->ops[2].imm <= 0)
                CGF_ICE("arm64 regalloc: malformed prologue SP adjustment");
            f->cfi_sp_offsets[i] =
                (i ? f->cfi_sp_offsets[i - 1] : 0) + (u32)adj->ops[2].imm;
        }
        if (f->cfi_sp_offsets[f->cfi_pre_insns - 1] != fr->total)
            CGF_ICE("arm64 regalloc: CFI SP adjustments cover %u of %u bytes",
                    f->cfi_sp_offsets[f->cfi_pre_insns - 1], fr->total);
        if (fr->base <= A64_FRAME_PREINDEX_MAX) {
            A64Inst in = mk_pair(A64_OP_STP, A64_X29, A64_X30, A64_SP,
                                 (i64)fr->base, A64_ADDR_SCALED);

            rb_put(&rb, &in);
            emit_add_imm(&rb, A64_X29, A64_SP, fr->base);
        } else {
            A64Inst in;

            before = rb.n;
            emit_add_imm(&rb, A64_X16, A64_SP, fr->base);
            f->cfi_pair_pre_insns = (u8)(rb.n - before);
            in = mk_pair(A64_OP_STP, A64_X29, A64_X30, A64_X16, 0,
                         A64_ADDR_SCALED);
            rb_put(&rb, &in);
            emit_add_imm(&rb, A64_X29, A64_X16, 0);
        }
    }
    csr_base = A64_X29;
    off = 16;
    for (i = 0; i + 1 < fr->ngp; i += 2, off += 16) {
        A64Inst in =
            mk_pair(A64_OP_STP, (A64PhysReg)fr->gp[i],
                    (A64PhysReg)fr->gp[i + 1], csr_base, off, A64_ADDR_SCALED);

        rb_put(&rb, &in);
    }
    if (i < fr->ngp) {
        A64Inst in =
            mk_mem_op(A64_OP_STORE, A64_SF64, phys_reg(fr->gp[i]), (i32)off);

        in.ops[1].mem.base = phys_reg((u8)csr_base);
        in.ops[1].mem.mode = (u8)a64_isel_addr((i64)off, 8, false, false);
        rb_put(&rb, &in);
        off += 8;
    }
    for (i = 0; i + 1 < fr->nfp; i += 2, off += 16) {
        A64Inst in =
            mk_pair(A64_OP_STP, (A64PhysReg)fr->fp[i],
                    (A64PhysReg)fr->fp[i + 1], csr_base, off, A64_ADDR_SCALED);

        rb_put(&rb, &in);
    }
    if (i < fr->nfp) {
        A64Inst in =
            mk_mem_op(A64_OP_STORE, A64_SF64, phys_reg(fr->fp[i]), (i32)off);

        in.ops[1].mem.base = phys_reg((u8)csr_base);
        in.ops[1].mem.mode = (u8)a64_isel_addr((i64)off, 8, false, false);
        rb_put(&rb, &in);
    }
    /* Apple passes every anonymous argument on the stack, so a variadic
     * function there needs no register save area at all. */
    if (f->variadic && cgf_target_selected().kind != CGF_TARGET_ARM64_MACOS)
        frame_emit_save_area(f, fr, &rb);
    for (i = 0; i < b->n; i++) {
        A64Inst original = b->insts[i];

        rb.map[i] = rb.n;
        rb.source_loc = original.loc;
        if (i == 0) {
            f->cfi_body_label = ++f->cfi_next_label;
            original.cfi_label = f->cfi_body_label;
        }
        rb_put(&rb, &original);
    }
    rb_commit(&rb, b);
}

static void frame_emit_epilogue(A64Func *f, const Frame *fr)
{
    u32 bi, nret = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        u32 ii;

        for (ii = 0; ii < f->blocks[bi].n; ii++)
            if (f->blocks[bi].insts[ii].op == A64_OP_RET)
                nret++;
    }
    f->cfi_epilogues = arena_alloc(
        f->arena, (size_t)(nret ? nret : 1) * sizeof(*f->cfi_epilogues),
        _Alignof(A64CfiEpilogue));
    f->cfi_nepilogues = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;
        u32 ii;

        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            A64CfiEpilogue *ep;
            u32 off, i;

            rb.map[ii] = rb.n;
            rb.source_loc = b->insts[ii].loc;
            if (b->insts[ii].op != A64_OP_RET) {
                rb_put(&rb, &b->insts[ii]);
                continue;
            }
            ep = &f->cfi_epilogues[f->cfi_nepilogues++];
            memset(ep, 0, sizeof(*ep));
            /* x29 points at the saved pair and never moves, so every
             * callee-saved reload remains encodable even when a large
             * outgoing area puts that pair far above SP. */
            off = 16;
            for (i = 0; i + 1 < fr->ngp; i += 2, off += 16) {
                A64Inst in = mk_pair(A64_OP_LDP, (A64PhysReg)fr->gp[i],
                                     (A64PhysReg)fr->gp[i + 1], A64_X29, off,
                                     A64_ADDR_SCALED);

                rb_put(&rb, &in);
            }
            if (i < fr->ngp) {
                A64Inst in = mk_mem_op(A64_OP_LOAD, A64_SF64,
                                       phys_reg(fr->gp[i]), (i32)off);

                in.ops[1].mem.base = phys_reg(A64_X29);
                in.ops[1].mem.mode =
                    (u8)a64_isel_addr((i64)off, 8, false, false);
                rb_put(&rb, &in);
                off += 8;
            }
            for (i = 0; i + 1 < fr->nfp; i += 2, off += 16) {
                A64Inst in = mk_pair(A64_OP_LDP, (A64PhysReg)fr->fp[i],
                                     (A64PhysReg)fr->fp[i + 1], A64_X29, off,
                                     A64_ADDR_SCALED);

                rb_put(&rb, &in);
            }
            if (i < fr->nfp) {
                A64Inst in = mk_mem_op(A64_OP_LOAD, A64_SF64,
                                       phys_reg(fr->fp[i]), (i32)off);

                in.ops[1].mem.base = phys_reg(A64_X29);
                in.ops[1].mem.mode =
                    (u8)a64_isel_addr((i64)off, 8, false, false);
                rb_put(&rb, &in);
            }
            /* A VLA may have moved SP since entry. Recover the frame bottom
             * before old x29 is reloaded and stops naming this frame. */
            if (fr->dynamic_sp) {
                A64Inst mv = mk_add_imm(phys_reg(A64_SP), phys_reg(A64_X29), 0);

                rb_put(&rb, &mv);
                if (fr->base)
                    emit_sp_adjust(&rb, A64_OP_SUB, fr->base);
            }
            if (fr->base == 0 && fr->total <= A64_FRAME_PREINDEX_MAX) {
                A64Inst in = mk_pair(A64_OP_LDP, A64_X29, A64_X30, A64_SP,
                                     (i64)fr->total, A64_ADDR_POST);

                ep->before_pair = ++f->cfi_next_label;
                ep->after_pair = ++f->cfi_next_label;
                in.cfi_label = ep->before_pair;
                in.cfi_after_label = ep->after_pair;
                rb_put(&rb, &in);
            } else {
                A64Inst in = mk_pair(A64_OP_LDP, A64_X29, A64_X30, A64_X29, 0,
                                     A64_ADDR_SCALED);
                u32 before, restored = 0;

                ep->before_pair = ++f->cfi_next_label;
                ep->after_pair = ++f->cfi_next_label;
                in.cfi_label = ep->before_pair;
                in.cfi_after_label = ep->after_pair;
                rb_put(&rb, &in);
                before = rb.n;
                emit_sp_adjust(&rb, A64_OP_ADD, fr->total);
                ep->nsp = rb.n - before;
                ep->sp_labels = arena_alloc(
                    f->arena, (size_t)ep->nsp * sizeof(*ep->sp_labels),
                    _Alignof(u32));
                ep->sp_offsets = arena_alloc(
                    f->arena, (size_t)ep->nsp * sizeof(*ep->sp_offsets),
                    _Alignof(u32));
                for (i = 0; i < ep->nsp; i++) {
                    A64Inst *adj = &rb.v[before + i];

                    if (adj->op != A64_OP_ADD || adj->nops != 3 ||
                        adj->ops[0].kind != A64O_REG ||
                        adj->ops[0].reg.id != (u32)A64_SP + 1 ||
                        adj->ops[1].kind != A64O_REG ||
                        adj->ops[1].reg.id != (u32)A64_SP + 1 ||
                        adj->ops[2].kind != A64O_IMM || adj->ops[2].imm <= 0 ||
                        (u64)restored + (u64)adj->ops[2].imm > fr->total)
                        CGF_ICE("arm64 regalloc: malformed epilogue SP "
                                "adjustment");
                    restored += (u32)adj->ops[2].imm;
                    ep->sp_labels[i] = ++f->cfi_next_label;
                    ep->sp_offsets[i] = fr->total - restored;
                    adj->cfi_after_label = ep->sp_labels[i];
                }
                if (restored != fr->total)
                    CGF_ICE("arm64 regalloc: epilogue CFI restores %u of %u "
                            "frame bytes",
                            restored, fr->total);
            }
            {
                A64Inst ret = b->insts[ii];

                ep->after_ret = ++f->cfi_next_label;
                ret.cfi_after_label = ep->after_ret;
                rb_put(&rb, &ret);
            }
        }
        rb_commit(&rb, b);
    }
}

static void frame_finalize(Ra *ra)
{
    A64Func *f = ra->f;
    Frame fr;

    memset(&fr, 0, sizeof(fr));
    frame_collect_saved(f, &fr);
    if (fr.ngp > CGF_ARRAY_LEN(f->cfi_gp) || fr.nfp > CGF_ARRAY_LEN(f->cfi_fp))
        CGF_ICE("arm64 regalloc: impossible callee-saved count");
    memcpy(f->cfi_gp, fr.gp, fr.ngp * sizeof(*fr.gp));
    memcpy(f->cfi_fp, fr.fp, fr.nfp * sizeof(*fr.fp));
    f->cfi_ngp = (u8)fr.ngp;
    f->cfi_nfp = (u8)fr.nfp;
    fr.base = f->out_args;
    fr.csr_size = 16 + fr.ngp * 8 + fr.nfp * 8;
    /* Round to 16, not 8. Frame objects are placed at `csr_size + offset`, so
     * this is the base their alignment is measured from -- an ODD number of
     * saved registers leaves csr_size at 8 mod 16 and every 16-aligned object
     * above it lands 8 bytes out. x86_64 already reserves this gap for the
     * same reason (vector homes and the variadic save area); arm64 did not,
     * and nothing noticed because until `_Alignas` reached an alloca no frame
     * object had ever asked for more than its type's natural alignment.
     *
     * The 8 bytes are never wasted in practice: a64_frame_total rounds the
     * whole frame to 16 anyway, so this only moves where the slack sits. */
    fr.csr_size = (fr.csr_size + 15) & ~15u;
    frame_assign_allocas(f, &ra->slots);
    fr.local_top = (u32)(-ra->slots.next_offset);
    fr.total = a64_frame_total(fr.base + fr.csr_size, fr.local_top,
                               f->variadic ? A64_VA_SAVE_BYTES : 0);
    if (fr.total & 15)
        CGF_ICE("arm64 regalloc: frame %u is not 16-byte aligned", fr.total);
    fr.dynamic_sp = frame_has_dynamic_sp(f);
    frame_fixup_slots(f, &fr);
    frame_expand_allocas(f, &fr);
    frame_expand_dynamic(f, &fr);
    frame_expand_vastart(f, &fr);
    if (f->nblocks)
        frame_emit_prologue(f, &fr);
    frame_emit_epilogue(f, &fr);
    frame_expand_mem_addresses(f);
    f->frame_bytes = fr.total;
}

/* --- entry ---------------------------------------------------------------- */

void a64_regalloc(A64Func *f)
{
    Ra ra;
    Rewriter rw;
    CgMirView view;
    u32 bi;

    if (f->allocated)
        CGF_ICE("arm64 regalloc: function @%s is already allocated", f->name);
    memset(&ra, 0, sizeof(ra));
    ra.f = f;
    ra.arena = f->arena;
    marshal_calls(f);
    ra.max_uses = compute_max_uses(f);
    view = a64_mir_view(&ra);
    ra.words = cg_liveness_words(&view);
    ra.live_in = cgf_xmalloc((size_t)ra.words * (f->nblocks ? f->nblocks : 1) *
                             sizeof(u64));
    ra.live_out = cgf_xmalloc((size_t)ra.words * (f->nblocks ? f->nblocks : 1) *
                              sizeof(u64));
    memset(ra.live_in, 0,
           (size_t)ra.words * (f->nblocks ? f->nblocks : 1) * sizeof(u64));
    memset(ra.live_out, 0,
           (size_t)ra.words * (f->nblocks ? f->nblocks : 1) * sizeof(u64));
    ra.iv = cgf_xmalloc((f->nvregs + 1) * sizeof(*ra.iv));

    cg_intervals_build(&view, ra.live_in, ra.live_out, ra.iv);
    if (f->vfixed) {
        u32 v;

        for (v = 1; v <= f->nvregs; v++)
            if (f->vfixed[v] && ra.iv[v].live)
                ra.iv[v].fixed = f->vfixed[v];
    }
    collect_no_spill(&ra);
    linear_scan(&ra);

    memset(&rw, 0, sizeof(rw));
    rw.ra = &ra;
    for (bi = 0; bi < f->nblocks; bi++)
        rewrite_block(&rw, &f->blocks[bi]);

    f->spill_bytes = (u32)(-ra.slots.next_offset);
    frame_finalize(&ra);
    (void)a64_peep_post_ra(f);
    f->allocated = true;

    free(ra.iv);
    free(ra.live_out);
    free(ra.live_in);
}
