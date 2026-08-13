#include "cg/x86_64/mir.h"

#include <string.h>

/* x86_64 instruction selection (Sprint 21): all INTEGER IR selects into
 * three-address MIR over vregs. FP and calls ICE naming Sprint 23; the
 * atomic/vararg/memcpy ops ICE naming Sprint 24 (they need emission
 * context). Stack ops (dynamic alloca, stacksave/restore) emit Sprint 22
 * markers regalloc expands; no text leaves the process until Sprint 24. */

bool x64_imm_fits_simm32(i64 v)
{
    return v >= -2147483648ll && v <= 2147483647ll;
}

bool x64_fold_ok(u8 scale, bool index_is_rsp, i64 disp)
{
    if (scale != 1 && scale != 2 && scale != 4 && scale != 8)
        return false;
    if (index_is_rsp) /* the encoding hole: rsp cannot be an index */
        return false;
    return x64_imm_fits_simm32(disp);
}

/* Per-IR-value bookkeeping: the vreg, and light folding patterns
 * (imul-by-{2,4,8} and ptradd-by-const) the address folder consumes. */
typedef struct ValInfo {
    X64VReg vr;
    u8 pat; /* 0 none, 1 = base*scale, 2 = base+disp */
    X64VReg pat_base;
    u8 pat_scale;
    i64 pat_disp;
    u8 cc_plus1;   /* icmp: the cc, for branch fusion */
    u32 flags_ins; /* icmp: producing cmp's index in its block */
    u32 flags_blk;
} ValInfo;

/* An address shape derived from verified SSA before block-order selection.
 * Keeping IR operands here, rather than selected vregs, is the important
 * bit: a definition may dominate its uses while appearing later in the
 * function's layout.  to_vreg reserves the stable vreg when the memory use
 * is selected and the ordinary forward-reference repair materializes it
 * when its defining block is eventually visited. */
typedef struct AddrPlan {
    bool valid;
    bool suppress;
    bool has_index;
    u8 scale;
    i64 disp;
    IrOperand base;
    IrOperand index;
} AddrPlan;

typedef struct Isel {
    Arena *arena;
    const IrModule *m;
    const IrFunc *f;
    X64Func *xf;
    ValInfo *vals;       /* [nvals+1] */
    AddrPlan *addr_plan; /* [nvals+1], keyed by IR ValueId */
    u32 *use_count;
    u32 cur;     /* current MIR block index (0-based) */
    u32 cur_loc; /* source attribution inherited by every selected MIR op */
    /* flags tracking within the current block */
    u32 last_flags_inst; /* index+1 of last DEFS_FLAGS inst; 0 = none */
    u32 last_flags_val;  /* the icmp ValueId it computed for, or 0 */
    X64PicLevel pic;
} Isel;

static bool is_foldable_addr_use(const IrInst *in, u32 operand)
{
    switch (in->op) {
    case IR_LOAD:
        return operand == 0;
    case IR_STORE:
        return operand == 1;
    case IR_ATOMICRMW:
    case IR_CMPXCHG:
        return operand == 0;
    case IR_PTRADD:
        /* A pointer chain is supported only if its child is suppressible too;
         * plan_addresses propagates a failed child back to this parent. */
        return operand == 0;
    default:
        return false;
    }
}

static bool scaled_index_def(const IrInst *in, IrOperand *index, u8 *scale)
{
    i64 k;

    if (!in || in->nops != 2 || in->ops[0].kind != IROP_VALUE ||
        in->ops[1].kind != IROP_ICONST)
        return false;
    k = (i64)in->ops[1].a;
    if (in->op == IR_IMUL && (k == 2 || k == 4 || k == 8)) {
        *index = in->ops[0];
        *scale = (u8)k;
        return true;
    }
    if (in->op == IR_SHL && k >= 1 && k <= 3) {
        *index = in->ops[0];
        *scale = (u8)(1u << k);
        return true;
    }
    return false;
}

/* Plan ptradd folding from verified IR, before selection imposes a block
 * layout.  A producer disappears only when every use is an encodable memory
 * address.  CFG edge arguments and all other operand positions invalidate
 * the plan.  Bare symbols deliberately bail: RIP-relative, GOT and TLS
 * addresses each have relocation/materialization rules that cannot be
 * represented by a base/index SIB plan. */
static void plan_addresses(Isel *is, const IrInst *const *defs,
                           const bool *only_addr_use)
{
    const IrFunc *f = is->f;
    u32 *base_parent =
        arena_alloc(is->arena, (f->nvals + 1) * sizeof(u32), _Alignof(u32));
    u32 *queue =
        arena_alloc(is->arena, (f->nvals + 1) * sizeof(u32), _Alignof(u32));
    bool *plan_done =
        arena_alloc(is->arena, (f->nvals + 1) * sizeof(bool), _Alignof(bool));
    bool *only_planned_scale_use =
        arena_alloc(is->arena, (f->nvals + 1) * sizeof(bool), _Alignof(bool));
    u32 bi, i, qhead = 0, qtail = 0;
    bool progress;

    memset(base_parent, 0, (f->nvals + 1) * sizeof(u32));
    memset(plan_done, 0, (f->nvals + 1) * sizeof(bool));
    memset(only_planned_scale_use, 1, (f->nvals + 1) * sizeof(bool));

    /* Value ids follow block layout, not dominance order.  A label block can
     * therefore contain a field-address child with a smaller id than the
     * pointer calculation that dominates it.  Record every dependency first,
     * then resolve parents before children instead of relying on id order. */
    for (i = 1; i <= f->nvals; i++) {
        const IrInst *in = defs[i];

        if (!in || in->op != IR_PTRADD || in->nops != 2 ||
            in->ops[0].kind != IROP_VALUE) {
            plan_done[i] = true;
            continue;
        }
        base_parent[i] = (u32)in->ops[0].a;
    }
    do {
        progress = false;
        for (i = 1; i <= f->nvals; i++) {
            const IrInst *in = defs[i];
            AddrPlan *p;
            const AddrPlan *parent = NULL;
            u32 parent_value;
            bool valid = true;

            if (plan_done[i])
                continue;
            parent_value = base_parent[i];
            if (defs[parent_value] && defs[parent_value]->op == IR_PTRADD &&
                !plan_done[parent_value])
                continue;
            if (defs[parent_value] && defs[parent_value]->op == IR_PTRADD &&
                is->addr_plan[parent_value].valid)
                parent = &is->addr_plan[parent_value];
            p = &is->addr_plan[i];
            memset(p, 0, sizeof(*p));
            if (parent) {
                *p = *parent;
                p->valid = false;
                p->suppress = false;
            } else {
                p->base = in->ops[0];
                p->scale = 1;
            }
            if (in->ops[1].kind == IROP_ICONST) {
                i64 add = (i64)in->ops[1].a;

                if ((add > 0 && p->disp > INT64_MAX - add) ||
                    (add < 0 && p->disp < INT64_MIN - add))
                    valid = false;
                else {
                    p->disp += add;
                    valid = x64_fold_ok(1, false, p->disp);
                }
            } else if (in->ops[1].kind == IROP_VALUE) {
                const IrInst *off_def = defs[(u32)in->ops[1].a];

                if (p->has_index) {
                    valid = false; /* x86 has one SIB index */
                } else {
                    p->has_index = true;
                    if (!scaled_index_def(off_def, &p->index, &p->scale))
                        p->index = in->ops[1];
                    valid = x64_fold_ok(p->scale, false, 0);
                }
            } else {
                valid = false;
            }
            if (valid) {
                p->valid = true;
                p->suppress = is->use_count[i] && only_addr_use[i];
            }
            plan_done[i] = true;
            progress = true;
        }
    } while (progress);

    /* If a ptradd child cannot disappear, its base plan cannot disappear
     * either: the selected child still needs that SSA value as a register.
     * Each child has one base, so a small reverse work queue closes the
     * dependency without a quadratic fixed-point scan. */
    for (i = 1; i <= f->nvals; i++)
        if (defs[i] && defs[i]->op == IR_PTRADD && !is->addr_plan[i].suppress)
            queue[qtail++] = i;
    while (qhead < qtail) {
        u32 parent_value = base_parent[queue[qhead++]];

        if (parent_value && is->addr_plan[parent_value].suppress) {
            is->addr_plan[parent_value].suppress = false;
            queue[qtail++] = parent_value;
        }
    }

    /* A multiply/shift used only as the scaled operand of planned ptradds is
     * redundant too.  If it has one ordinary use, keep the producer: the
     * memory operands may still use the cheaper shape, but no SSA value is
     * left undefined. */
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 k, e, a;

            for (k = 0; k < in->nops; k++) {
                u32 v;
                AddrPlan *p;

                if (in->ops[k].kind != IROP_VALUE)
                    continue;
                v = (u32)in->ops[k].a;
                p = in->result.v ? &is->addr_plan[in->result.v] : NULL;
                if (!(in->op == IR_PTRADD && k == 1 && p && p->suppress &&
                      p->has_index && p->scale != 1))
                    only_planned_scale_use[v] = false;
            }
            for (e = 0; e < in->nedges; e++)
                for (a = 0; a < in->edges[e].nargs; a++)
                    if (in->edges[e].args[a].kind == IROP_VALUE)
                        only_planned_scale_use[(u32)in->edges[e].args[a].a] =
                            false;
        }
    }
    for (i = 1; i <= f->nvals; i++) {
        IrOperand index;
        u8 scale;

        if (is->use_count[i] && only_planned_scale_use[i] &&
            scaled_index_def(defs[i], &index, &scale))
            is->addr_plan[i].suppress = true;
    }
}

/* DATA: does reaching this object need the GOT?
 *
 * Only under full PIC, and only for external linkage. A global this module
 * defines can still be interposed by another shared object, so reading the
 * local copy when the program means the interposed one is a silent wrong
 * answer -- which is why `defined_here` does NOT excuse it.
 *
 * PIE does not need the GOT at all, and this was wrong in the first draft.
 * Everything in an executable resolves within one module: the linker binds a
 * direct pcrel reference, and data that really lives in a shared object gets
 * a COPY relocation. Measured against gcc, which emits plain `(%rip)` under
 * -fPIE even for an UNDEFINED extern. Routing it through the GOT would still
 * be correct, just slower -- but parity with gcc is the baseline.
 *
 * Visibility attributes (Sprint 55) would let `hidden` behave local under
 * full PIC; until they exist the conservative answer is the correct one. */
static bool sym_data_needs_got(const Isel *is, u32 sym_index)
{
    if (is->pic != X64_PIC_FULL)
        return false;
    return ir_sym_binding(is->m, sym_index).external;
}

/* CALLS: does this call go through the PLT?
 *
 * Under full PIC, every external call does -- including one to a function
 * this module DEFINES, because that definition is interposable too. gcc
 * emits `call def_fn@PLT` for exactly that reason, and only
 * -fno-semantic-interposition relaxes it.
 *
 * Under PIE, a defined function is reached directly and an undefined one
 * still needs the stub.
 *
 * We emit @PLT and let the linker build the stub; we never emit a
 * GOT-indirect call ourselves, which keeps lazy-versus-now binding the
 * linker's decision rather than ours. */
static bool sym_call_needs_plt(const Isel *is, u32 sym_index)
{
    IrSymBinding b;

    if (is->pic == X64_PIC_NONE)
        return false;
    b = ir_sym_binding(is->m, sym_index);
    if (!b.external)
        return false;
    return is->pic == X64_PIC_FULL || !b.defined_here;
}

static X64VReg newv(Isel *is)
{
    return x64_newv(is->xf, X64RC_GP);
}

static X64VReg newvf(Isel *is)
{
    return x64_newv(is->xf, X64RC_XMM);
}

static X64VReg newvv(Isel *is)
{
    return x64_newv_width(is->xf, X64RC_XMM, X64_X);
}

static X64Block *blk(Isel *is)
{
    return &is->xf->blocks[is->cur];
}

static void materialize_pending_cc(Isel *is);

static bool op_defs_flags(X64Op op)
{
    switch (op) {
    case X64_OP_ADD:
    case X64_OP_SUB:
    case X64_OP_AND:
    case X64_OP_OR:
    case X64_OP_XOR:
    case X64_OP_IMUL:
    case X64_OP_NEG:
    case X64_OP_NOT:
    case X64_OP_SHL:
    case X64_OP_SHR:
    case X64_OP_SAR:
    case X64_OP_CMP:
    case X64_OP_TEST:
    case X64_OP_CQO:
    case X64_OP_IDIV:
    case X64_OP_DIV:
    case X64_OP_UCOMI:
    case X64_OP_X87_FUCOMIP:
    case X64_OP_CALL:
    case X64_OP_ALLOCA_DYN:
        return true;
    default:
        return false;
    }
}

static X64Inst *emit(Isel *is, X64Op op, X64Width w)
{
    X64Block *b;
    X64Inst *in;

    /* A compare kept only in EFLAGS still represents a live SSA boolean.
     * Preserve it before any later MIR operation overwrites those flags; the
     * eventual condbr will test the materialized 0/1 value. */
    if (op_defs_flags(op))
        materialize_pending_cc(is);
    b = blk(is);
    if (b->n == b->cap) {
        u32 nc = b->cap ? b->cap * 2 : 16;
        X64Inst *ni =
            arena_alloc(is->arena, nc * sizeof(X64Inst), _Alignof(X64Inst));

        if (b->n)
            memcpy(ni, b->insts, b->n * sizeof(X64Inst));
        b->insts = ni;
        b->cap = nc;
    }
    in = &b->insts[b->n++];
    memset(in, 0, sizeof(*in));
    in->op = (u16)op;
    in->width = (u8)w;
    in->loc = is->cur_loc;
    switch (op) {
    case X64_OP_ADD:
    case X64_OP_SUB:
    case X64_OP_AND:
    case X64_OP_OR:
    case X64_OP_XOR:
    case X64_OP_IMUL:
    case X64_OP_NEG:
    case X64_OP_NOT:
    case X64_OP_SHL:
    case X64_OP_SHR:
    case X64_OP_SAR:
        in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
        is->last_flags_inst = b->n; /* index+1 */
        is->last_flags_val = 0;
        break;
    case X64_OP_FADD:
    case X64_OP_FSUB:
    case X64_OP_FMUL:
    case X64_OP_FDIV:
    case X64_OP_FXORM:
    case X64_OP_FANDM:
    case X64_OP_VADD:
    case X64_OP_VSUB:
    case X64_OP_VMUL:
    case X64_OP_VDIV:
    case X64_OP_VAND:
    case X64_OP_VOR:
    case X64_OP_VXOR:
    case X64_OP_VUNPCKLBW:
    case X64_OP_VUNPCKLWD:
    case X64_OP_VUNPCKLQ:
    case X64_OP_VSRLDQ:
        /* SSE arithmetic is two-address but does NOT touch EFLAGS. */
        in->flags = X64IF_TWO_ADDR;
        break;
    case X64_OP_CMP:
    case X64_OP_TEST:
    case X64_OP_CQO:
    case X64_OP_IDIV:
    case X64_OP_DIV:
    case X64_OP_UCOMI:
    case X64_OP_X87_FUCOMIP:
        in->flags = X64IF_DEFS_FLAGS;
        is->last_flags_inst = b->n;
        is->last_flags_val = 0;
        break;
    case X64_OP_CALL:
        /* Calls clobber EFLAGS along with everything else; carrying
         * DEFS_FLAGS makes the verifier reject any fusion across one. */
        in->flags = X64IF_DEFS_FLAGS;
        is->last_flags_inst = b->n;
        is->last_flags_val = 0;
        break;
    case X64_OP_ALLOCA_DYN:
        /* The Sprint 22 expansion (add/and/sub) clobbers flags, so the
         * marker must too — otherwise a cmp/jcc fusion could legally
         * span it pre-RA and break post-expansion. */
        in->flags = X64IF_DEFS_FLAGS;
        is->last_flags_inst = b->n;
        is->last_flags_val = 0;
        break;
    default:
        break; /* mov/lea/movzx/movsx/setcc/jmp/jcc leave flags alone */
    }
    return in;
}

/* A branch-only compare normally stays in EFLAGS until the block's condbr.
 * Preserve that pending boolean in a vreg before emitting anything that
 * overwrites the flags.  The condbr will then test the materialized value. */
static void materialize_pending_cc(Isel *is)
{
    u32 v = is->last_flags_val;
    ValInfo *vi;
    X64VReg s, z;
    X64Inst *x;

    if (!v)
        return;
    vi = &is->vals[v];
    if (vi->vr.v || !vi->cc_plus1)
        return;
    s = newv(is);
    z = newv(is);
    x = emit(is, X64_OP_SETCC, X64_B);
    x->def = s;
    x->cc = (u8)(vi->cc_plus1 - 1);
    x->flags = X64IF_USES_FLAGS;
    x->flags_src = vi->flags_ins;
    x = emit(is, X64_OP_MOVZX, X64_L);
    x->src_width = X64_B;
    x->def = z;
    x->a.kind = X64O_VREG;
    x->a.r = s;
    vi->vr = z;
}

static u32 new_block(Isel *is, const char *name)
{
    X64Func *xf = is->xf;

    if (xf->nblocks == xf->cap_blocks) {
        u32 nc = xf->cap_blocks ? xf->cap_blocks * 2 : 8;
        X64Block *nb =
            arena_alloc(is->arena, nc * sizeof(X64Block), _Alignof(X64Block));

        if (xf->nblocks)
            memcpy(nb, xf->blocks, xf->nblocks * sizeof(X64Block));
        xf->blocks = nb;
        xf->cap_blocks = nc;
    }
    memset(&xf->blocks[xf->nblocks], 0, sizeof(X64Block));
    xf->blocks[xf->nblocks].name = name;
    return ++xf->nblocks; /* 1-based */
}

/* An asm operand's width comes from its C type's BYTE SIZE, and X64Width's
 * enumerators are those sizes, so the mapping is identity with a clamp. A
 * 3-byte struct cannot reach here (the constraint decoder takes only scalars
 * and addresses), but the clamp keeps a future one from selecting garbage. */
static X64Width asm_width(u8 size)
{
    switch (size) {
    case 1:
        return X64_B;
    case 2:
        return X64_W;
    case 4:
        return X64_L;
    default:
        return X64_Q;
    }
}

static X64Width width_of(IrType t)
{
    switch (t) {
    case IRT_I8:
        return X64_B;
    case IRT_I16:
        return X64_W;
    case IRT_I32:
        return X64_L;
    case IRT_I64:
    case IRT_PTR:
        return X64_Q;
    default:
        CGF_ICE("x86_64 isel: no integer width for this IR type");
    }
}

static X64Operand ovreg(X64VReg r)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_VREG;
    o.r = r;
    return o;
}

static X64Operand oimm(i64 v)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_IMM;
    o.imm = v;
    return o;
}

/* Materialize any IR operand into a vreg. Constants pick the cheapest
 * legal form: movl zero-extends for free; movabs is the ONLY imm64. */
static X64VReg to_vreg(Isel *is, const IrOperand *o)
{
    switch (o->kind) {
    case IROP_VALUE:
        /* SSA permits a dominating definition to appear later in layout
         * order (mem2reg exposes this around loop backedges). Reserve its
         * vreg now; the definition walk bridges its selected result into
         * this stable identity. */
        if (!is->vals[(u32)o->a].vr.v)
            is->vals[(u32)o->a].vr = newv(is);
        return is->vals[(u32)o->a].vr;
    case IROP_ICONST: {
        X64Width w = width_of((IrType)o->type);
        /* Narrow IR integers are bit patterns. Constant folding may leave
         * their sign-extended mathematical value in the u64 payload (for
         * example an i32 bitfield-clear mask can arrive as -4294967289),
         * but every x86 32-bit encoding consumes only the low imm32 bits.
         * Canonicalize that payload before MIR so legal imm32 patterns do
         * not masquerade as forbidden imm64 operands. */
        i64 v = w == X64_Q ? (i64)o->a : (i64)(u32)o->a;
        X64VReg r = newv(is);
        X64Inst *in;

        if (w == X64_Q && !x64_imm_fits_simm32(v)) {
            if ((u64)v <= 0xFFFFFFFFull) {
                in = emit(is, X64_OP_MOV, X64_L); /* free zext */
            } else {
                in = emit(is, X64_OP_MOVABS, X64_Q);
            }
        } else {
            in = emit(is, X64_OP_MOV, w == X64_Q ? X64_Q : X64_L);
        }
        in->def = r;
        in->a = oimm(v);
        return r;
    }
    case IROP_SYMBOL: {
        /* 64-bit absolute addresses never fold: RIP-relative lea. */
        X64VReg r = newv(is);
        X64Inst *in;

        if (ir_sym_is_tls(is->m, o->sym + 1)) {
            /* Local-exec. A thread-local has no ordinary address, so the
             * address is BUILT: read the thread pointer out of %fs:0, then
             * add the symbol's link-time offset from it.
             *
             *     movq %fs:0, %r
             *     leaq sym@tpoff(%r), %r2
             *
             * Materializing the address rather than folding `%fs:sym@tpoff`
             * into every access keeps one code path for loads, stores and
             * address-of alike. An addend rides the lea's displacement,
             * where a relocation cannot carry it. */
            X64VReg base = newv(is);
            X64Inst *tp = emit(is, X64_OP_MOV, X64_Q);

            tp->def = base;
            tp->a.kind = X64O_MEM;
            tp->a.mem.seg_fs = true;
            tp->a.mem.scale = 1;

            in = emit(is, X64_OP_LEA, X64_Q);
            in->def = r;
            in->a.kind = X64O_MEM;
            in->a.mem.base = base;
            in->a.mem.scale = 1;
            in->a.mem.tpoff_sym = o->sym + 1;
            in->a.mem.disp = (i32)(i64)o->a;
            return r;
        }
        if (sym_data_needs_got(is, o->sym + 1)) {
            /* The GOT slot holds the address, so this is a LOAD, and the
             * addend cannot ride the relocation -- it becomes a separate
             * lea once the base is in hand. */
            in = emit(is, X64_OP_MOV, X64_Q);
            in->def = r;
            in->a.kind = X64O_MEM;
            in->a.mem.rip_sym = o->sym + 1;
            in->a.mem.rip_got = true;
            if ((i64)o->a) {
                X64VReg off = newv(is);
                X64Inst *add = emit(is, X64_OP_LEA, X64_Q);

                add->def = off;
                add->a.kind = X64O_MEM;
                add->a.mem.base = r;
                add->a.mem.scale = 1;
                add->a.mem.disp = (i32)(i64)o->a;
                return off;
            }
            return r;
        }
        in = emit(is, X64_OP_LEA, X64_Q);
        in->def = r;
        in->a.kind = X64O_MEM;
        in->a.mem.rip_sym = o->sym + 1;
        in->a.mem.disp = (i32)(i64)o->a;
        return r;
    }
    case IROP_UNDEF: {
        return newv(is); /* per-read freedom: any bits will do */
    }
    default:
        CGF_ICE("x86_64 isel: bad operand kind %u", o->kind);
    }
}

/* ALU source: an immediate rides inline when it fits the encoding. */
static X64Operand to_src(Isel *is, const IrOperand *o, X64Width w)
{
    if (o->kind == IROP_ICONST) {
        i64 v = w == X64_Q ? (i64)o->a : (i64)(u32)o->a;

        if (w != X64_Q || x64_imm_fits_simm32(v))
            return oimm(v);
    }
    return ovreg(to_vreg(is, o));
}

/* Address folding for load/store: [base + index*scale + disp] built from
 * the light pattern records; symbols go RIP-relative. */
static X64Mem fold_addr(Isel *is, const IrOperand *addr)
{
    X64Mem mem;

    memset(&mem, 0, sizeof(mem));
    mem.scale = 1;
    if (addr->kind == IROP_SYMBOL) {
        if (sym_data_needs_got(is, addr->sym + 1) ||
            ir_sym_is_tls(is->m, addr->sym + 1)) {
            /* Two indirections cannot fold into one operand: the GOT load
             * has to happen first, and to_vreg already knows how. A
             * thread-local is the same shape -- its address is BUILT from
             * the thread pointer, so folding the bare symbol here would
             * emit an ordinary absolute reference and quietly read the
             * wrong thread's copy. */
            mem.base = to_vreg(is, addr);
            mem.scale = 1;
            return mem;
        }
        mem.rip_sym = addr->sym + 1;
        mem.disp = (i32)(i64)addr->a;
        return mem;
    }
    if (addr->kind == IROP_VALUE) {
        const AddrPlan *ap = &is->addr_plan[(u32)addr->a];
        const ValInfo *vi = &is->vals[(u32)addr->a];

        if (ap->valid) {
            mem.base = to_vreg(is, &ap->base);
            mem.disp = (i32)ap->disp;
            if (ap->has_index) {
                mem.index = to_vreg(is, &ap->index);
                mem.scale = ap->scale;
            }
            return mem;
        }
        if (vi->pat == 2 && x64_fold_ok(1, false, vi->pat_disp)) {
            mem.base = vi->pat_base;
            mem.disp = (i32)vi->pat_disp;
            return mem;
        }
        /* A dominating address definition may appear later in block layout.
         * Reserve its stable identity exactly as every other value use does;
         * vreg 0 means the frame base and would silently miscompile here. */
        mem.base = to_vreg(is, addr);
        return mem;
    }
    mem.base = to_vreg(is, addr);
    return mem;
}

static X64Cc cc_of(IrIcmp p)
{
    switch (p) {
    case ICMP_EQ:
        return X64_CC_E;
    case ICMP_NE:
        return X64_CC_NE;
    case ICMP_SLT:
        return X64_CC_L;
    case ICMP_SLE:
        return X64_CC_LE;
    case ICMP_SGT:
        return X64_CC_G;
    case ICMP_SGE:
        return X64_CC_GE;
    case ICMP_ULT:
        return X64_CC_B;
    case ICMP_ULE:
        return X64_CC_BE;
    case ICMP_UGT:
        return X64_CC_A;
    default:
        return X64_CC_AE;
    }
}

static const char *const cc_names_[12] = {"e", "ne", "l", "le", "g", "ge",
                                          "b", "be", "a", "ae", "p", "np"};

const char *x64_cc_name(u8 cc)
{
    return cc < 12 ? cc_names_[cc] : "?";
}

/* --- Sprint 23: FP helpers -------------------------------------------------
 */

static X64Width fpw(u8 t)
{
    return t == IRT_F32 ? X64_L : X64_Q;
}

static bool irt_sse(u8 t)
{
    return t == IRT_F32 || t == IRT_F64;
}

static bool irt_vector(u8 t)
{
    return ir_type_is_vector((IrType)t);
}

/* A value that occupies a WHOLE xmm register and moves as 16 bytes.
 *
 * The 128-bit vectors, plus f128 -- `_Float128` on x86-64, where the psABI
 * classifies it SSE+SSEUP and so passes it in one xmm, exactly like a
 * vector. Sprint 36 already taught the register allocator, the spill path
 * and the edge movers to handle a 16-byte xmm value; this is the predicate
 * that lets f128 ride all of it instead of growing a parallel copy.
 *
 * DELIBERATELY NOT irt_vector itself. The vector ARITHMETIC paths select
 * packed SSE2 instructions, which would be silently wrong for f128 -- a
 * packed add is four f32 adds, not one binary128 add. f128 arithmetic never
 * reaches isel (src/lower/f128.c turns every operation into a libcall), and
 * keeping the predicates separate is what makes that a fact the code
 * states rather than one a reader has to reconstruct. */
static bool irt_xmm16(u8 t)
{
    return irt_vector(t) || t == IRT_F128;
}

/* Defined below, next to the other constant-pool helpers; to_vvreg needs
 * it for the f128 literal case. */
static u32 cpool_fconst(Isel *is, const IrOperand *o);

static X64VReg to_vvreg(Isel *is, const IrOperand *o)
{
    switch (o->kind) {
    case IROP_VALUE:
        if (!is->vals[(u32)o->a].vr.v)
            is->vals[(u32)o->a].vr = newvv(is);
        return is->vals[(u32)o->a].vr;
    case IROP_UNDEF:
        return newvv(is);
    case IROP_FCONST: {
        /* f128 ONLY. The vector types have no constant operand form -- a
         * vector constant is built with vsplat -- so this case did not
         * exist until _Float128 gave the 16-byte xmm class a literal.
         * There is no 128-bit FP immediate, so it loads from the pool. */
        X64VReg d;
        X64Inst *x;

        if (o->type != IRT_F128)
            CGF_ICE("x86_64 isel: vector constants require vsplat/load");
        d = newvv(is);
        x = emit(is, X64_OP_VLOAD, X64_X);
        x->def = d;
        x->a.kind = X64O_MEM;
        x->a.mem.cpool = cpool_fconst(is, o);
        return d;
    }
    default:
        CGF_ICE("x86_64 isel: vector constants require vsplat/load");
    }
}

static X64VReg vector_extract_low(Isel *is, X64VReg v, IrType elem)
{
    X64VReg d;
    X64Inst *x;

    if (elem == IRT_F32 || elem == IRT_F64) {
        d = newvf(is);
        x = emit(is, X64_OP_FMOV, fpw((u8)elem));
    } else {
        d = newv(is);
        x = emit(is, X64_OP_MOVQRX, elem == IRT_I64 ? X64_Q : X64_L);
    }
    x->def = d;
    x->a = ovreg(v);
    return d;
}

static X64VReg vector_shift_bytes(Isel *is, X64VReg v, u8 bytes)
{
    X64VReg d = newvv(is);
    X64Inst *x = emit(is, X64_OP_VSRLDQ, X64_X);

    x->def = d;
    x->a = ovreg(v);
    x->b = oimm(bytes);
    return d;
}

static u32 cpool_fconst(Isel *is, const IrOperand *o)
{
    if (o->type == IRT_F32)
        return x64_cpool_intern(is->xf, o->a & 0xffffffffull, 0, 4, 4);
    if (o->type == IRT_F64)
        return x64_cpool_intern(is->xf, o->a, 0, 8, 8);
    if (o->type == IRT_F128)
        return x64_cpool_intern(is->xf, o->a, o->b, 16, 16);
    /* f80: 10 data bytes in a 16-byte 16-aligned slot */
    return x64_cpool_intern(is->xf, o->a, o->b, 10, 16);
}

/* Materialize an f32/f64 operand into an xmm vreg. Constants load from
 * the rodata pool — there is no FP immediate form on x86. */
static X64VReg to_fvreg(Isel *is, const IrOperand *o)
{
    switch (o->kind) {
    case IROP_VALUE:
        if (!is->vals[(u32)o->a].vr.v)
            is->vals[(u32)o->a].vr = newvf(is);
        return is->vals[(u32)o->a].vr;
    case IROP_FCONST: {
        X64VReg r = newvf(is);
        X64Inst *x = emit(is, X64_OP_FLOAD, fpw(o->type));

        x->def = r;
        x->a.kind = X64O_MEM;
        x->a.mem.cpool = cpool_fconst(is, o);
        return r;
    }
    case IROP_UNDEF:
        return newvf(is); /* per-read freedom */
    default:
        CGF_ICE("x86_64 isel: bad FP operand kind %u", o->kind);
    }
}

/* f80 values live in MEMORY (the load-op-store law): an f80 IR value is
 * represented by a GP vreg holding the ADDRESS of its 16-byte slot
 * (10 data + 6 pad, 16-aligned like the psABI stack form). */
static X64VReg f80_slot(Isel *is)
{
    X64VReg d = newv(is);
    X64Inst *x = emit(is, X64_OP_LEA, X64_Q);

    x->def = d;
    x->a.kind = X64O_MEM; /* frame marker: base 0 */
    x->b.imm = 16;
    x->table = 16;
    return d;
}

static X64VReg f80_addr(Isel *is, const IrOperand *o)
{
    switch (o->kind) {
    case IROP_VALUE:
        if (!is->vals[(u32)o->a].vr.v)
            is->vals[(u32)o->a].vr = newv(is);
        return is->vals[(u32)o->a].vr;
    case IROP_FCONST: {
        X64VReg r = newv(is);
        X64Inst *x = emit(is, X64_OP_LEA, X64_Q);

        x->def = r;
        x->a.kind = X64O_MEM;
        x->a.mem.cpool = cpool_fconst(is, o);
        return r;
    }
    case IROP_UNDEF:
        return f80_slot(is);
    default:
        CGF_ICE("x86_64 isel: bad f80 operand kind %u", o->kind);
    }
}

/* One x87 memory op: fld/fstp/fild/fistp/fnstcw/fldcw at [addr+disp]. */
static void x87_mem(Isel *is, X64Op op, X64Width w, X64VReg addr, i32 disp)
{
    X64Inst *x = emit(is, op, w);

    x->a.kind = X64O_MEM;
    x->a.mem.base = addr;
    x->a.mem.scale = 1;
    x->a.mem.disp = disp;
}

static void x87_op0(Isel *is, X64Op op)
{
    (void)emit(is, op, X64_T);
}

/* The FP compare recipe table (THE anti-NaN table). ucomi sets ZF/PF/CF
 * like an unsigned compare with unordered => ZF=PF=CF=1; PF is the
 * unordered flag. The swaps turn < into > so the unordered case falls
 * on the correct side without touching PF; the cc pairs handle ==/!=
 * where ZF alone lies on NaN. comb: 0 = cc1 alone, 1 = AND, 2 = OR. */
typedef struct FcmpRecipe {
    u8 swap;
    u8 cc1;
    u8 cc2;
    u8 comb;
} FcmpRecipe;

static const FcmpRecipe fcmp_recipes[14] = {
    [FCMP_OEQ] = {0, X64_CC_E, X64_CC_NP, 1},
    [FCMP_ONE] = {0, X64_CC_NE, X64_CC_NP, 1},
    [FCMP_OLT] = {1, X64_CC_A, 0, 0},
    [FCMP_OLE] = {1, X64_CC_AE, 0, 0},
    [FCMP_OGT] = {0, X64_CC_A, 0, 0},
    [FCMP_OGE] = {0, X64_CC_AE, 0, 0},
    [FCMP_ORD] = {0, X64_CC_NP, 0, 0},
    [FCMP_UEQ] = {0, X64_CC_E, 0, 0}, /* unordered => ZF=1 => true */
    [FCMP_UNE] = {0, X64_CC_NE, X64_CC_P, 2},
    [FCMP_ULT] = {0, X64_CC_B, 0, 0}, /* CF set by unordered: correct */
    [FCMP_ULE] = {0, X64_CC_BE, 0, 0},
    [FCMP_UGT] = {1, X64_CC_B, 0, 0},
    [FCMP_UGE] = {1, X64_CC_BE, 0, 0},
    [FCMP_UNO] = {0, X64_CC_P, 0, 0},
};

/* Materialize the 0/1 value of an fcmp whose flags are current (the
 * producer sits at flags_ins). Single-cc recipes leave the flags intact
 * (setcc does not write them), so condbr fusion off the SAME compare
 * still works; pair recipes end in and/or, which clobbers — those
 * branch through the materialized value instead. */
static X64VReg fcmp_value(Isel *is, const FcmpRecipe *rc, u32 flags_ins)
{
    X64VReg s1 = newv(is);
    X64VReg z = newv(is);
    X64Inst *x;

    x = emit(is, X64_OP_SETCC, X64_B);
    x->def = s1;
    x->cc = rc->cc1;
    x->flags = X64IF_USES_FLAGS;
    x->flags_src = flags_ins;
    if (rc->comb) {
        X64VReg s2 = newv(is);
        X64VReg s3 = newv(is);

        x = emit(is, X64_OP_SETCC, X64_B);
        x->def = s2;
        x->cc = rc->cc2;
        x->flags = X64IF_USES_FLAGS;
        x->flags_src = flags_ins;
        x = emit(is, rc->comb == 1 ? X64_OP_AND : X64_OP_OR, X64_B);
        x->def = s3;
        x->a = ovreg(s1);
        x->b = ovreg(s2);
        s1 = s3;
    }
    x = emit(is, X64_OP_MOVZX, X64_L);
    x->src_width = X64_B;
    x->def = z;
    x->a = ovreg(s1);
    return z;
}

/* Truncate an 8-byte repeated-byte pattern to a narrower store width. */
static u64 sim_pattern_narrow(u64 pat, u32 step)
{
    return step >= 8 ? pat : pat & ((1ull << (step * 8)) - 1);
}

/* st0 is loaded; truncate-store it as i64 into [tmp+0] with the RC
 * dance (x87 default rounds to nearest, C wants truncation): save the
 * control word at [tmp+8], set RC=11 via [tmp+10], fistpq, restore.
 * Pops st0 — the sequence stays locally balanced. */
static void x87_trunc_store(Isel *is, X64VReg tmp)
{
    X64VReg t = newv(is);
    X64VReg t2 = newv(is);
    X64Inst *x;

    x87_mem(is, X64_OP_X87_FNSTCW, X64_W, tmp, 8);
    x = emit(is, X64_OP_MOVZX, X64_L);
    x->src_width = X64_W;
    x->def = t;
    x->a.kind = X64O_MEM;
    x->a.mem.base = tmp;
    x->a.mem.scale = 1;
    x->a.mem.disp = 8;
    x = emit(is, X64_OP_OR, X64_L);
    x->def = t2;
    x->a = ovreg(t);
    x->b = oimm(0xC00);
    x = emit(is, X64_OP_STORE, X64_W);
    x->a = ovreg(t2);
    x->b.kind = X64O_MEM;
    x->b.mem.base = tmp;
    x->b.mem.scale = 1;
    x->b.mem.disp = 10;
    x87_mem(is, X64_OP_X87_FLDCW, X64_W, tmp, 10);
    x87_mem(is, X64_OP_X87_FISTP, X64_Q, tmp, 0);
    x87_mem(is, X64_OP_X87_FLDCW, X64_W, tmp, 8);
}

/* --- block-parameter moves (parallel-copy semantics) ---------------------- */

typedef struct PMove {
    X64VReg dst;
    X64Operand src;
    bool fp; /* xmm class: copies are FMOV, the cycle scratch is xmm */
    bool done;
} PMove;

/* Sequentialize a parallel copy. Emit any move whose dst is not a
 * PENDING source; when stuck, a cycle exists (the classic a<->b swap of
 * loop-carried block params) — break it with one scratch vreg. Naive
 * sequential emission here silently corrupts loop-carried values. */
static void emit_parallel_copy(Isel *is, PMove *mv, u32 n, X64Width *widths)
{
    u32 remaining = n;

    while (remaining) {
        u32 i, j;
        bool progressed = false;

        for (i = 0; i < n; i++) {
            bool is_src = false;

            if (mv[i].done)
                continue;
            for (j = 0; j < n; j++)
                if (!mv[j].done && j != i && mv[j].src.kind == X64O_VREG &&
                    mv[j].src.r.v == mv[i].dst.v)
                    is_src = true;
            if (is_src)
                continue;
            {
                X64Inst *in = emit(
                    is,
                    mv[i].fp ? (widths[i] == X64_X ? X64_OP_VMOV : X64_OP_FMOV)
                             : X64_OP_MOV,
                    widths[i]);

                in->def = mv[i].dst;
                in->a = mv[i].src;
            }
            mv[i].done = true;
            remaining--;
            progressed = true;
        }
        if (!progressed) {
            /* cycle: rotate through a scratch */
            for (i = 0; i < n && mv[i].done; i++)
                ;
            {
                X64VReg scratch =
                    mv[i].fp ? (widths[i] == X64_X ? newvv(is) : newvf(is))
                             : newv(is);
                X64Inst *in = emit(
                    is,
                    mv[i].fp ? (widths[i] == X64_X ? X64_OP_VMOV : X64_OP_FMOV)
                             : X64_OP_MOV,
                    widths[i]);

                in->def = scratch;
                in->a = ovreg(mv[i].dst);
                for (j = 0; j < n; j++)
                    if (!mv[j].done && mv[j].src.kind == X64O_VREG &&
                        mv[j].src.r.v == mv[i].dst.v)
                        mv[j].src = ovreg(scratch);
            }
        }
    }
}

/* Emit the arg->param moves for one IR edge into the CURRENT block. */
static void edge_moves(Isel *is, const IrEdge *e)
{
    const IrBlock *tb = ir_block((IrFunc *)is->f, e->target);
    PMove mv[32];
    X64Width widths[32];
    u32 i, n = 0;

    if (!tb || !e->nargs)
        return;
    for (i = 0; i < e->nargs && i < tb->nparams && n < 32; i++) {
        u8 at = e->args[i].type;

        if (at == IRT_F80 || at == IRT_F128)
            CGF_ICE("x86_64 isel: f80/f128 block parameters violate "
                    "the memory law");
        if (irt_vector(at)) {
            mv[n].src = ovreg(to_vvreg(is, &e->args[i]));
            mv[n].fp = true;
            widths[n] = X64_X;
        } else if (irt_sse(at)) {
            mv[n].src = ovreg(to_fvreg(is, &e->args[i]));
            mv[n].fp = true;
            widths[n] = fpw(at);
        } else {
            mv[n].src =
                to_src(is, &e->args[i], width_of((IrType)e->args[i].type));
            mv[n].fp = false;
            widths[n] = width_of((IrType)e->args[i].type);
        }
        mv[n].dst = is->vals[tb->params[i].v].vr;
        mv[n].done = false;
        n++;
    }
    emit_parallel_copy(is, mv, n, widths);
}

/* A condbr edge carrying args gets a SPLIT block (trampoline): moves +
 * jmp. Critical-edge safety by construction. */
static u32 edge_target(Isel *is, const IrEdge *e)
{
    u32 save;
    u32 t;

    if (!e->nargs)
        return e->target.v;
    save = is->cur;
    t = new_block(is, "split");
    is->cur = t - 1;
    edge_moves(is, e);
    {
        X64Inst *in = emit(is, X64_OP_JMP, X64_Q);

        in->target = e->target.v;
    }
    is->cur = save;
    return t;
}

/* --- the per-instruction walk --------------------------------------------- */

static X64Op alu_op(IrOp op)
{
    switch (op) {
    case IR_IADD:
        return X64_OP_ADD;
    case IR_ISUB:
        return X64_OP_SUB;
    case IR_AND:
        return X64_OP_AND;
    case IR_OR:
        return X64_OP_OR;
    case IR_XOR:
        return X64_OP_XOR;
    case IR_IMUL:
        return X64_OP_IMUL;
    case IR_SHL:
        return X64_OP_SHL;
    case IR_LSHR:
        return X64_OP_SHR;
    default:
        return X64_OP_SAR; /* IR_ASHR */
    }
}

static void sel_inst(Isel *is, const IrInst *in, const IrBlock *irb)
{
    if (in->result.v && is->addr_plan[in->result.v].suppress)
        return;

    switch (in->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_IMUL: {
        if (irt_vector(in->type)) {
            X64VReg d = newvv(is);
            X64VReg a = to_vvreg(is, &in->ops[0]);
            X64VReg b = to_vvreg(is, &in->ops[1]);
            X64Op op = in->op == IR_IADD   ? X64_OP_VADD
                       : in->op == IR_ISUB ? X64_OP_VSUB
                       : in->op == IR_IMUL ? X64_OP_VMUL
                       : in->op == IR_AND  ? X64_OP_VAND
                       : in->op == IR_OR   ? X64_OP_VOR
                                           : X64_OP_VXOR;
            X64Inst *x = emit(is, op, X64_X);

            x->src_width = in->type;
            x->def = d;
            x->a = ovreg(a);
            x->b = ovreg(b);
            is->vals[in->result.v].vr = d;
            break;
        }
        X64Width w = width_of((IrType)in->type);
        X64VReg d = newv(is);
        X64Inst *x;
        X64Operand b = to_src(is, &in->ops[1], w);
        X64VReg a = to_vreg(is, &in->ops[0]);

        x = emit(is, alu_op((IrOp)in->op), w);
        x->def = d;
        x->a = ovreg(a);
        x->b = b;
        is->vals[in->result.v].vr = d;
        /* light folding record: v = base * {2,4,8} or base + disp */
        if (in->op == IR_IMUL && in->ops[1].kind == IROP_ICONST) {
            i64 k = (i64)in->ops[1].a;

            if (k == 2 || k == 4 || k == 8) {
                is->vals[in->result.v].pat = 1;
                is->vals[in->result.v].pat_base = a;
                is->vals[in->result.v].pat_scale = (u8)k;
            }
        }
        break;
    }
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR: {
        X64Width w = width_of((IrType)in->type);
        X64VReg d = newv(is);
        X64VReg a0 = to_vreg(is, &in->ops[0]);
        X64Operand count;
        X64Inst *x;

        if (in->ops[1].kind == IROP_ICONST) {
            count = oimm((i64)in->ops[1].a);
        } else {
            /* variable count: CL fixed-reg constraint (hardware masks
             * &63/&31; over-width shifts are C UB — matching gcc).  Give
             * the constraint its own vreg: the source may itself be a div
             * result precolored to rax/rdx. */
            X64VReg src = to_vreg(is, &in->ops[1]);
            X64VReg cl = newv(is);

            x = emit(is, X64_OP_MOV, X64_Q);
            x->def = cl;
            x->def_fixed = X64_RCX + 1;
            x->a = ovreg(src);
            count = ovreg(cl);
            count.fixed = X64_RCX + 1;
        }
        x = emit(is, alu_op((IrOp)in->op), w);
        x->def = d;
        x->a = ovreg(a0);
        x->b = count;
        is->vals[in->result.v].vr = d;
        /* Sprint 31 canonicalizes multiply-by-{2,4,8} to a shift.  Keep
         * the address-folding promise by recording the equivalent scaled
         * index shape here as well as in the IMUL case above. */
        if (in->op == IR_SHL && in->ops[1].kind == IROP_ICONST &&
            in->ops[1].a >= 1 && in->ops[1].a <= 3) {
            is->vals[in->result.v].pat = 1;
            is->vals[in->result.v].pat_base = a0;
            is->vals[in->result.v].pat_scale = (u8)(1u << in->ops[1].a);
        }
        break;
    }
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM: {
        /* rax/rdx fixed-reg dance; INT_MIN/-1 and /0 FAULT at runtime:
         * that is C UB and deliberate — gcc faults too, and diverging
         * breaks differential testing. */
        X64Width w = width_of((IrType)in->type);
        bool sign = in->op == IR_SDIV || in->op == IR_SREM;
        bool quot = in->op == IR_SDIV || in->op == IR_UDIV;
        X64VReg lo = newv(is);
        X64VReg hi = newv(is);
        X64VReg d = newv(is);
        X64Inst *x;

        {
            X64VReg dv = to_vreg(is, &in->ops[0]);

            x = emit(is, X64_OP_MOV, w);
            x->def = lo;
            x->def_fixed = X64_RAX + 1;
            x->a = ovreg(dv);
        }
        if (sign) {
            x = emit(is, X64_OP_CQO, w);
            x->def = hi;
            x->def_fixed = X64_RDX + 1;
            x->a = ovreg(lo);
            x->a.fixed = X64_RAX + 1;
        } else {
            x = emit(is, X64_OP_MOV, X64_L);
            x->def = hi;
            x->def_fixed = X64_RDX + 1;
            x->a = oimm(0);
        }
        {
            X64VReg dvs = to_vreg(is, &in->ops[1]);

            x = emit(is, sign ? X64_OP_IDIV : X64_OP_DIV, w);
            x->def = d;
            x->def_fixed = (u8)((quot ? X64_RAX : X64_RDX) + 1);
            x->a = ovreg(lo);
            x->a.fixed = X64_RAX + 1;
            x->b = ovreg(dvs);
            /* idiv reads rdx:rax; rdx has no operand slot, so without
             * this the hi interval dies at the cqo and the allocator
             * would hand rdx to someone else across the divide. */
            x64_add_xuse(is->xf, x, hi, X64_RDX + 1);
        }
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_ICMP: {
        X64Width w = width_of((IrType)in->ops[0].type);
        bool zero_rhs = in->ops[1].kind == IROP_ICONST && in->ops[1].a == 0;
        X64VReg a = to_vreg(is, &in->ops[0]);
        X64Inst *x;

        if (zero_rhs) {
            x = emit(is, X64_OP_TEST, w); /* test r,r: shorter, same flags */
            x->a = ovreg(a);
            x->b = ovreg(a);
        } else {
            /* operands BEFORE the consumer emits (the Sprint 21 law,
             * third strike): an imm64 rhs materializes via movabs, and
             * emitted after the cmp it lands BETWEEN cmp and its
             * consumer reading a garbage register. Found by the first
             * e2e imm64 comparison. */
            X64Operand rhs = to_src(is, &in->ops[1], w);

            x = emit(is, X64_OP_CMP, w);
            x->a = ovreg(a);
            x->b = rhs;
        }
        is->vals[in->result.v].cc_plus1 = (u8)(cc_of((IrIcmp)in->subop) + 1);
        is->vals[in->result.v].flags_ins = blk(is)->n - 1;
        is->vals[in->result.v].flags_blk = is->cur;
        is->last_flags_val = in->result.v;
        /* Branch-only compares never materialize 0/1; a value that is
         * ALSO used gets setcc+movzx off the SAME flags. */
        if (is->use_count[in->result.v] > 1 ||
            !(irb->last && irb->last->op == IR_CONDBR &&
              irb->last->ops[0].kind == IROP_VALUE &&
              (u32)irb->last->ops[0].a == in->result.v)) {
            X64VReg s = newv(is);
            X64VReg z = newv(is);

            x = emit(is, X64_OP_SETCC, X64_B);
            x->def = s;
            x->cc = (u8)cc_of((IrIcmp)in->subop);
            x->flags = X64IF_USES_FLAGS;
            x->flags_src = is->vals[in->result.v].flags_ins;
            x = emit(is, X64_OP_MOVZX, X64_L);
            x->src_width = X64_B;
            x->def = z;
            x->a = ovreg(s);
            is->vals[in->result.v].vr = z;
        }
        break;
    }
    case IR_SEXT:
    case IR_ZEXT: {
        X64Width dw = width_of((IrType)in->type);
        X64Width sw = width_of((IrType)in->ops[0].type);
        X64VReg d = newv(is);
        X64VReg src = to_vreg(is, &in->ops[0]);
        X64Inst *x;

        if (in->op == IR_ZEXT && sw == X64_L && dw == X64_Q) {
            /* movl %r,%r: 32-bit writes zero 63:32 — the free zext. The
             * self-mov is REQUIRED: after a trunc rename the upper bits
             * are stale until a 32-bit op writes the register. */
            x = emit(is, X64_OP_MOV, X64_L);
        } else {
            x = emit(is, in->op == IR_ZEXT ? X64_OP_MOVZX : X64_OP_MOVSX, dw);
            x->src_width = (u8)sw;
        }
        x->def = d;
        x->a = ovreg(src);
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_TRUNC:
    case IR_BITCAST: {
        /* Renames: a trunc is a width relabel (callers use the narrow
         * width; upper bits stale by contract); ptr<->i64 bitcast is
         * free. FP<->int bitcasts are real movq/movd (Sprint 23). */
        bool dst_sse = irt_sse(in->type);
        bool src_sse = irt_sse(in->ops[0].type);

        if (in->op == IR_BITCAST && dst_sse && !src_sse) {
            X64VReg d = newvf(is);
            X64VReg s = to_vreg(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_MOVQXR, fpw(in->type));

            x->def = d;
            x->a = ovreg(s);
            is->vals[in->result.v].vr = d;
            break;
        }
        if (in->op == IR_BITCAST && !dst_sse && src_sse) {
            X64VReg d = newv(is);
            X64VReg s = to_fvreg(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_MOVQRX, fpw(in->ops[0].type));

            x->def = d;
            x->a = ovreg(s);
            is->vals[in->result.v].vr = d;
            break;
        }
        if (in->op == IR_BITCAST && dst_sse && src_sse) {
            is->vals[in->result.v].vr = to_fvreg(is, &in->ops[0]);
            break;
        }
        is->vals[in->result.v].vr = to_vreg(is, &in->ops[0]);
        break;
    }
    case IR_ALLOCA: {
        if (in->ops[0].kind == IROP_ICONST) {
            /* Static: an opaque LEA-from-frame marker (base 0, no
             * symbol). Size rides in b.imm and align in table — b.kind
             * stays NONE so nothing downstream prints or verifies it —
             * and x64_frame_finalize turns the marker into rbp-disp. */
            X64VReg d;
            X64Inst *x;

            if (in->align > 16)
                materialize_pending_cc(is);
            d = newv(is);
            x = emit(is, X64_OP_LEA, X64_Q);

            x->def = d;
            x->a.kind = X64O_MEM;
            x->a.mem.base.v = 0;
            x->a.mem.disp = 0;
            x->b.imm = (i64)in->ops[0].a;
            x->table = in->align;
            if (in->align > 16) {
                /* Frame finalization expands this marker to lea+and.  The
                 * mask writes EFLAGS, so make that clobber visible now and
                 * prevent cmp/jcc fusion from spanning the allocation. */
                x->flags = X64IF_DEFS_FLAGS;
                is->last_flags_inst = blk(is)->n;
                is->last_flags_val = 0;
            }
            is->vals[in->result.v].vr = d;
        } else {
            /* Dynamic (VLA): a marker regalloc expands post-RA into the
             * aligned rsp bump and, for alignment >16, a separately masked
             * object pointer. rsp itself remains 16-aligned for calls. */
            X64VReg s;
            X64VReg d;
            X64Inst *x;

            materialize_pending_cc(is);
            s = to_vreg(is, &in->ops[0]);
            d = newv(is);
            x = emit(is, X64_OP_ALLOCA_DYN, X64_Q);
            x->def = d;
            x->a = ovreg(s);
            x->table = in->align;
            is->vals[in->result.v].vr = d;
        }
        break;
    }
    case IR_LOAD: {
        if (irt_xmm16(in->type)) {
            X64VReg d = newvv(is);
            X64Mem mem = fold_addr(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_VLOAD, X64_X);

            x->def = d;
            x->a.kind = X64O_MEM;
            x->a.mem = mem;
            is->vals[in->result.v].vr = d;
            break;
        }
        if (irt_sse(in->type)) {
            X64VReg d = newvf(is);
            X64Mem mem = fold_addr(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_FLOAD, fpw(in->type));

            x->def = d;
            x->a.kind = X64O_MEM;
            x->a.mem = mem;
            is->vals[in->result.v].vr = d;
            break;
        }
        if (in->type == IRT_F80) {
            /* A load PRODUCES A VALUE: copy the 10 bytes into a fresh
             * slot so later stores through the pointer cannot alias it
             * (fldt/fstpt, locally balanced). */
            X64Mem mem = fold_addr(is, &in->ops[0]);
            X64VReg slot = f80_slot(is);
            X64Inst *x = emit(is, X64_OP_X87_FLD, X64_T);

            x->a.kind = X64O_MEM;
            x->a.mem = mem;
            x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
            is->vals[in->result.v].vr = slot;
            break;
        }
        {
            X64Width w = width_of((IrType)in->type);
            X64VReg d = newv(is);
            X64Mem mem = fold_addr(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_LOAD, w);

            x->def = d;
            x->a.kind = X64O_MEM;
            x->a.mem = mem;
            is->vals[in->result.v].vr = d;
        }
        break;
    }
    case IR_STORE: {
        if (irt_xmm16(in->ops[0].type)) {
            X64VReg v = to_vvreg(is, &in->ops[0]);
            X64Mem mem = fold_addr(is, &in->ops[1]);
            X64Inst *x = emit(is, X64_OP_VSTORE, X64_X);

            x->a = ovreg(v);
            x->b.kind = X64O_MEM;
            x->b.mem = mem;
            break;
        }
        if (irt_sse(in->ops[0].type)) {
            X64VReg v = to_fvreg(is, &in->ops[0]);
            X64Mem mem = fold_addr(is, &in->ops[1]);
            X64Inst *x = emit(is, X64_OP_FSTORE, fpw(in->ops[0].type));

            x->a = ovreg(v);
            x->b.kind = X64O_MEM;
            x->b.mem = mem;
            break;
        }
        if (in->ops[0].type == IRT_F80) {
            X64VReg src = f80_addr(is, &in->ops[0]);
            X64Mem mem = fold_addr(is, &in->ops[1]);
            X64Inst *x;

            x87_mem(is, X64_OP_X87_FLD, X64_T, src, 0);
            x = emit(is, X64_OP_X87_FSTP, X64_T);
            x->a.kind = X64O_MEM;
            x->a.mem = mem;
            break;
        }
        {
            X64Width w = width_of((IrType)in->ops[0].type);
            X64Operand v = to_src(is, &in->ops[0], w);
            X64Mem mem = fold_addr(is, &in->ops[1]);
            X64Inst *x = emit(is, X64_OP_STORE, w);

            x->a = v;
            x->b.kind = X64O_MEM;
            x->b.mem = mem;
            if (in->flags & IRF_SEQ_CST) {
                /* x86-TSO gives every load acquire semantics and every store
                 * release semantics for free, so an atomic LOAD needs nothing
                 * beyond a plain mov. A sequentially consistent STORE does
                 * not come free: the store buffer lets a later load overtake
                 * it, which is exactly the reordering Dekker-style algorithms
                 * depend on not happening. Without this fence the code is
                 * silently wrong on real hardware and passes every
                 * single-threaded test. */
                (void)emit(is, X64_OP_MFENCE, X64_Q);
            }
        }
        break;
    }
    case IR_PTRADD: {
        X64VReg d = newv(is);
        X64VReg base = to_vreg(is, &in->ops[0]);
        X64Inst *x = emit(is, X64_OP_LEA, X64_Q);

        x->def = d;
        x->a.kind = X64O_MEM;
        x->a.mem.scale = 1;
        x->a.mem.base = base;
        if (in->ops[1].kind == IROP_ICONST &&
            x64_imm_fits_simm32((i64)in->ops[1].a)) {
            x->a.mem.disp = (i32)(i64)in->ops[1].a;
            is->vals[in->result.v].pat = 2;
            is->vals[in->result.v].pat_base = base;
            is->vals[in->result.v].pat_disp = (i64)in->ops[1].a;
        } else if (in->ops[1].kind == IROP_VALUE &&
                   is->vals[(u32)in->ops[1].a].pat == 1 &&
                   x64_fold_ok(is->vals[(u32)in->ops[1].a].pat_scale, false,
                               0)) {
            /* a + b*{2,4,8}: the one free lunch — lea, no flags. */
            x->a.mem.index = is->vals[(u32)in->ops[1].a].pat_base;
            x->a.mem.scale = is->vals[(u32)in->ops[1].a].pat_scale;
        } else if (in->ops[1].kind == IROP_VALUE) {
            x->a.mem.index = to_vreg(is, &in->ops[1]);
        } else {
            /* constant too wide to fold: pre-materialized path */
            X64VReg off;
            X64Inst *save = x;

            blk(is)->n--; /* rewind the lea; order operands first */
            off = to_vreg(is, &in->ops[1]);
            x = emit(is, X64_OP_LEA, X64_Q);
            *x = *save;
            x->a.mem.index = off;
        }
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_SELECT: {
        /* v0: the branch diamond. cmovcc is upgrade-safe (operands are
         * computed IR values, no speculation hazard) but afs-as has no
         * cmovcc arm yet — Sprint 24 files the upstream extension.
         * FP selects ride the same diamond with fmov (Sprint 23); f80
         * copies its 10 bytes per arm, each arm locally balanced. */
        bool sse = irt_sse(in->type);
        bool f80 = in->type == IRT_F80;
        X64VReg d;
        u32 tb = new_block(is, "sel.t");
        u32 jb = new_block(is, "sel.j");
        X64VReg c;
        X64Inst *x;

        if (f80)
            d = f80_slot(is);
        else
            d = sse ? newvf(is) : newv(is);
        c = to_vreg(is, &in->ops[0]);
        if (f80) {
            X64VReg ea = f80_addr(is, &in->ops[2]);

            x87_mem(is, X64_OP_X87_FLD, X64_T, ea, 0);
            x87_mem(is, X64_OP_X87_FSTP, X64_T, d, 0);
        } else if (sse) {
            X64VReg ev = to_fvreg(is, &in->ops[2]);

            x = emit(is, X64_OP_FMOV, fpw(in->type));
            x->def = d;
            x->a = ovreg(ev);
        } else {
            X64Width w = width_of((IrType)in->type);
            X64Operand ev = to_src(is, &in->ops[2], w);

            x = emit(is, X64_OP_MOV, w); /* else value first */
            x->def = d;
            x->a = ev;
        }
        x = emit(is, X64_OP_TEST, X64_L);
        x->a = ovreg(c);
        x->b = ovreg(c);
        x = emit(is, X64_OP_JCC, X64_Q);
        x->cc = X64_CC_NE;
        x->flags = X64IF_USES_FLAGS;
        x->flags_src = blk(is)->n - 2;
        x->target = tb;
        x->target2 = jb;
        is->cur = tb - 1;
        if (f80) {
            X64VReg ta = f80_addr(is, &in->ops[1]);

            x87_mem(is, X64_OP_X87_FLD, X64_T, ta, 0);
            x87_mem(is, X64_OP_X87_FSTP, X64_T, d, 0);
        } else if (sse) {
            X64VReg tv = to_fvreg(is, &in->ops[1]);

            x = emit(is, X64_OP_FMOV, fpw(in->type));
            x->def = d;
            x->a = ovreg(tv);
        } else {
            X64Width w = width_of((IrType)in->type);
            X64Operand tv = to_src(is, &in->ops[1], w);

            x = emit(is, X64_OP_MOV, w);
            x->def = d;
            x->a = tv;
        }
        x = emit(is, X64_OP_JMP, X64_Q);
        x->target = jb;
        is->cur = jb - 1;
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_RET: {
        const IrFunc *irf = is->f;
        X64VReg retregs[2];
        u8 retfix[2];
        u32 nret = 0, k;
        X64Inst *x;

        if (irf->abi_ret == IR_ABIRET_SRET) {
            /* psABI: the sret pointer comes BACK in rax too. */
            X64VReg r = newv(is);

            x = emit(is, X64_OP_MOV, X64_Q);
            x->def = r;
            x->def_fixed = X64_RAX + 1;
            x->a = ovreg(is->vals[irf->param_vals[0].v].vr);
            retregs[nret] = r;
            retfix[nret++] = X64_RAX + 1;
        } else if (irf->abi_ret != IR_ABIRET_NONE) {
            /* PAIR: the VALUE travels in registers; load the eightbytes
             * back from the sret-shaped pointer per class order. */
            X64VReg p = is->vals[irf->param_vals[0].v].vr;
            bool sse0 = irf->abi_ret == IR_ABIRET_PAIR_SI ||
                        irf->abi_ret == IR_ABIRET_PAIR_SS;
            bool sse1 = irf->abi_ret == IR_ABIRET_PAIR_IS ||
                        irf->abi_ret == IR_ABIRET_PAIR_SS;
            u8 gp_next = X64_RAX;

            for (k = 0; k < 2; k++) {
                bool sse = k ? sse1 : sse0;
                X64VReg r = sse ? newvf(is) : newv(is);
                u8 fix;

                if (sse) {
                    fix = (u8)(X64_XMM0 + (k && sse0 ? 1 : 0) + 1);
                    x = emit(is, X64_OP_FLOAD, X64_Q);
                } else {
                    fix = (u8)(gp_next + 1);
                    gp_next = X64_RDX;
                    x = emit(is, X64_OP_LOAD, X64_Q);
                }
                x->def = r;
                x->def_fixed = fix;
                x->a.kind = X64O_MEM;
                x->a.mem.base = p;
                x->a.mem.scale = 1;
                x->a.mem.disp = (i32)(8 * k);
                retregs[nret] = r;
                retfix[nret++] = fix;
            }
        } else if (in->nops) {
            u8 rt = in->ops[0].type;

            if (rt == IRT_F80) {
                /* st0 is the one legitimate x87 value at ret. */
                X64VReg a = f80_addr(is, &in->ops[0]);

                x87_mem(is, X64_OP_X87_FLD, X64_T, a, 0);
            } else if (rt == IRT_F128) {
                /* SSE+SSEUP comes back in the whole of xmm0, so the move
                 * must be the 16-byte one. */
                X64VReg r = newvv(is);
                X64VReg sv = to_vvreg(is, &in->ops[0]);

                x = emit(is, X64_OP_VMOV, X64_X);
                x->def = r;
                x->def_fixed = X64_XMM0 + 1;
                x->a = ovreg(sv);
                retregs[nret] = r;
                retfix[nret++] = X64_XMM0 + 1;
            } else if (irt_sse(rt)) {
                X64VReg r = newvf(is);
                X64VReg sv = to_fvreg(is, &in->ops[0]);

                x = emit(is, X64_OP_FMOV, fpw(rt));
                x->def = r;
                x->def_fixed = X64_XMM0 + 1;
                x->a = ovreg(sv);
                retregs[nret] = r;
                retfix[nret++] = X64_XMM0 + 1;
            } else {
                X64Width w = width_of((IrType)rt);
                X64VReg r = newv(is);
                X64Operand rv = to_src(is, &in->ops[0], w);

                x = emit(is, X64_OP_MOV, w);
                x->def = r;
                x->def_fixed = X64_RAX + 1;
                x->a = rv;
                retregs[nret] = r;
                retfix[nret++] = X64_RAX + 1;
            }
        }
        x = emit(is, X64_OP_RET, X64_Q);
        for (k = 0; k < nret; k++)
            x64_add_xuse(is->xf, x, retregs[k], retfix[k]);
        break;
    }
    case IR_BR: {
        X64Inst *x;

        edge_moves(is, &in->edges[0]);
        x = emit(is, X64_OP_JMP, X64_Q);
        x->target = in->edges[0].target.v;
        break;
    }
    case IR_CONDBR: {
        u32 t1, t2;
        X64Inst *x;
        const IrOperand *c = &in->ops[0];
        u8 cc = X64_CC_NE;
        u32 fsrc;

        /* Fusion: a same-block icmp whose flags are still live feeds the
         * jcc directly — never materialize 0/1 and retest. */
        if (c->kind == IROP_VALUE && is->vals[(u32)c->a].cc_plus1 &&
            is->last_flags_val == (u32)c->a &&
            is->vals[(u32)c->a].flags_blk == is->cur) {
            cc = (u8)(is->vals[(u32)c->a].cc_plus1 - 1);
            fsrc = is->vals[(u32)c->a].flags_ins;
        } else {
            X64VReg cv = to_vreg(is, c);

            x = emit(is, X64_OP_TEST, X64_L);
            x->a = ovreg(cv);
            x->b = ovreg(cv);
            fsrc = blk(is)->n - 1;
        }
        t1 = edge_target(is, &in->edges[0]);
        t2 = edge_target(is, &in->edges[1]);
        x = emit(is, X64_OP_JCC, X64_Q);
        x->cc = cc;
        x->flags = X64IF_USES_FLAGS;
        x->flags_src = fsrc;
        x->target = t1;
        x->target2 = t2;
        break;
    }
    case IR_SWITCH: {
        X64Width w = width_of((IrType)in->ops[0].type);
        X64VReg v = to_vreg(is, &in->ops[0]);
        u32 n = in->nedges - 1;
        u32 def = edge_target(is, &in->edges[0]);

        if (n == 0) {
            X64Inst *x = emit(is, X64_OP_JMP, X64_Q);

            x->target = def;
            break;
        }
        {
            i64 min = in->edges[1].case_val, max = min;
            u32 i;

            for (i = 1; i <= n; i++) {
                if (in->edges[i].case_val < min)
                    min = in->edges[i].case_val;
                if (in->edges[i].case_val > max)
                    max = in->edges[i].case_val;
            }
            /* density > ~40% and >= 5 cases -> RIP-relative table of
             * label addresses; bounds check + default first. Else a
             * balanced compare tree. Insertion-ordered emission. */
            if (n >= 5 && (u64)(max - min + 1) <= 0x10000 &&
                n * 100 > 40 * (u64)(max - min + 1)) {
                X64Func *xf = is->xf;
                X64Table *tb;
                X64VReg idx = newv(is);
                X64Inst *x;

                if (xf->ntables == xf->cap_tables) {
                    u32 nc = xf->cap_tables ? xf->cap_tables * 2 : 4;
                    X64Table *nt = arena_alloc(is->arena, nc * sizeof(X64Table),
                                               _Alignof(X64Table));

                    if (xf->ntables)
                        memcpy(nt, xf->tables, xf->ntables * sizeof(X64Table));
                    xf->tables = nt;
                    xf->cap_tables = nc;
                }
                tb = &xf->tables[xf->ntables];
                tb->n = (u32)(max - min + 1);
                tb->targets =
                    arena_alloc(is->arena, tb->n * sizeof(u32), _Alignof(u32));
                for (i = 0; i < tb->n; i++)
                    tb->targets[i] = def;
                for (i = 1; i <= n; i++)
                    tb->targets[in->edges[i].case_val - min] =
                        edge_target(is, &in->edges[i]);
                x = emit(is, X64_OP_MOV, w);
                x->def = idx;
                x->a = ovreg(v);
                if (min) {
                    x = emit(is, X64_OP_SUB, w);
                    x->def = idx;
                    x->a = ovreg(idx);
                    x->b = oimm(min);
                }
                x = emit(is, X64_OP_CMP, w);
                x->a = ovreg(idx);
                x->b = oimm((i64)(max - min));
                x = emit(is, X64_OP_JCC, X64_Q);
                x->cc = X64_CC_A; /* unsigned: negative wraps huge */
                x->flags = X64IF_USES_FLAGS;
                x->flags_src = blk(is)->n - 2;
                x->target = def;
                x->target2 = 0; /* falls into the jmptbl */
                x = emit(is, X64_OP_JMPTBL, X64_Q);
                x->a = ovreg(idx);
                x->table = xf->ntables++;
            } else {
                /* Linear compare chain, emitted in insertion order for
                 * determinism. Balancing remains a post-v0.1.0 tradeoff. */
                for (i = 1; i <= n; i++) {
                    u32 t = edge_target(is, &in->edges[i]);
                    X64Inst *x = emit(is, X64_OP_CMP, w);

                    x->a = ovreg(v);
                    x->b = oimm(in->edges[i].case_val);
                    x = emit(is, X64_OP_JCC, X64_Q);
                    x->cc = X64_CC_E;
                    x->flags = X64IF_USES_FLAGS;
                    x->flags_src = blk(is)->n - 2;
                    x->target = t;
                    x->target2 = 0; /* falls through to the next cmp */
                }
                {
                    X64Inst *x = emit(is, X64_OP_JMP, X64_Q);

                    x->target = def;
                }
            }
        }
        break;
    }
    case IR_UNREACHABLE:
        emit(is, X64_OP_UD2, X64_Q);
        break;
    case IR_CALL: {
        /* The SysV call sequence. Both register queues advance
         * INDEPENDENTLY (rdi..r9 and xmm0..7); exhausted queues spill
         * to the outgoing area at [rsp+0..) — 8-byte slots, f80 in
         * 16-aligned 16-byte slots, byval aggregates copied inline.
         * Stack traffic is emitted FIRST so the fixed-register arg
         * intervals stay tiny (mov -> call). AL carries the xmm-arg
         * count for variadic callees — forgetting it corrupts the
         * callee's va_arg, and only when it reads FP. */
        static const u8 gpq[6] = {X64_RDI, X64_RSI, X64_RDX,
                                  X64_RCX, X64_R8,  X64_R9};
        u32 first = in->subop == FUNCREF_INDIRECT ? 1 : 0;
        u32 gp = 0, fp = 0, off = 0;
        u32 nargs = in->nops - first;
        typedef struct CallArgPlan {
            u32 stk_off;
            u8 in_reg;
            u8 reg_slot;
        } CallArgPlan;
        CallArgPlan *plans =
            arena_alloc(is->arena, (nargs ? nargs : 1) * sizeof(*plans),
                        _Alignof(CallArgPlan));
        /* 0 stack, 1 gp, 2 xmm (8 bytes), 3 skipped-pair-ptr,
         * 4 xmm-16 (an f128: SSE+SSEUP, a WHOLE xmm).
         *
         * 3 IS TAKEN, and pass 2b deliberately has no branch for it --
         * a skipped pair pointer emits nothing. f128 first reused that
         * value and every pair-returning call started moving a GP vreg
         * with a 16-byte vector move; the MIR verifier's register-bank
         * check caught it. Read this line before adding the next one. */
        const IrOperand *pairp = NULL;
        u64 pair_ann = 0;
        X64XUse argx[16];
        u32 nargx = 0;
        X64VReg fnv = {0};
        X64Inst *x;
        u32 i2;

        /* Pass 1: queue walk + stack layout. */
        for (i2 = first; i2 < in->nops; i2++) {
            const IrOperand *o = &in->ops[i2];
            u64 ann =
                (o->kind == IROP_VALUE || o->kind == IROP_SYMBOL) ? o->b : 0;
            u32 kind = ir_arg_kind(ann);
            u32 idx = i2 - first;

            if (kind >= IR_ARG_PAIR_II && kind <= IR_ARG_PAIR_SS) {
                /* The pair hidden pointer is IR bookkeeping only — it
                 * is NOT passed at runtime; the caller stores the
                 * register pair through it after the call. */
                plans[idx].in_reg = 3;
                pairp = o;
                pair_ann = ann;
                continue;
            }
            if (kind == IR_ARG_BYVAL) {
                u32 sz = (ir_arg_size(ann) + 7) & ~7u;

                plans[idx].in_reg = 0;
                plans[idx].stk_off = off;
                off += sz;
                continue;
            }
            if (o->type == IRT_F80) {
                off = (off + 15) & ~15u;
                plans[idx].in_reg = 0;
                plans[idx].stk_off = off;
                off += 16;
                continue;
            }
            if (o->type == IRT_F128) {
                /* SSE+SSEUP: one WHOLE xmm register, or 16 bytes of stack
                 * at 16-byte alignment when the eight are used up. */
                if (fp < 8) {
                    plans[idx].in_reg = 4;
                    plans[idx].reg_slot = (u8)fp++;
                } else {
                    off = (off + 15) & ~15u;
                    plans[idx].in_reg = 0;
                    plans[idx].stk_off = off;
                    off += 16;
                }
                continue;
            }
            if (irt_sse(o->type) ||
                (o->kind == IROP_FCONST && irt_sse(o->type))) {
                if (fp < 8) {
                    plans[idx].in_reg = 2;
                    plans[idx].reg_slot = (u8)fp++;
                } else {
                    plans[idx].in_reg = 0;
                    plans[idx].stk_off = off;
                    off += 8;
                }
                continue;
            }
            if (gp < 6) {
                plans[idx].in_reg = 1;
                plans[idx].reg_slot = (u8)gp++;
            } else {
                plans[idx].in_reg = 0;
                plans[idx].stk_off = off;
                off += 8;
            }
        }
        if (off > is->xf->out_args)
            is->xf->out_args = off;

        if (in->subop == FUNCREF_INDIRECT)
            fnv = to_vreg(is, &in->ops[0]);

        /* Pass 2a: stack traffic. */
        for (i2 = first; i2 < in->nops; i2++) {
            const IrOperand *o = &in->ops[i2];
            u32 idx = i2 - first;
            u64 ann =
                (o->kind == IROP_VALUE || o->kind == IROP_SYMBOL) ? o->b : 0;

            if (plans[idx].in_reg != 0)
                continue;
            if (ir_arg_kind(ann) == IR_ARG_BYVAL) {
                /* Inline word copy of the pointee onto the stack (the
                 * memcpy intrinsic is Sprint 24's; correctness first). */
                u32 sz = ir_arg_size(ann);
                X64VReg p = to_vreg(is, o);
                u32 c = 0;

                while (c < sz) {
                    u32 step = sz - c >= 8   ? 8
                               : sz - c >= 4 ? 4
                               : sz - c >= 2 ? 2
                                             : 1;
                    X64VReg t = newv(is);

                    x = emit(is, X64_OP_LOAD, (X64Width)step);
                    x->def = t;
                    x->a.kind = X64O_MEM;
                    x->a.mem.base = p;
                    x->a.mem.scale = 1;
                    x->a.mem.disp = (i32)c;
                    x = emit(is, X64_OP_STORE, (X64Width)step);
                    x->a = ovreg(t);
                    x->b.kind = X64O_MEM;
                    x->b.mem.rsp_rel = 1;
                    x->b.mem.scale = 1;
                    x->b.mem.disp = (i32)(plans[idx].stk_off + c);
                    c += step;
                }
                continue;
            }
            if (o->type == IRT_F80) {
                X64VReg a80 = f80_addr(is, o);

                x87_mem(is, X64_OP_X87_FLD, X64_T, a80, 0);
                x = emit(is, X64_OP_X87_FSTP, X64_T);
                x->a.kind = X64O_MEM;
                x->a.mem.rsp_rel = 1;
                x->a.mem.scale = 1;
                x->a.mem.disp = (i32)plans[idx].stk_off;
                continue;
            }
            if (o->type == IRT_F128) {
                X64VReg fv = to_vvreg(is, o);

                x = emit(is, X64_OP_VSTORE, X64_X);
                x->a = ovreg(fv);
                x->b.kind = X64O_MEM;
                x->b.mem.rsp_rel = 1;
                x->b.mem.scale = 1;
                x->b.mem.disp = (i32)plans[idx].stk_off;
                continue;
            }
            if (irt_sse(o->type)) {
                X64VReg fv = to_fvreg(is, o);

                x = emit(is, X64_OP_FSTORE, fpw(o->type));
                x->a = ovreg(fv);
                x->b.kind = X64O_MEM;
                x->b.mem.rsp_rel = 1;
                x->b.mem.scale = 1;
                x->b.mem.disp = (i32)plans[idx].stk_off;
                continue;
            }
            {
                X64Operand v = to_src(is, o, X64_Q);

                x = emit(is, X64_OP_STORE, X64_Q);
                x->a = v;
                x->b.kind = X64O_MEM;
                x->b.mem.rsp_rel = 1;
                x->b.mem.scale = 1;
                x->b.mem.disp = (i32)plans[idx].stk_off;
            }
        }
        /* Pass 2b: register args (tiny fixed intervals next to the
         * call), then AL, then the call itself carrying every implicit
         * register use. */
        for (i2 = first; i2 < in->nops; i2++) {
            const IrOperand *o = &in->ops[i2];
            u32 idx = i2 - first;

            if (plans[idx].in_reg == 1) {
                X64VReg t = newv(is);
                u8 fix = (u8)(gpq[plans[idx].reg_slot] + 1);
                /* operands BEFORE their consumer emits — the Sprint 21
                 * ordering law (a symbol arg materializes via lea). */
                X64Operand av = to_src(is, o, X64_Q);

                x = emit(is, X64_OP_MOV, X64_Q);
                x->def = t;
                x->def_fixed = fix;
                x->a = av;
                argx[nargx].r = t;
                argx[nargx++].fixed = fix;
            } else if (plans[idx].in_reg == 4) {
                /* Whole-register move: VMOV is the 16-byte form, so the
                 * upper eightbyte (the SSEUP half) travels too. FMOV would
                 * move 8 bytes and silently drop half the value. */
                X64VReg t = newvv(is);
                u8 fix = (u8)(X64_XMM0 + plans[idx].reg_slot + 1);
                X64VReg sv = to_vvreg(is, o);

                x = emit(is, X64_OP_VMOV, X64_X);
                x->def = t;
                x->def_fixed = fix;
                x->a = ovreg(sv);
                argx[nargx].r = t;
                argx[nargx++].fixed = fix;
            } else if (plans[idx].in_reg == 2) {
                X64VReg t = newvf(is);
                u8 fix = (u8)(X64_XMM0 + plans[idx].reg_slot + 1);
                X64VReg sv = to_fvreg(is, o);

                x = emit(is, X64_OP_FMOV, fpw(o->type));
                x->def = t;
                x->def_fixed = fix;
                x->a = ovreg(sv);
                argx[nargx].r = t;
                argx[nargx++].fixed = fix;
            }
        }
        if (in->flags & IRF_CALL_VARIADIC) {
            X64VReg t = newv(is);

            x = emit(is, X64_OP_MOV, X64_L);
            x->def = t;
            x->def_fixed = X64_RAX + 1;
            x->a = oimm((i64)fp);
            argx[nargx].r = t;
            argx[nargx++].fixed = X64_RAX + 1;
        }
        {
            u8 retty = in->type;
            bool ret80 = retty == IRT_F80;
            u32 k;

            x = emit(is, X64_OP_CALL, ret80 ? X64_T : X64_Q);
            if (in->subop == FUNCREF_INDIRECT) {
                x->a = ovreg(fnv);
            } else if (in->subop == FUNCREF_EXTERNAL) {
                x->a.kind = X64O_MEM;
                x->a.mem.rip_sym = in->callee + 1;
                if (sym_call_needs_plt(is, in->callee + 1))
                    x->flags |= X64IF_CALL_PLT;
            } else {
                x->table = in->callee + 1; /* internal func index */
                /* An INTERNAL funcref means the module carries the body, not
                 * that the symbol is unpreemptible: an external-linkage
                 * definition can still be interposed, so full PIC routes it
                 * through the PLT as well. gcc does the same. */
                if (is->pic == X64_PIC_FULL &&
                    is->m->funcs[in->callee].linkage != IRLINK_INTERNAL)
                    x->flags |= X64IF_CALL_PLT;
            }
            for (k = 0; k < nargx; k++)
                x64_add_xuse(is->xf, x, argx[k].r, argx[k].fixed);
            if (pairp) {
                /* PAIR return: read the register pair, store through
                 * the hidden pointer (which was never passed). */
                u32 kind = ir_arg_kind(pair_ann);
                bool sse0 = kind == IR_ARG_PAIR_SI || kind == IR_ARG_PAIR_SS;
                bool sse1 = kind == IR_ARG_PAIR_IS || kind == IR_ARG_PAIR_SS;
                u32 psz = ir_arg_size(pair_ann);
                X64VReg pv = to_vreg(is, pairp);
                u8 gp_next = X64_RAX;

                for (k = 0; k < 2 && 8 * k < psz; k++) {
                    bool sse = k ? sse1 : sse0;
                    X64VReg r = sse ? newvf(is) : newv(is);

                    x = emit(is, X64_OP_READREG, X64_Q);
                    x->def = r;
                    if (sse) {
                        x->def_fixed = (u8)(X64_XMM0 + (k && sse0 ? 1 : 0) + 1);
                    } else {
                        x->def_fixed = (u8)(gp_next + 1);
                        gp_next = X64_RDX;
                    }
                    if (sse) {
                        x = emit(is, X64_OP_FSTORE, X64_Q);
                        x->a = ovreg(r);
                    } else {
                        x = emit(is, X64_OP_STORE, X64_Q);
                        x->a = ovreg(r);
                    }
                    x->b.kind = X64O_MEM;
                    x->b.mem.base = pv;
                    x->b.mem.scale = 1;
                    x->b.mem.disp = (i32)(8 * k);
                }
            } else if (ret80) {
                X64VReg slot = f80_slot(is);

                x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
                if (in->result.v)
                    is->vals[in->result.v].vr = slot;
            } else if (in->result.v && retty == IRT_F128) {
                X64VReg d = newvv(is);
                X64Inst *rr = emit(is, X64_OP_READREG, X64_X);

                rr->def = d;
                rr->def_fixed = X64_XMM0 + 1;
                is->vals[in->result.v].vr = d;
            } else if (in->result.v && irt_sse(retty)) {
                X64VReg d = newvf(is);
                X64Inst *rr = emit(is, X64_OP_READREG, fpw(retty));

                rr->def = d;
                rr->def_fixed = X64_XMM0 + 1;
                is->vals[in->result.v].vr = d;
            } else if (in->result.v) {
                X64VReg d = newv(is);
                X64Inst *rr = emit(is, X64_OP_READREG, X64_Q);

                rr->def = d;
                rr->def_fixed = X64_RAX + 1;
                is->vals[in->result.v].vr = d;
            }
        }
        break;
    }
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV: {
        if (irt_vector(in->type)) {
            X64VReg d = newvv(is);
            X64VReg a = to_vvreg(is, &in->ops[0]);
            X64VReg b = to_vvreg(is, &in->ops[1]);
            X64Op op = in->op == IR_FADD   ? X64_OP_VADD
                       : in->op == IR_FSUB ? X64_OP_VSUB
                       : in->op == IR_FMUL ? X64_OP_VMUL
                                           : X64_OP_VDIV;
            X64Inst *x = emit(is, op, X64_X);

            x->src_width = in->type;
            x->def = d;
            x->a = ovreg(a);
            x->b = ovreg(b);
            is->vals[in->result.v].vr = d;
            break;
        }
        if (in->type == IRT_F128)
            CGF_ICE("x86_64 isel: f128 is outside the v0.1.0 scope "
                    "contract");
        if (in->type == IRT_F80) {
            /* load-op-store, locally balanced: fld a; fld b; op-p;
             * fstpt dst. faddp/fsubp/fmulp/fdivp compute st1 OP st0
             * with a in st1 — exactly a OP b. */
            X64VReg av = f80_addr(is, &in->ops[0]);
            X64VReg bv = f80_addr(is, &in->ops[1]);
            X64VReg slot = f80_slot(is);
            X64Op op = in->op == IR_FADD   ? X64_OP_X87_FADDP
                       : in->op == IR_FSUB ? X64_OP_X87_FSUBP
                       : in->op == IR_FMUL ? X64_OP_X87_FMULP
                                           : X64_OP_X87_FDIVP;

            x87_mem(is, X64_OP_X87_FLD, X64_T, av, 0);
            x87_mem(is, X64_OP_X87_FLD, X64_T, bv, 0);
            x87_op0(is, op);
            x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
            is->vals[in->result.v].vr = slot;
            break;
        }
        {
            X64Width w = fpw(in->type);
            X64VReg d = newvf(is);
            X64VReg av = to_fvreg(is, &in->ops[0]);
            X64VReg bv = to_fvreg(is, &in->ops[1]);
            X64Op op = in->op == IR_FADD   ? X64_OP_FADD
                       : in->op == IR_FSUB ? X64_OP_FSUB
                       : in->op == IR_FMUL ? X64_OP_FMUL
                                           : X64_OP_FDIV;
            X64Inst *x = emit(is, op, w);

            x->def = d;
            x->a = ovreg(av);
            x->b = ovreg(bv);
            is->vals[in->result.v].vr = d;
        }
        break;
    }
    case IR_FNEG: {
        if (in->type == IRT_F80) {
            X64VReg av = f80_addr(is, &in->ops[0]);
            X64VReg slot = f80_slot(is);

            x87_mem(is, X64_OP_X87_FLD, X64_T, av, 0);
            x87_op0(is, X64_OP_X87_FCHS);
            x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
            is->vals[in->result.v].vr = slot;
            break;
        }
        {
            /* No FP neg instruction: xorp{s,d} with the sign mask from
             * .rodata (only the low lane matters; the full-width xor is
             * harmless on our scalar discipline). */
            X64Width w = fpw(in->type);
            X64VReg d = newvf(is);
            X64VReg av = to_fvreg(is, &in->ops[0]);
            u32 cp = in->type == IRT_F32
                         ? x64_cpool_intern(is->xf, 0x80000000ull, 0, 16, 16)
                         : x64_cpool_intern(is->xf, 0x8000000000000000ull, 0,
                                            16, 16);
            X64Inst *x = emit(is, X64_OP_FXORM, w);

            x->def = d;
            x->a = ovreg(av);
            x->b.kind = X64O_MEM;
            x->b.mem.cpool = cp;
            is->vals[in->result.v].vr = d;
        }
        break;
    }
    case IR_VSPLAT: {
        IrType vt = (IrType)in->type;
        IrType et = ir_vector_elem_type(vt);
        X64VReg d = newvv(is);
        X64Inst *x;

        if (et == IRT_F32 || et == IRT_F64) {
            X64VReg s = to_fvreg(is, &in->ops[0]);

            if (et == IRT_F32) {
                x = emit(is, X64_OP_VSHUF32, X64_X);
                x->def = d;
                x->a = ovreg(s);
                x->b = oimm(0);
            } else {
                x = emit(is, X64_OP_VUNPCKLQ, X64_X);
                x->def = d;
                x->a = ovreg(s);
                x->b = ovreg(s);
            }
        } else {
            X64VReg s = to_vreg(is, &in->ops[0]);
            X64VReg bits = newvv(is);

            x = emit(is, X64_OP_MOVQXR, et == IRT_I64 ? X64_Q : X64_L);
            x->def = bits;
            x->a = ovreg(s);
            if (et == IRT_I8) {
                X64VReg t = newvv(is);

                x = emit(is, X64_OP_VUNPCKLBW, X64_X);
                x->def = t;
                x->a = ovreg(bits);
                x->b = ovreg(bits);
                bits = t;
                t = newvv(is);
                x = emit(is, X64_OP_VUNPCKLWD, X64_X);
                x->def = t;
                x->a = ovreg(bits);
                x->b = ovreg(bits);
                bits = t;
            }
            if (et == IRT_I16) {
                x = emit(is, X64_OP_VSHUFLO16, X64_X);
                x->def = d;
                x->a = ovreg(bits);
                x->b = oimm(0);
                bits = d;
            }
            if (et == IRT_I64) {
                x = emit(is, X64_OP_VUNPCKLQ, X64_X);
                x->def = d;
                x->a = ovreg(bits);
                x->b = ovreg(bits);
            } else {
                x = emit(is, X64_OP_VSHUF32, X64_X);
                x->def = d;
                x->a = ovreg(bits);
                x->b = oimm(0);
            }
        }
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_VEXTRACT: {
        IrType vt = (IrType)in->ops[0].type;
        IrType et = ir_vector_elem_type(vt);
        X64VReg v = to_vvreg(is, &in->ops[0]);

        if (in->subop)
            v = vector_shift_bytes(is, v, (u8)(in->subop * ir_type_size(et)));
        is->vals[in->result.v].vr = vector_extract_low(is, v, et);
        break;
    }
    case IR_VREDUCE_ADD:
    case IR_VREDUCE_MUL:
    case IR_VREDUCE_AND:
    case IR_VREDUCE_OR:
    case IR_VREDUCE_XOR: {
        IrType vt = (IrType)in->ops[0].type;
        IrType et = ir_vector_elem_type(vt);
        X64VReg acc = to_vvreg(is, &in->ops[0]);
        u32 lanes = ir_vector_lanes(vt);
        u32 span = 8;
        X64Op op = in->op == IR_VREDUCE_ADD   ? X64_OP_VADD
                   : in->op == IR_VREDUCE_MUL ? X64_OP_VMUL
                   : in->op == IR_VREDUCE_AND ? X64_OP_VAND
                   : in->op == IR_VREDUCE_OR  ? X64_OP_VOR
                                              : X64_OP_VXOR;

        while (lanes > 1) {
            X64VReg hi = vector_shift_bytes(is, acc, (u8)span);
            X64VReg d = newvv(is);
            X64Inst *x = emit(is, op, X64_X);

            x->src_width = (u8)vt;
            x->def = d;
            x->a = ovreg(acc);
            x->b = ovreg(hi);
            acc = d;
            lanes >>= 1;
            span >>= 1;
        }
        is->vals[in->result.v].vr = vector_extract_low(is, acc, et);
        break;
    }
    case IR_FCMP: {
        const FcmpRecipe *rc = &fcmp_recipes[in->subop];
        u8 st = in->ops[0].type;
        u32 flags_ins;

        if (st == IRT_F128)
            CGF_ICE("x86_64 isel: f128 is outside the v0.1.0 scope "
                    "contract");
        if (st == IRT_F80) {
            /* fucomip compares st0 vs st1 (then pops); loading b first
             * and a second puts a in st0 — flags read a ? b. A swapped
             * recipe just reverses the load order. */
            X64VReg av = f80_addr(is, &in->ops[rc->swap ? 1 : 0]);
            X64VReg bv = f80_addr(is, &in->ops[rc->swap ? 0 : 1]);

            x87_mem(is, X64_OP_X87_FLD, X64_T, bv, 0);
            x87_mem(is, X64_OP_X87_FLD, X64_T, av, 0);
            x87_op0(is, X64_OP_X87_FUCOMIP);
            flags_ins = blk(is)->n - 1;
            x87_op0(is, X64_OP_X87_FPOP);
        } else {
            X64Width w = fpw(st);
            X64VReg av = to_fvreg(is, &in->ops[rc->swap ? 1 : 0]);
            X64VReg bv = to_fvreg(is, &in->ops[rc->swap ? 0 : 1]);
            X64Inst *x = emit(is, X64_OP_UCOMI, w);

            x->a = ovreg(av);
            x->b = ovreg(bv);
            flags_ins = blk(is)->n - 1;
        }
        /* Single-cc recipes fuse into condbr exactly like icmp (setcc
         * leaves the flags intact); pair recipes end in and/or, which
         * clobbers flags, so their branches test the materialized
         * value. The materialized form itself IS the recipe table. */
        is->vals[in->result.v].cc_plus1 = rc->comb ? 0 : (u8)(rc->cc1 + 1);
        is->vals[in->result.v].flags_ins = flags_ins;
        is->vals[in->result.v].flags_blk = is->cur;
        is->last_flags_val = rc->comb ? 0 : in->result.v;
        if (rc->comb || is->use_count[in->result.v] > 1 ||
            !(irb->last && irb->last->op == IR_CONDBR &&
              irb->last->ops[0].kind == IROP_VALUE &&
              (u32)irb->last->ops[0].a == in->result.v))
            is->vals[in->result.v].vr =
                fcmp_value(is, rc, is->vals[in->result.v].flags_ins);
        break;
    }
    case IR_FPEXT:
    case IR_FPTRUNC: {
        u8 st = in->ops[0].type;
        u8 dt = in->type;

        if (st == IRT_F128 || dt == IRT_F128)
            CGF_ICE("x86_64 isel: f128 is outside the v0.1.0 scope "
                    "contract");
        if (irt_sse(st) && irt_sse(dt)) {
            X64VReg d = newvf(is);
            X64VReg sv = to_fvreg(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_CVTF2F, fpw(dt));

            x->src_width = (u8)fpw(st);
            x->def = d;
            x->a = ovreg(sv);
            is->vals[in->result.v].vr = d;
            break;
        }
        if (irt_sse(st) && dt == IRT_F80) {
            /* xmm -> memory -> x87 -> f80 slot (fldl/flds converts). */
            X64VReg tmp = f80_slot(is);
            X64VReg slot = f80_slot(is);
            X64VReg sv = to_fvreg(is, &in->ops[0]);
            X64Inst *x = emit(is, X64_OP_FSTORE, fpw(st));

            x->a = ovreg(sv);
            x->b.kind = X64O_MEM;
            x->b.mem.base = tmp;
            x->b.mem.scale = 1;
            x87_mem(is, X64_OP_X87_FLD, fpw(st), tmp, 0);
            x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
            is->vals[in->result.v].vr = slot;
            break;
        }
        if (st == IRT_F80 && irt_sse(dt)) {
            /* f80 -> x87 store-convert -> xmm (fstpl/fstps rounds). */
            X64VReg sv = f80_addr(is, &in->ops[0]);
            X64VReg tmp = f80_slot(is);
            X64VReg d = newvf(is);
            X64Inst *x;

            x87_mem(is, X64_OP_X87_FLD, X64_T, sv, 0);
            x87_mem(is, X64_OP_X87_FSTP, fpw(dt), tmp, 0);
            x = emit(is, X64_OP_FLOAD, fpw(dt));
            x->def = d;
            x->a.kind = X64O_MEM;
            x->a.mem.base = tmp;
            x->a.mem.scale = 1;
            is->vals[in->result.v].vr = d;
            break;
        }
        CGF_ICE("x86_64 isel: bad fpext/fptrunc %u -> %u", st, dt);
    }
    case IR_FPTOSI:
    case IR_FPTOUI: {
        u8 st = in->ops[0].type;
        u8 dt = in->type;
        bool uns = in->op == IR_FPTOUI;

        if (st == IRT_F128)
            CGF_ICE("x86_64 isel: f128 is outside the v0.1.0 scope "
                    "contract");
        if (st == IRT_F80) {
            /* Truncation needs the RC dance (x87 default rounds to
             * nearest, C wants truncation); f80 -> u64 additionally
             * subtracts 2^63 above the signed range, arms balanced. */
            X64VReg sv = f80_addr(is, &in->ops[0]);

            if (uns && dt == IRT_I64) {
                u32 c63 = x64_cpool_intern(is->xf, 0x8000000000000000ull,
                                           0x403e, 10, 16); /* 2^63 f80 */
                X64VReg c63a = newv(is);
                X64VReg d = newv(is);
                u32 bsmall = new_block(is, "f2u.s");
                u32 bbig = new_block(is, "f2u.b");
                u32 bjoin = new_block(is, "f2u.j");
                X64VReg tmp;
                X64Inst *x;

                x = emit(is, X64_OP_LEA, X64_Q);
                x->def = c63a;
                x->a.kind = X64O_MEM;
                x->a.mem.cpool = c63;
                x87_mem(is, X64_OP_X87_FLD, X64_T, c63a, 0);
                x87_mem(is, X64_OP_X87_FLD, X64_T, sv, 0);
                x87_op0(is, X64_OP_X87_FUCOMIP); /* src ? 2^63 */
                x87_op0(is, X64_OP_X87_FPOP);
                x = emit(is, X64_OP_JCC, X64_Q);
                x->cc = X64_CC_B;
                x->flags = X64IF_USES_FLAGS;
                x->flags_src = blk(is)->n - 3;
                x->target = bsmall;
                x->target2 = bbig;
                is->cur = bsmall - 1;
                tmp = f80_slot(is);
                x87_mem(is, X64_OP_X87_FLD, X64_T, sv, 0);
                x87_trunc_store(is, tmp);
                x = emit(is, X64_OP_LOAD, X64_Q);
                x->def = d;
                x->a.kind = X64O_MEM;
                x->a.mem.base = tmp;
                x->a.mem.scale = 1;
                x = emit(is, X64_OP_JMP, X64_Q);
                x->target = bjoin;
                is->cur = bbig - 1;
                tmp = f80_slot(is);
                x87_mem(is, X64_OP_X87_FLD, X64_T, sv, 0);
                x87_mem(is, X64_OP_X87_FLD, X64_T, c63a, 0);
                x87_op0(is, X64_OP_X87_FSUBP); /* src - 2^63 */
                x87_trunc_store(is, tmp);
                {
                    X64VReg t1 = newv(is);
                    X64VReg m = newv(is);
                    X64VReg t2 = newv(is);

                    x = emit(is, X64_OP_LOAD, X64_Q);
                    x->def = t1;
                    x->a.kind = X64O_MEM;
                    x->a.mem.base = tmp;
                    x->a.mem.scale = 1;
                    x = emit(is, X64_OP_MOVABS, X64_Q);
                    x->def = m;
                    x->a = oimm((i64)0x8000000000000000ull);
                    x = emit(is, X64_OP_XOR, X64_Q);
                    x->def = t2;
                    x->a = ovreg(t1);
                    x->b = ovreg(m);
                    x = emit(is, X64_OP_MOV, X64_Q);
                    x->def = d;
                    x->a = ovreg(t2);
                }
                x = emit(is, X64_OP_JMP, X64_Q);
                x->target = bjoin;
                is->cur = bjoin - 1;
                is->vals[in->result.v].vr = d;
                break;
            }
            {
                X64VReg tmp = f80_slot(is);
                X64VReg d = newv(is);
                X64Inst *x;

                x87_mem(is, X64_OP_X87_FLD, X64_T, sv, 0);
                x87_trunc_store(is, tmp);
                x = emit(is, X64_OP_LOAD, dt == IRT_I64 || uns ? X64_Q : X64_L);
                x->def = d;
                x->a.kind = X64O_MEM;
                x->a.mem.base = tmp;
                x->a.mem.scale = 1;
                is->vals[in->result.v].vr = d;
            }
            break;
        }
        /* SSE sources: cvtts{s,d}2si — the TRUNCATING form (the missing
         * 't' rounds in the current mode: silent wrong answers). */
        if (uns && dt == IRT_I64) {
            /* f -> u64: no instruction. Below 2^63 the signed convert
             * is exact; above, subtract 2^63, convert, put the top bit
             * back. Out of range is C UB — matches gcc. */
            u32 climit =
                st == IRT_F32
                    ? x64_cpool_intern(is->xf, 0x5f000000ull, 0, 4, 4)
                    : x64_cpool_intern(is->xf, 0x43e0000000000000ull, 0, 8, 8);
            X64VReg sv = to_fvreg(is, &in->ops[0]);
            X64VReg lim = newvf(is);
            X64VReg d = newv(is);
            u32 bsmall = new_block(is, "f2u.s");
            u32 bbig = new_block(is, "f2u.b");
            u32 bjoin = new_block(is, "f2u.j");
            X64Inst *x;

            x = emit(is, X64_OP_FLOAD, fpw(st));
            x->def = lim;
            x->a.kind = X64O_MEM;
            x->a.mem.cpool = climit;
            x = emit(is, X64_OP_UCOMI, fpw(st));
            x->a = ovreg(sv);
            x->b = ovreg(lim);
            x = emit(is, X64_OP_JCC, X64_Q);
            x->cc = X64_CC_B; /* src < 2^63 (CF; NaN lands small = UB) */
            x->flags = X64IF_USES_FLAGS;
            x->flags_src = blk(is)->n - 2;
            x->target = bsmall;
            x->target2 = bbig;
            is->cur = bsmall - 1;
            x = emit(is, X64_OP_CVTF2I, X64_Q);
            x->src_width = (u8)fpw(st);
            x->def = d;
            x->a = ovreg(sv);
            x = emit(is, X64_OP_JMP, X64_Q);
            x->target = bjoin;
            is->cur = bbig - 1;
            {
                X64VReg t = newvf(is);
                X64VReg ti = newv(is);
                X64VReg m = newv(is);
                X64VReg t2 = newv(is);

                x = emit(is, X64_OP_FSUB, fpw(st));
                x->def = t;
                x->a = ovreg(sv);
                x->b = ovreg(lim);
                x = emit(is, X64_OP_CVTF2I, X64_Q);
                x->src_width = (u8)fpw(st);
                x->def = ti;
                x->a = ovreg(t);
                x = emit(is, X64_OP_MOVABS, X64_Q);
                x->def = m;
                x->a = oimm((i64)0x8000000000000000ull);
                x = emit(is, X64_OP_XOR, X64_Q);
                x->def = t2;
                x->a = ovreg(ti);
                x->b = ovreg(m);
                x = emit(is, X64_OP_MOV, X64_Q);
                x->def = d;
                x->a = ovreg(t2);
            }
            x = emit(is, X64_OP_JMP, X64_Q);
            x->target = bjoin;
            is->cur = bjoin - 1;
            is->vals[in->result.v].vr = d;
            break;
        }
        {
            /* Signed converts; f -> u32 rides the 64-bit convert (the
             * value fits, the low half is the answer). */
            X64VReg sv = to_fvreg(is, &in->ops[0]);
            X64VReg d = newv(is);
            X64Inst *x =
                emit(is, X64_OP_CVTF2I, (dt == IRT_I64 || uns) ? X64_Q : X64_L);

            x->src_width = (u8)fpw(st);
            x->def = d;
            x->a = ovreg(sv);
            is->vals[in->result.v].vr = d;
        }
        break;
    }
    case IR_SITOFP:
    case IR_UITOFP: {
        u8 st = in->ops[0].type;
        u8 dt = in->type;
        bool uns = in->op == IR_UITOFP;

        if (dt == IRT_F128)
            CGF_ICE("x86_64 isel: f128 is outside the v0.1.0 scope "
                    "contract");
        if (dt == IRT_F80) {
            /* Integers reach x87 through memory (fildq); u64 above the
             * signed range adds 2^64 back in a balanced arm. */
            X64VReg sv = to_vreg(is, &in->ops[0]);
            X64VReg wide = sv;
            X64VReg tmp = f80_slot(is);
            X64VReg slot = f80_slot(is);
            X64Inst *x;

            if (st == IRT_I8 || st == IRT_I16 || st == IRT_I32) {
                wide = newv(is);
                if (uns) {
                    x = emit(is, st == IRT_I32 ? X64_OP_MOV : X64_OP_MOVZX,
                             X64_L);
                    if (st != IRT_I32)
                        x->src_width = (u8)width_of((IrType)st);
                } else {
                    x = emit(is, X64_OP_MOVSX, X64_Q);
                    x->src_width = (u8)width_of((IrType)st);
                }
                x->def = wide;
                x->a = ovreg(sv);
            }
            x = emit(is, X64_OP_STORE, X64_Q);
            x->a = ovreg(wide);
            x->b.kind = X64O_MEM;
            x->b.mem.base = tmp;
            x->b.mem.scale = 1;
            if (uns && st == IRT_I64) {
                u32 c64 = x64_cpool_intern(is->xf, 0x8000000000000000ull,
                                           0x403f, 10, 16); /* 2^64 f80 */
                u32 bpos = new_block(is, "u2f.p");
                u32 bneg = new_block(is, "u2f.n");
                u32 bjoin = new_block(is, "u2f.j");

                x = emit(is, X64_OP_CMP, X64_Q);
                x->a = ovreg(wide);
                x->b = oimm(0);
                x = emit(is, X64_OP_JCC, X64_Q);
                x->cc = X64_CC_L; /* sign set: 2^63 <= v */
                x->flags = X64IF_USES_FLAGS;
                x->flags_src = blk(is)->n - 2;
                x->target = bneg;
                x->target2 = bpos;
                is->cur = bpos - 1;
                x87_mem(is, X64_OP_X87_FILD, X64_Q, tmp, 0);
                x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
                x = emit(is, X64_OP_JMP, X64_Q);
                x->target = bjoin;
                is->cur = bneg - 1;
                {
                    X64VReg ca = newv(is);

                    x87_mem(is, X64_OP_X87_FILD, X64_Q, tmp, 0);
                    x = emit(is, X64_OP_LEA, X64_Q);
                    x->def = ca;
                    x->a.kind = X64O_MEM;
                    x->a.mem.cpool = c64;
                    x87_mem(is, X64_OP_X87_FLD, X64_T, ca, 0);
                    x87_op0(is, X64_OP_X87_FADDP);
                    x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
                }
                x = emit(is, X64_OP_JMP, X64_Q);
                x->target = bjoin;
                is->cur = bjoin - 1;
            } else {
                x87_mem(is, X64_OP_X87_FILD, X64_Q, tmp, 0);
                x87_mem(is, X64_OP_X87_FSTP, X64_T, slot, 0);
            }
            is->vals[in->result.v].vr = slot;
            break;
        }
        if (uns && st == IRT_I64) {
            /* u64 -> f: halve with a sticky low bit, convert, double —
             * ONE rounding total (the sprint's verbatim sequence). */
            X64VReg sv = to_vreg(is, &in->ops[0]);
            X64VReg d = newvf(is);
            u32 bpos = new_block(is, "u2f.p");
            u32 bneg = new_block(is, "u2f.n");
            u32 bjoin = new_block(is, "u2f.j");
            X64Inst *x;

            x = emit(is, X64_OP_CMP, X64_Q);
            x->a = ovreg(sv);
            x->b = oimm(0);
            x = emit(is, X64_OP_JCC, X64_Q);
            x->cc = X64_CC_L;
            x->flags = X64IF_USES_FLAGS;
            x->flags_src = blk(is)->n - 2;
            x->target = bneg;
            x->target2 = bpos;
            is->cur = bpos - 1;
            x = emit(is, X64_OP_CVTI2F, fpw(dt));
            x->src_width = X64_Q;
            x->def = d;
            x->a = ovreg(sv);
            x = emit(is, X64_OP_JMP, X64_Q);
            x->target = bjoin;
            is->cur = bneg - 1;
            {
                X64VReg h = newv(is);
                X64VReg lo = newv(is);
                X64VReg t = newv(is);
                X64VReg dt2 = newvf(is);

                x = emit(is, X64_OP_SHR, X64_Q);
                x->def = h;
                x->a = ovreg(sv);
                x->b = oimm(1);
                x = emit(is, X64_OP_AND, X64_Q);
                x->def = lo;
                x->a = ovreg(sv);
                x->b = oimm(1);
                x = emit(is, X64_OP_OR, X64_Q);
                x->def = t;
                x->a = ovreg(h);
                x->b = ovreg(lo);
                x = emit(is, X64_OP_CVTI2F, fpw(dt));
                x->src_width = X64_Q;
                x->def = dt2;
                x->a = ovreg(t);
                x = emit(is, X64_OP_FADD, fpw(dt));
                x->def = d;
                x->a = ovreg(dt2);
                x->b = ovreg(dt2);
            }
            x = emit(is, X64_OP_JMP, X64_Q);
            x->target = bjoin;
            is->cur = bjoin - 1;
            is->vals[in->result.v].vr = d;
            break;
        }
        {
            /* i8/i16 sign/zero-extend to 32; u32 rides the free zext
             * and converts as i64 (exact). */
            X64VReg sv = to_vreg(is, &in->ops[0]);
            X64VReg wide = sv;
            X64VReg d = newvf(is);
            u8 srcw = X64_L;
            X64Inst *x;

            if (st == IRT_I8 || st == IRT_I16) {
                wide = newv(is);
                x = emit(is, uns ? X64_OP_MOVZX : X64_OP_MOVSX, X64_L);
                x->src_width = (u8)width_of((IrType)st);
                x->def = wide;
                x->a = ovreg(sv);
            } else if (st == IRT_I32 && uns) {
                wide = newv(is);
                x = emit(is, X64_OP_MOV, X64_L); /* free zext */
                x->def = wide;
                x->a = ovreg(sv);
                srcw = X64_Q;
            } else if (st == IRT_I64) {
                srcw = X64_Q;
            }
            x = emit(is, X64_OP_CVTI2F, fpw(dt));
            x->src_width = srcw;
            x->def = d;
            x->a = ovreg(wide);
            is->vals[in->result.v].vr = d;
        }
        break;
    }
    case IR_STACKSAVE: {
        /* A marker: regalloc rewrites it to `mov def, rsp` once operands
         * are physical (rsp has no pre-RA spelling on purpose). */
        X64VReg d = newv(is);
        X64Inst *x = emit(is, X64_OP_STACKSAVE, X64_Q);

        x->def = d;
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_STACKRESTORE: {
        X64VReg s = to_vreg(is, &in->ops[0]);
        X64Inst *x = emit(is, X64_OP_STACKRESTORE, X64_Q);

        x->a = ovreg(s);
        break;
    }
    case IR_ASM: {
        /* Operands become ONE instruction plus the moves around it:
         *
         *   <materialize each input into a vreg>
         *   X64_OP_ASM   def = the primary register output (may be 0)
         *                xuses = every operand that needs a register, in
         *                        IrAsm operand order, skipping that output
         *   <READREG-capture every extra fixed register output>
         *   <store all output vregs through their addresses>
         *
         * EARLY CLOBBER NEEDS NO CODE HERE, but it is NOT free the way an
         * earlier version of this comment said. cg_intervals_build extends
         * a def and a use at the same instruction point to that SAME point,
         * and intervals are hole-free and inclusive, so an output cannot
         * share a register with an input -- WHILE BOTH HOLD REGISTERS. In
         * the spill path that reasoning does not apply at all: reloads go
         * through a two-register scratch set, and three `r` operands all
         * arrived in %r11. What makes `&` implied is regalloc marking every
         * asm operand interval CgInterval.no_spill, so no asm operand ever
         * reaches that path. Not sharing is always safe -- gcc merely shares
         * when it may -- so this compiler still cannot demonstrate `&`
         * mattering, and the doc says so instead of claiming a feature.
         *
         * A TIED operand reserves its output's location. The primary
         * unfixed output can use the same vreg; fixed outputs use a fresh
         * pre-coloured input vreg, and extra fixed outputs are captured with
         * READREG immediately after the asm. */
        const IrAsm *a = in->callee && in->callee <= is->m->nasms
                             ? &is->m->asms[in->callee - 1]
                             : NULL;
        X64VReg opreg[64];
        X64VReg outval[64];
        X64VReg clobval[64];
        X64Inst *x;
        X64VReg outv = {0};
        u32 outidx = (u32)-1;
        u32 x87_outidx = (u32)-1;
        bool has_x87_up = false;
        bool x87_input_only = false;
        u32 k;

        if (!a) {
            x = emit(is, X64_OP_ASM, X64_Q);
            x->table = in->callee;
            break;
        }
        memset(opreg, 0, sizeof(opreg));
        memset(outval, 0, sizeof(outval));
        memset(clobval, 0, sizeof(clobval));
        /* Pass 1: the primary register output, so a tied input can target
         * the instruction's one real def. Lowering permits later register
         * outputs only when they are fixed and have one matching input. */
        for (k = 0; k < a->nops && k < 64; k++)
            if (a->ops[k].is_output && a->ops[k].cls != ASM_CLS_MEM) {
                if (a->ops[k].cls == ASM_CLS_X87) {
                    x87_outidx = k;
                    continue;
                } else {
                    outv =
                        a->ops[k].cls == ASM_CLS_FPREG ? newvf(is) : newv(is);
                    opreg[k] = outv;
                    outidx = k;
                }
                break;
            }
        /* Pass 2: everything that has to be somewhere before the asm. */
        for (k = 0; k < a->nops && k < 64; k++) {
            const IrAsmOp *o = &a->ops[k];

            if (o->cls == ASM_CLS_IMM)
                continue; /* no register: printed as $N */
            if (o->cls == ASM_CLS_X87 || o->cls == ASM_CLS_X87UP) {
                if (!o->is_output) {
                    X64VReg src = f80_addr(is, &in->ops[k]);

                    x87_mem(is, X64_OP_X87_FLD, X64_T, src, 0);
                    if (o->cls == ASM_CLS_X87UP)
                        has_x87_up = true;
                    else if (x87_outidx == (u32)-1)
                        x87_input_only = true;
                }
                continue; /* st(0), never an allocator-visible vreg */
            }
            if (o->is_output && o->cls != ASM_CLS_MEM)
                continue; /* handled above; the asm defines it */
            if (o->tied_to >= 0 && (u32)o->tied_to < a->nops &&
                a->ops[o->tied_to].is_output &&
                a->ops[o->tied_to].cls != ASM_CLS_MEM) {
                /* A TIED operand is two operands in ONE location, and how to
                 * say that depends on whether the location is NAMED.
                 *
                 * Fixed (`"=a"` + `"0"`, musl's syscall): a SEPARATE vreg
                 * pinned to the same register. It dies exactly where the
                 * output is defined, which is the endpoint sharing the
                 * pre-color repair explicitly permits -- the idiv handoff --
                 * so the two never conflict.
                 *
                 * Unfixed (`"+r"`): the SAME vreg, because there is no
                 * register to name and no fixed colour for the repair to
                 * split. Reusing the same vreg in the FIXED case is what the
                 * first draft did, and the repair then localized the def
                 * through a tiny vreg while the input's move still targeted
                 * the original -- the tie silently broke, the syscall read a
                 * stale rax, and write(2) returned -ENOSYS at -O0 while -O2
                 * happened to allocate its way out of it. */
                u32 tied = (u32)o->tied_to;
                const IrAsmOp *out = &a->ops[tied];
                X64VReg dst;
                X64VReg src;
                X64Inst *mv;

                /* THE OPERAND-ORDERING LAW: to_vreg EMITS the instructions
                 * that materialize a constant, so it must run BEFORE the
                 * consumer is appended. Calling it inside the assignment to
                 * mv->a puts the materialization AFTER the move that reads
                 * it -- which is how `movq $1, %rdx` ended up following
                 * `movq %rdx, %rax` and the syscall read garbage. Fourth
                 * appearance of this bug class in the backend. */
                src = out->cls == ASM_CLS_FPREG ? to_fvreg(is, &in->ops[k])
                                                : to_vreg(is, &in->ops[k]);
                if (tied == outidx && out->cls != ASM_CLS_FIXED) {
                    dst = outv;
                } else {
                    /* Every fixed tie gets a FRESH vreg. Pinning src would
                     * pin the IR value globally; for an extra output this
                     * vreg is also what reserves the physical register at
                     * the atomic ASM point until READREG captures it. */
                    dst = out->cls == ASM_CLS_FPREG ? newvf(is) : newv(is);
                }
                mv = emit(is,
                          out->cls == ASM_CLS_FPREG ? X64_OP_FMOV : X64_OP_MOV,
                          asm_width(o->size));
                mv->def = dst;
                mv->a = ovreg(src);
                if (out->cls == ASM_CLS_FIXED)
                    mv->def_fixed = (u8)(out->reg + 1);
                opreg[k] = dst;
                continue;
            }
            /* An input, or a memory operand's ADDRESS: both are just a
             * register the template will name.
             *
             * A FIXED input gets a FRESH vreg with the pin, never the IR
             * value's own. Pinning the value's vreg pins it EVERYWHERE it is
             * used, so one value feeding two differently-pinned operands --
             * ordinary once mem2reg has run -- makes the allocator ICE with
             * "constrained to two registers". Copying first is what the call
             * path does with its argument registers, for the same reason. */
            if (o->cls == ASM_CLS_FIXED) {
                X64VReg src = to_vreg(is, &in->ops[k]); /* BEFORE emit */
                X64Inst *mv = emit(is, X64_OP_MOV, asm_width(o->size));

                opreg[k] = newv(is);
                mv->def = opreg[k];
                mv->def_fixed = (u8)(o->reg + 1);
                mv->a = ovreg(src);
                continue;
            }
            {
                X64VReg src = o->cls == ASM_CLS_FPREG
                                  ? to_fvreg(is, &in->ops[k])
                                  : to_vreg(is, &in->ops[k]);
                X64VReg local = o->cls == ASM_CLS_FPREG ? newvf(is) : newv(is);
                X64Inst *mv =
                    emit(is, o->cls == ASM_CLS_FPREG ? X64_OP_FMOV : X64_OP_MOV,
                         o->cls == ASM_CLS_MEM ? X64_Q : asm_width(o->size));

                /* Register-required asm operands are unspillable because the
                 * template names every one simultaneously and the generic
                 * reload path has only two scratches.  Never put that policy
                 * on the source value's whole interval: an address reused
                 * long after this asm can cross calls and exhaust the five
                 * callee-saved registers (musl pthread_barrier_wait did).
                 * Localize the requirement through a one-site copy, exactly
                 * as fixed inputs above are localized.  Tied operands already
                 * use their output's fresh vreg and do not reach this path. */
                mv->def = local;
                mv->a = ovreg(src);
                opreg[k] = local;
            }
        }

        /* A named clobber must exclude its EXACT register, including the
         * callee-saved set. The blanket call-point model below excludes all
         * caller-saved registers from live-through values, but by design it
         * attracts those values to rbx/r12-r15. A tiny READREG -> ASM xuse
         * sentinel reserves the named physical register at this instruction;
         * its value is irrelevant and the marker normally rewrites away. */
        for (k = 0; k < a->nclobber_regs && k < 64; k++) {
            X64Inst *rr;

            clobval[k] = newv(is);
            rr = emit(is, X64_OP_READREG, X64_Q);
            rr->def = clobval[k];
            rr->def_fixed = (u8)(a->clobber_regs[k] + 1);
        }

        x = emit(is, X64_OP_ASM, X64_Q);
        x->table = in->callee;
        if (outv.v) {
            x->def = outv;
            if (a->ops[outidx].cls == ASM_CLS_FIXED)
                x->def_fixed = (u8)(a->ops[outidx].reg + 1);
        }
        for (k = 0; k < a->nops && k < 64; k++) {
            const IrAsmOp *o = &a->ops[k];
            u8 fixed;

            if (!opreg[k].v || k == outidx)
                continue;
            fixed = o->cls == ASM_CLS_FIXED ? (u8)(o->reg + 1) : 0;
            /* A tied input inherits its OUTPUT's register, not its own
             * constraint letter: `"0"` names operand 0, and the location is
             * whatever operand 0 got. */
            if (o->tied_to >= 0 && (u32)o->tied_to < a->nops &&
                a->ops[o->tied_to].cls == ASM_CLS_FIXED)
                fixed = (u8)(a->ops[o->tied_to].reg + 1);
            x64_add_xuse(is->xf, x, opreg[k], fixed);
        }
        for (k = 0; k < a->nclobber_regs && k < 64; k++)
            x64_add_xuse(is->xf, x, clobval[k], (u8)(a->clobber_regs[k] + 1));
        /* Keep the conservative CALL-CLOBBER model as well: a live-through
         * value avoids every caller-saved register, while the exact sentinels
         * above cover named callee-saved registers too. */
        if (a->nclobber_regs)
            x->flags |= X64IF_ASM_CLOBBERS;
        if (x87_input_only)
            x->flags |= X64IF_ASM_X87_POP;
        if (outv.v)
            outval[outidx] = outv;
        /* Capture EVERY extra output before even computing a store address.
         * Until READREG creates a live value, only the matching input keeps
         * the physical register occupied at the ASM point; address folding
         * inserted here could otherwise reuse and overwrite (for example)
         * rcx before its output was observed. */
        for (k = 0; k < a->noutputs && k < a->nops && k < 64; k++) {
            const IrAsmOp *o = &a->ops[k];
            X64Inst *rr;

            if (!o->is_output || o->cls == ASM_CLS_MEM ||
                o->cls == ASM_CLS_X87 || o->cls == ASM_CLS_X87UP || k == outidx)
                continue;
            if (o->cls != ASM_CLS_FIXED)
                CGF_ICE("x86_64 isel: extra asm output is not fixed");
            outval[k] = newv(is);
            rr = emit(is, X64_OP_READREG, asm_width(o->size));
            rr->def = outval[k];
            rr->def_fixed = (u8)(o->reg + 1);
        }
        /* Register outputs go back to the C objects their constraints
         * named. Their ADDRESSES are the IR operands -- see ir.h. Memory
         * outputs were written by the template itself and need no store. */
        for (k = 0; k < a->noutputs && k < a->nops && k < 64; k++) {
            X64Inst *st;

            if (k == x87_outidx) {
                X64Mem mem = fold_addr(is, &in->ops[k]);

                st = emit(is, X64_OP_X87_FSTP, X64_T);
                st->a.kind = X64O_MEM;
                st->a.mem = mem;
                continue;
            }
            if (!outval[k].v)
                continue;
            st = emit(is,
                      a->ops[k].cls == ASM_CLS_FPREG ? X64_OP_FSTORE
                                                     : X64_OP_STORE,
                      asm_width(a->ops[k].size));
            st->a = ovreg(outval[k]);
            st->b.kind = X64O_MEM;
            st->b.mem = fold_addr(is, &in->ops[k]);
        }
        if (has_x87_up)
            x87_op0(is, X64_OP_X87_FPOP);
        break;
    }
    case IR_VA_START: {
        /* Marker: only frame-finalize knows where the register save
         * area and the first unnamed stack argument live. */
        X64VReg ap = to_vreg(is, &in->ops[0]);
        X64Inst *x = emit(is, X64_OP_VASTART, X64_Q);

        x->a = ovreg(ap);
        break;
    }
    case IR_MEMCPY: {
        /* Constant sizes inline as word copies (aggregate assignment,
         * sret temps — Sprint 23 calls need them); dynamic sizes wait
         * for Sprint 24's rep movs. */
        X64VReg dst, src;
        u64 sz;
        u32 c = 0;
        X64Inst *x;

        if (in->ops[2].kind != IROP_ICONST)
            CGF_ICE("x86_64 isel: memcpy with a non-constant size (the C front "
                    "end always emits a constant one; a dynamic size "
                    "belongs in a libc call)");
        dst = to_vreg(is, &in->ops[0]);
        src = to_vreg(is, &in->ops[1]);
        sz = in->ops[2].a;
        while (c < sz) {
            u32 step = sz - c >= 8 ? 8 : sz - c >= 4 ? 4 : sz - c >= 2 ? 2 : 1;
            X64VReg tv = newv(is);

            x = emit(is, X64_OP_LOAD, (X64Width)step);
            x->def = tv;
            x->a.kind = X64O_MEM;
            x->a.mem.base = src;
            x->a.mem.scale = 1;
            x->a.mem.disp = (i32)c;
            x = emit(is, X64_OP_STORE, (X64Width)step);
            x->a = ovreg(tv);
            x->b.kind = X64O_MEM;
            x->b.mem.base = dst;
            x->b.mem.scale = 1;
            x->b.mem.disp = (i32)c;
            c += step;
        }
        break;
    }
    case IR_MEMSET: {
        X64VReg dst;
        u64 sz, byte, pat;
        u32 c = 0;
        X64Inst *x;

        if (in->ops[2].kind != IROP_ICONST || in->ops[1].kind != IROP_ICONST)
            CGF_ICE(
                "x86_64 isel: memset with a non-constant size or byte (the C "
                "front end always emits constants; a dynamic size "
                "belongs in a libc call)");
        dst = to_vreg(is, &in->ops[0]);
        byte = in->ops[1].a & 0xff;
        pat = byte * 0x0101010101010101ull;
        sz = in->ops[2].a;
        while (c < sz) {
            u32 step = sz - c >= 8 ? 8 : sz - c >= 4 ? 4 : sz - c >= 2 ? 2 : 1;

            /* an 8-byte pattern store needs simm32: all-zero and
             * all-one bytes fit; anything else goes through a vreg */
            if (step == 8 && !x64_imm_fits_simm32((i64)pat)) {
                X64VReg tv = newv(is);

                x = emit(is, X64_OP_MOVABS, X64_Q);
                x->def = tv;
                x->a = oimm((i64)pat);
                x = emit(is, X64_OP_STORE, X64_Q);
                x->a = ovreg(tv);
            } else {
                x = emit(is, X64_OP_STORE, (X64Width)step);
                x->a = oimm((i64)sim_pattern_narrow(pat, step));
            }
            x->b.kind = X64O_MEM;
            x->b.mem.base = dst;
            x->b.mem.scale = 1;
            x->b.mem.disp = (i32)c;
            c += step;
        }
        break;
    }
    case IR_ATOMICRMW: {
        /* Every form returns the OLD value. `add` and `xchg` have direct
         * instructions for that; `sub` is add of the negation; and/or/xor
         * have no such form and go through the compare-exchange below.
         *
         * Unlike the arm64 ll/sc sequence, a retry loop here is ordinary
         * code: x86 has no exclusive monitor to lose, so spill code inside
         * it is merely slow and the loop is built from real blocks. */
        X64Width w = width_of((IrType)in->type);
        X64Mem mem = fold_addr(is, &in->ops[0]);
        X64VReg val = to_vreg(is, &in->ops[1]);
        X64VReg out = newv(is);
        X64Inst *x;

        if (in->subop == RMW_XCHG) {
            /* xchg against memory is atomic with no prefix — the one form
             * the architecture locks implicitly. */
            x = emit(is, X64_OP_MOV, w);
            x->def = out;
            x->a = ovreg(val);
            x = emit(is, X64_OP_XCHG, w);
            x->def = out;
            x->flags |= X64IF_TWO_ADDR;
            x->a = ovreg(out);
            x->b.kind = X64O_MEM;
            x->b.mem = mem;
            is->vals[in->result.v].vr = out;
            break;
        }
        if (in->subop == RMW_ADD || in->subop == RMW_SUB) {
            x = emit(is, X64_OP_MOV, w);
            x->def = out;
            x->a = ovreg(val);
            if (in->subop == RMW_SUB) {
                x = emit(is, X64_OP_NEG, w);
                x->def = out;
                x->flags |= X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
                x->a = ovreg(out);
            }
            x = emit(is, X64_OP_XADD, w);
            x->def = out;
            x->flags |= X64IF_TWO_ADDR | X64IF_DEFS_FLAGS | X64IF_LOCK;
            x->a = ovreg(out);
            x->b.kind = X64O_MEM;
            x->b.mem = mem;
            is->vals[in->result.v].vr = out;
            break;
        }
        {
            u32 loop = new_block(is, "atomic.loop");
            u32 done = new_block(is, "atomic.done");
            X64VReg next = newv(is);
            IrOp alu = in->subop == RMW_AND  ? IR_AND
                       : in->subop == RMW_OR ? IR_OR
                                             : IR_XOR;

            /* Seed rax with the current value, then retry until the
             * compare-exchange reports success. */
            x = emit(is, X64_OP_LOAD, w);
            x->def = out;
            x->def_fixed = X64_RAX + 1;
            x->a.kind = X64O_MEM;
            x->a.mem = mem;
            x = emit(is, X64_OP_JMP, X64_Q);
            x->target = loop;

            is->cur = loop - 1;
            x = emit(is, X64_OP_MOV, w);
            x->def = next;
            x->a = ovreg(out);
            x->a.fixed = X64_RAX + 1;
            x = emit(is, alu_op(alu), w);
            x->def = next;
            x->flags |= X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
            x->a = ovreg(next);
            x->b = ovreg(val);
            /* cmpxchg compares rax against memory: on a match it stores the
             * source and sets ZF; otherwise it loads memory INTO rax, which
             * is what makes the retry converge without re-reading. */
            x = emit(is, X64_OP_CMPXCHG, w);
            x->def = out;
            x->def_fixed = X64_RAX + 1;
            x->flags |= X64IF_DEFS_FLAGS | X64IF_LOCK;
            x->a = ovreg(next);
            x->b.kind = X64O_MEM;
            x->b.mem = mem;
            x64_add_xuse(is->xf, x, out, X64_RAX + 1);
            x = emit(is, X64_OP_JCC, X64_Q);
            x->cc = X64_CC_NE;
            x->flags |= X64IF_USES_FLAGS;
            x->flags_src = blk(is)->n - 2;
            x->target = loop;
            x->target2 = done;

            is->cur = done - 1;
            is->vals[in->result.v].vr = out;
        }
        break;
    }
    case IR_CMPXCHG: {
        X64Width w = width_of((IrType)in->type);
        X64Mem mem = fold_addr(is, &in->ops[0]);
        X64VReg expected = to_vreg(is, &in->ops[1]);
        X64VReg desired = to_vreg(is, &in->ops[2]);
        X64VReg out = newv(is);
        X64Inst *x;

        /* rax carries the expected value in and the OLD value out. The IR
         * contract is single-result: success is `icmp eq old, expected`,
         * which the caller computes, so ZF is not consumed here. */
        x = emit(is, X64_OP_MOV, w);
        x->def = out;
        x->def_fixed = X64_RAX + 1;
        x->a = ovreg(expected);
        x = emit(is, X64_OP_CMPXCHG, w);
        x->def = out;
        x->def_fixed = X64_RAX + 1;
        x->flags |= X64IF_DEFS_FLAGS | X64IF_LOCK;
        x->a = ovreg(desired);
        x->b.kind = X64O_MEM;
        x->b.mem = mem;
        x64_add_xuse(is->xf, x, out, X64_RAX + 1);
        is->vals[in->result.v].vr = out;
        break;
    }
    default:
        CGF_ICE("x86_64 isel: unhandled IR op %u", in->op);
    }
}

X64Func *x64_isel_function(const IrModule *m, const IrFunc *f, Arena *a,
                           X64PicLevel pic)
{
    Isel is;
    X64Func *xf = arena_alloc(a, sizeof(X64Func), _Alignof(X64Func));
    u32 bi, i;

    if (ir_type_is_vector((IrType)f->ret))
        CGF_ICE("x86_64 isel: vector function return has no Sprint 36 ABI");
    for (i = 0; i < f->nparams; i++)
        if (ir_type_is_vector((IrType)f->param_types[i]))
            CGF_ICE(
                "x86_64 isel: vector function parameter has no Sprint 36 ABI");

    memset(xf, 0, sizeof(*xf));
    xf->name = f->name;
    xf->arena = a;
    xf->m = m;
    memset(&is, 0, sizeof(is));
    is.arena = a;
    is.m = m;
    is.f = f;
    is.xf = xf;
    is.pic = pic;
    is.vals =
        arena_alloc(a, (f->nvals + 1) * sizeof(ValInfo), _Alignof(ValInfo));
    memset(is.vals, 0, (f->nvals + 1) * sizeof(ValInfo));
    is.addr_plan =
        arena_alloc(a, (f->nvals + 1) * sizeof(AddrPlan), _Alignof(AddrPlan));
    memset(is.addr_plan, 0, (f->nvals + 1) * sizeof(AddrPlan));
    is.use_count = arena_alloc(a, (f->nvals + 1) * sizeof(u32), _Alignof(u32));
    memset(is.use_count, 0, (f->nvals + 1) * sizeof(u32));

    xf->variadic = f->variadic;
    xf->ret_f80 = f->ret == IRT_F80;

    /* Pre-pass: MIR block per IR block; vregs for every param (class by
     * IR type: f32/f64 are xmm; f80 values are ADDRESSES, hence GP);
     * USE counts (branch fusion needs them). */
    for (bi = 0; bi < f->nblocks; bi++)
        new_block(&is, f->blocks[bi].name);
    for (i = 0; i < f->nparams; i++)
        is.vals[f->param_vals[i].v].vr =
            irt_xmm16(f->param_types[i]) ? newvv(&is)
            : irt_sse(f->param_types[i]) ? newvf(&is)
                                         : newv(&is);
    {
        const IrInst **defs = arena_alloc(a, (f->nvals + 1) * sizeof(IrInst *),
                                          _Alignof(IrInst *));
        bool *only_addr_use =
            arena_alloc(a, (f->nvals + 1) * sizeof(bool), _Alignof(bool));

        memset(defs, 0, (f->nvals + 1) * sizeof(IrInst *));
        memset(only_addr_use, 1, (f->nvals + 1) * sizeof(bool));
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrBlock *b = &f->blocks[bi];
            const IrInst *in;

            for (i = 0; i < b->nparams; i++) {
                u8 pt = f->vals[b->params[i].v - 1].type;

                if (pt == IRT_F80)
                    CGF_ICE("x86_64 isel: f80 block parameters violate the "
                            "memory law (lowering never emits them)");
                is.vals[b->params[i].v].vr = irt_xmm16(pt) ? newvv(&is)
                                             : irt_sse(pt) ? newvf(&is)
                                                           : newv(&is);
            }
            for (in = b->first; in; in = in->next) {
                u32 k, j;

                if (in->result.v)
                    defs[in->result.v] = in;
                for (k = 0; k < in->nops; k++) {
                    if (in->ops[k].kind == IROP_VALUE) {
                        u32 v = (u32)in->ops[k].a;

                        is.use_count[v]++;
                        if (!is_foldable_addr_use(in, k))
                            only_addr_use[v] = false;
                    }
                }
                for (k = 0; k < in->nedges; k++)
                    for (j = 0; j < in->edges[k].nargs; j++)
                        if (in->edges[k].args[j].kind == IROP_VALUE) {
                            u32 v = (u32)in->edges[k].args[j].a;

                            is.use_count[v]++;
                            only_addr_use[v] = false;
                        }
            }
        }
        plan_addresses(&is, defs, only_addr_use);
    }

    /* Callee-side parameter binding (Sprint 23): the same queue walk as
     * the caller, into the ENTRY block ahead of the body. Register args
     * arrive via READREG (rewrite copies or elides); stack args load or
     * take their address at [rbp+16+off]. A PAIR-returning function's
     * hidden pointer was never passed — it binds to a fresh local slot
     * the ret sequence reads back. */
    {
        static const u8 gpq[6] = {X64_RDI, X64_RSI, X64_RDX,
                                  X64_RCX, X64_R8,  X64_R9};
        u32 gp = 0, fpq = 0, stack_off = 0;
        bool pair_hidden =
            f->abi_ret != IR_ABIRET_NONE && f->abi_ret != IR_ABIRET_SRET;
        X64Inst *x;

        is.cur = 0;
        for (i = 0; i < f->nparams; i++) {
            u8 pt = f->param_types[i];
            X64VReg pv = is.vals[f->param_vals[i].v].vr;
            u64 ann = f->param_annots ? f->param_annots[i] : 0;

            if (i == 0 && pair_hidden) {
                x = emit(&is, X64_OP_LEA, X64_Q);
                x->def = pv;
                x->a.kind = X64O_MEM; /* frame marker */
                x->b.imm = 16;
                x->table = 16;
                continue;
            }
            if (ir_arg_kind(ann) == IR_ARG_BYVAL) {
                u32 sz = (ir_arg_size(ann) + 7) & ~7u;

                x = emit(&is, X64_OP_ARGLEA, X64_Q);
                x->def = pv;
                x->b.imm = (i64)stack_off;
                stack_off += sz;
                continue;
            }
            if (pt == IRT_F80) {
                stack_off = (stack_off + 15) & ~15u;
                x = emit(&is, X64_OP_ARGLEA, X64_Q);
                x->def = pv;
                x->b.imm = (i64)stack_off;
                stack_off += 16;
                continue;
            }
            if (pt == IRT_F128) {
                /* The mirror of the caller's SSE+SSEUP placement: a whole
                 * xmm, or 16 bytes of 16-aligned stack once the eight
                 * registers are gone. Reading it at X64_X is what carries
                 * the upper eightbyte -- fpw() would take 8 bytes and lose
                 * half the value with nothing to show for it. */
                if (fpq < 8) {
                    x = emit(&is, X64_OP_READREG, X64_X);
                    x->def = pv;
                    x->def_fixed = (u8)(X64_XMM0 + fpq++ + 1);
                } else {
                    stack_off = (stack_off + 15) & ~15u;
                    x = emit(&is, X64_OP_ARGLD, X64_X);
                    x->def = pv;
                    x->b.imm = (i64)stack_off;
                    stack_off += 16;
                }
                continue;
            }
            if (irt_sse(pt)) {
                if (fpq < 8) {
                    x = emit(&is, X64_OP_READREG, fpw(pt));
                    x->def = pv;
                    x->def_fixed = (u8)(X64_XMM0 + fpq++ + 1);
                } else {
                    x = emit(&is, X64_OP_ARGLD, fpw(pt));
                    x->def = pv;
                    x->b.imm = (i64)stack_off;
                    stack_off += 8;
                }
                continue;
            }
            if (gp < 6) {
                x = emit(&is, X64_OP_READREG, X64_Q);
                x->def = pv;
                x->def_fixed = (u8)(gpq[gp++] + 1);
            } else {
                x = emit(&is, X64_OP_ARGLD, X64_Q);
                x->def = pv;
                x->b.imm = (i64)stack_off;
                stack_off += 8;
            }
        }
        xf->named_stack_bytes = stack_off;
    }

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        is.cur = bi;
        is.last_flags_inst = 0;
        is.last_flags_val = 0;
        for (in = f->blocks[bi].first; in; in = in->next) {
            X64VReg forward = {0};

            if (in->result.v)
                forward = is.vals[in->result.v].vr;
            is.cur_loc = in->loc;
            sel_inst(&is, in, &f->blocks[bi]);
            if (forward.v) {
                X64VReg actual = is.vals[in->result.v].vr;

                if (!actual.v)
                    CGF_ICE("x86_64 isel: forward value %%%u was not "
                            "materialized",
                            in->result.v - 1);
                if (actual.v != forward.v) {
                    X64Inst *copy;

                    if (irt_xmm16(in->type)) {
                        copy = emit(&is, X64_OP_VMOV, X64_X);
                    } else if (irt_sse(in->type)) {
                        copy = emit(&is, X64_OP_FMOV, fpw(in->type));
                    } else {
                        X64Width w = in->type == IRT_F80
                                         ? X64_Q
                                         : width_of((IrType)in->type);

                        copy = emit(&is, X64_OP_MOV, w);
                    }
                    copy->def = forward;
                    copy->a = ovreg(actual);
                    is.vals[in->result.v].vr = forward;
                }
            }
        }
    }
    return xf;
}
