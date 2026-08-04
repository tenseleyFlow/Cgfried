#include "cg/arm64/mir.h"

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
 *   x0-x13   allocatable. x0-x7 are the argument/result registers and x8 is
 *            the indirect-result register; none of that constrains ordinary
 *            temporaries, so they allocate freely and the call marshalling
 *            pass pre-colors what it needs.
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
 *   v0-v28   allocatable; v8-v15 are callee-saved but ONLY their low 64 bits.
 *   v29-v31  RESERVED as FP reload scratches (FCSEL is def + two sources).
 *
 * The d8-d15 rule: AAPCS64 obliges the callee to preserve d8-d15, i.e. bits
 * 0-63 of v8-v15. The upper halves are caller-saved. So (a) the prologue
 * saves 8-byte halves, and (b) a 128-bit value live across a call must never
 * be placed there expecting preservation. Every A64 vreg is currently 32 or
 * 64 bits wide, so (b) cannot yet be violated; a64_reg_preserved_across_call
 * states the rule anyway and is unit-tested directly, so Sprint 49's NEON
 * values inherit it rather than rediscovering it. */

#define A64_NGP_SCRATCH 4
#define A64_NFP_SCRATCH 3

static const u8 gp_scratch[A64_NGP_SCRATCH] = {A64_X16, A64_X17, A64_X15,
                                               A64_X14};
static const u8 fp_scratch[A64_NFP_SCRATCH] = {A64_V31, A64_V30, A64_V29};

/* Caller-saved first: they are free in a leaf range, and a call-crossing
 * interval is steered to the callee-saved tail by reg_usable anyway. */
static const u8 gp_order[] = {
    A64_X0,  A64_X1,  A64_X2,  A64_X3,  A64_X4,  A64_X5,  A64_X6,  A64_X7,
    A64_X8,  A64_X9,  A64_X10, A64_X11, A64_X12, A64_X13, A64_X19, A64_X20,
    A64_X21, A64_X22, A64_X23, A64_X24, A64_X25, A64_X26, A64_X27, A64_X28,
};
#define A64_NGP ((u32)(sizeof(gp_order) / sizeof(gp_order[0])))

static const u8 fp_order[] = {
    A64_V0,  A64_V1,  A64_V2,  A64_V3,  A64_V4,  A64_V5,  A64_V6,  A64_V7,
    A64_V16, A64_V17, A64_V18, A64_V19, A64_V20, A64_V21, A64_V22, A64_V23,
    A64_V24, A64_V25, A64_V26, A64_V27, A64_V28, A64_V8,  A64_V9,  A64_V10,
    A64_V11, A64_V12, A64_V13, A64_V14, A64_V15,
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
    u32 *call_pts;
    u32 ncalls;
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

static void collect_calls(Ra *ra)
{
    u32 bi, ii, point = 0, cap = 0;

    for (bi = 0; bi < ra->f->nblocks; bi++)
        cap += ra->f->blocks[bi].n;
    ra->call_pts = cap ? cgf_xmalloc(cap * sizeof(u32)) : NULL;
    for (bi = 0; bi < ra->f->nblocks; bi++) {
        const A64Block *b = &ra->f->blocks[bi];

        for (ii = 0; ii < b->n; ii++)
            if (b->insts[ii].op == A64_OP_CALL)
                ra->call_pts[ra->ncalls++] = point + ii;
        point += b->n ? b->n : 0;
    }
}

/* Strictly spanning a call means the value must survive the clobber. A
 * result defined at the call, or an argument whose interval ends there, does
 * not span it. */
static bool crosses_call(const Ra *ra, u32 start, u32 end)
{
    u32 i;

    for (i = 0; i < ra->ncalls; i++)
        if (start < ra->call_pts[i] && ra->call_pts[i] < end)
            return true;
    return false;
}

/* --- linear scan policy --------------------------------------------------- */

static bool vreg_is_fp(const A64Func *f, u32 vreg)
{
    return a64_vclass(f, (A64Reg){vreg, 0}) == A64RC_FP;
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

static bool a64_scan_reg_usable(void *ctx, u32 vreg, u8 reg, u32 start, u32 end)
{
    Ra *ra = ctx;

    if (vreg_is_fp(ra->f, vreg) != (reg >= A64_V0 && reg <= A64_V31))
        return false;
    if (crosses_call(ra, start, end))
        return a64_reg_preserved_across_call(reg, false);
    return true;
}

/* Eight bytes covers every value the A64 backend has today: a w-register
 * def zero-extends into its x view, and an s-register def zeroes the rest of
 * the d view, so the slot always captures the whole architectural value.
 * Sprint 49's 128-bit vectors widen this to 16. */
static u32 a64_scan_spill_size(void *ctx, u32 vreg)
{
    (void)ctx;
    (void)vreg;
    return 8;
}

static u32 a64_scan_spill_align(void *ctx, u32 vreg)
{
    (void)ctx;
    (void)vreg;
    return 8;
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
    in.ops[1].mem.mode =
        a64_isel_addr(offset, 8, false, false) == A64_ADDR_SCALED
            ? A64_ADDR_SCALED
            : A64_ADDR_UNSCALED;
    in.ops[1].mem.size = 8;
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

        rb.map[ii] = rb.n;
        rb.source_loc = in.loc;
        rw->gp_next = 0;
        rw->fp_next = 0;

        if (in.op == A64_OP_CALL)
            CGF_ICE("arm64 regalloc: AAPCS64 call marshalling is not wired "
                    "up yet (Sprint 48 deliverable 6)");

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
                 * the bank, exactly as the printer reads it. */
                reload = mk_mem_op(A64_OP_LOAD, A64_SF64, phys_reg(scratch),
                                   ra->iv[v].slot);
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
                def_scratch = take_scratch(rw, vreg_is_fp(ra->f, def));
                in.ops[0].reg = phys_reg(def_scratch);
            } else {
                in.ops[0].reg = phys_reg((u8)(ra->iv[def].phys - 1));
            }
        }
        rb_put(&rb, &in);
        if (def_spilled) {
            A64Inst store = mk_mem_op(A64_OP_STORE, A64_SF64,
                                      phys_reg(def_scratch), ra->iv[def].slot);

            rb_put(&rb, &store);
        }
    }
    rb_commit(&rb, b);
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
 * -512; a larger frame subtracts from SP first and stores at #0.
 *
 * Layout from the new SP upward: x29/x30, callee-saved GP, callee-saved
 * d-halves, then spill slots and static allocas. SP stays 16-byte aligned
 * because AArch64 CHECKS it in hardware at every SP-based access — a
 * misaligned SP faults at the next load rather than limping the way a
 * misaligned rsp does. a64_frame_total rounds and the verifier re-checks. */

#define A64_FRAME_PREINDEX_MAX 512

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
    u32 csr_end;   /* first byte above the saved-register area */
    u32 total;     /* whole frame, a multiple of 16 */
    u32 local_top; /* bytes of spill/alloca area */
} Frame;

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
                    op->mem.base.id != (u32)A64_X29 + 1 || op->mem.offset >= 0)
                    continue;
                slot = -op->mem.offset; /* 8, 16, 24, ... */
                op->mem.offset = (i64)fr->csr_end + slot - 8;
                op->mem.mode = (u8)a64_isel_addr(op->mem.offset, op->mem.size,
                                                 false, false);
            }
        }
    }
}

static void frame_emit_prologue(A64Func *f, const Frame *fr)
{
    Rb rb;
    A64Block *b = &f->blocks[0];
    u32 off, i;

    rb_init(&rb, f->arena, b->n);
    if (fr->total <= A64_FRAME_PREINDEX_MAX) {
        A64Inst in = mk_pair(A64_OP_STP, A64_X29, A64_X30, A64_SP,
                             -(i64)fr->total, A64_ADDR_PRE);

        rb_put(&rb, &in);
    } else {
        A64Inst sub = mk_addsub_sp(A64_OP_SUB, (i64)fr->total);
        A64Inst in =
            mk_pair(A64_OP_STP, A64_X29, A64_X30, A64_SP, 0, A64_ADDR_SCALED);

        rb_put(&rb, &sub);
        rb_put(&rb, &in);
    }
    {
        A64Inst mov;

        memset(&mov, 0, sizeof(mov));
        mov.op = A64_OP_ADD; /* `mov x29, sp` is the ADD-immediate alias */
        mov.sf = A64_SF64;
        mov.nops = 3;
        mov.ops[0].kind = A64O_REG;
        mov.ops[0].reg = phys_reg(A64_X29);
        mov.ops[1].kind = A64O_REG;
        mov.ops[1].reg = phys_reg(A64_SP);
        mov.ops[2].kind = A64O_IMM;
        rb_put(&rb, &mov);
    }
    off = 16;
    for (i = 0; i + 1 < fr->ngp; i += 2, off += 16) {
        A64Inst in =
            mk_pair(A64_OP_STP, (A64PhysReg)fr->gp[i],
                    (A64PhysReg)fr->gp[i + 1], A64_SP, off, A64_ADDR_SCALED);

        rb_put(&rb, &in);
    }
    if (i < fr->ngp) {
        A64Inst in =
            mk_mem_op(A64_OP_STORE, A64_SF64, phys_reg(fr->gp[i]), (i32)off);

        in.ops[1].mem.base = phys_reg(A64_SP);
        in.ops[1].mem.mode = (u8)a64_isel_addr((i64)off, 8, false, false);
        rb_put(&rb, &in);
        off += 8;
    }
    for (i = 0; i + 1 < fr->nfp; i += 2, off += 16) {
        A64Inst in =
            mk_pair(A64_OP_STP, (A64PhysReg)fr->fp[i],
                    (A64PhysReg)fr->fp[i + 1], A64_SP, off, A64_ADDR_SCALED);

        rb_put(&rb, &in);
    }
    if (i < fr->nfp) {
        A64Inst in =
            mk_mem_op(A64_OP_STORE, A64_SF64, phys_reg(fr->fp[i]), (i32)off);

        in.ops[1].mem.base = phys_reg(A64_SP);
        in.ops[1].mem.mode = (u8)a64_isel_addr((i64)off, 8, false, false);
        rb_put(&rb, &in);
    }
    for (i = 0; i < b->n; i++) {
        rb.map[i] = rb.n;
        rb.source_loc = b->insts[i].loc;
        rb_put(&rb, &b->insts[i]);
    }
    rb_commit(&rb, b);
}

static void frame_emit_epilogue(A64Func *f, const Frame *fr)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        Rb rb;
        u32 ii;

        rb_init(&rb, f->arena, b->n);
        for (ii = 0; ii < b->n; ii++) {
            u32 off, i;

            rb.map[ii] = rb.n;
            rb.source_loc = b->insts[ii].loc;
            if (b->insts[ii].op != A64_OP_RET) {
                rb_put(&rb, &b->insts[ii]);
                continue;
            }
            off = 16;
            for (i = 0; i + 1 < fr->ngp; i += 2, off += 16) {
                A64Inst in = mk_pair(A64_OP_LDP, (A64PhysReg)fr->gp[i],
                                     (A64PhysReg)fr->gp[i + 1], A64_SP, off,
                                     A64_ADDR_SCALED);

                rb_put(&rb, &in);
            }
            if (i < fr->ngp) {
                A64Inst in = mk_mem_op(A64_OP_LOAD, A64_SF64,
                                       phys_reg(fr->gp[i]), (i32)off);

                in.ops[1].mem.base = phys_reg(A64_SP);
                in.ops[1].mem.mode =
                    (u8)a64_isel_addr((i64)off, 8, false, false);
                rb_put(&rb, &in);
                off += 8;
            }
            for (i = 0; i + 1 < fr->nfp; i += 2, off += 16) {
                A64Inst in = mk_pair(A64_OP_LDP, (A64PhysReg)fr->fp[i],
                                     (A64PhysReg)fr->fp[i + 1], A64_SP, off,
                                     A64_ADDR_SCALED);

                rb_put(&rb, &in);
            }
            if (i < fr->nfp) {
                A64Inst in = mk_mem_op(A64_OP_LOAD, A64_SF64,
                                       phys_reg(fr->fp[i]), (i32)off);

                in.ops[1].mem.base = phys_reg(A64_SP);
                in.ops[1].mem.mode =
                    (u8)a64_isel_addr((i64)off, 8, false, false);
                rb_put(&rb, &in);
            }
            if (fr->total <= A64_FRAME_PREINDEX_MAX) {
                A64Inst in = mk_pair(A64_OP_LDP, A64_X29, A64_X30, A64_SP,
                                     (i64)fr->total, A64_ADDR_POST);

                rb_put(&rb, &in);
            } else {
                A64Inst in = mk_pair(A64_OP_LDP, A64_X29, A64_X30, A64_SP, 0,
                                     A64_ADDR_SCALED);
                A64Inst add = mk_addsub_sp(A64_OP_ADD, (i64)fr->total);

                rb_put(&rb, &in);
                rb_put(&rb, &add);
            }
            rb_put(&rb, &b->insts[ii]);
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
    if (fr.ngp > 16 || fr.nfp > 8)
        CGF_ICE("arm64 regalloc: impossible callee-saved count");
    fr.csr_end = 16 + fr.ngp * 8 + fr.nfp * 8;
    fr.csr_end = (fr.csr_end + 7) & ~7u;
    fr.local_top = (u32)(-ra->slots.next_offset);
    fr.total = a64_frame_total(fr.csr_end, fr.local_top, 0);
    if (fr.total & 15)
        CGF_ICE("arm64 regalloc: frame %u is not 16-byte aligned", fr.total);
    frame_fixup_slots(f, &fr);
    if (f->nblocks)
        frame_emit_prologue(f, &fr);
    frame_emit_epilogue(f, &fr);
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
    collect_calls(&ra);
    linear_scan(&ra);

    memset(&rw, 0, sizeof(rw));
    rw.ra = &ra;
    for (bi = 0; bi < f->nblocks; bi++)
        rewrite_block(&rw, &f->blocks[bi]);

    f->spill_bytes = (u32)(-ra.slots.next_offset);
    frame_finalize(&ra);
    f->allocated = true;

    free(ra.call_pts);
    free(ra.iv);
    free(ra.live_out);
    free(ra.live_in);
}
