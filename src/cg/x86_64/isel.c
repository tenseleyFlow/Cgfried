#include "cg/x86_64/mir.h"

#include <string.h>

/* x86_64 instruction selection (Sprint 21): all INTEGER IR selects into
 * three-address MIR over vregs. FP and calls ICE naming Sprint 23; the
 * stack/atomic/vararg ops ICE naming Sprint 24 (they need frame and
 * emission context). Register allocation and the two-address fixup are
 * Sprint 22's; no text leaves the process until Sprint 24. */

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

typedef struct Isel {
    Arena *arena;
    const IrModule *m;
    const IrFunc *f;
    X64Func *xf;
    ValInfo *vals; /* [nvals+1] */
    u32 *use_count;
    u32 cur; /* current MIR block index (0-based) */
    /* flags tracking within the current block */
    u32 last_flags_inst; /* index+1 of last DEFS_FLAGS inst; 0 = none */
    u32 last_flags_val;  /* the icmp ValueId it computed for, or 0 */
} Isel;

static X64VReg newv(Isel *is)
{
    X64VReg r = {++is->xf->nvregs};

    return r;
}

static X64Block *blk(Isel *is)
{
    return &is->xf->blocks[is->cur];
}

static X64Inst *emit(Isel *is, X64Op op, X64Width w)
{
    X64Block *b = blk(is);
    X64Inst *in;

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
    case X64_OP_CMP:
    case X64_OP_TEST:
    case X64_OP_CQO:
    case X64_OP_IDIV:
    case X64_OP_DIV:
        in->flags = X64IF_DEFS_FLAGS;
        is->last_flags_inst = b->n;
        is->last_flags_val = 0;
        break;
    default:
        break; /* mov/lea/movzx/movsx/setcc/jmp/jcc leave flags alone */
    }
    return in;
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
        CGF_ICE("x86_64 isel: f32/f64/f80/f128 isel lands in Sprint 23");
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
        return is->vals[(u32)o->a].vr;
    case IROP_ICONST: {
        X64Width w = width_of((IrType)o->type);
        i64 v = (i64)o->a;
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
        X64Inst *in = emit(is, X64_OP_LEA, X64_Q);

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
        i64 v = (i64)o->a;

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
        mem.rip_sym = addr->sym + 1;
        mem.disp = (i32)(i64)addr->a;
        return mem;
    }
    if (addr->kind == IROP_VALUE) {
        const ValInfo *vi = &is->vals[(u32)addr->a];

        if (vi->pat == 2 && x64_fold_ok(1, false, vi->pat_disp)) {
            mem.base = vi->pat_base;
            mem.disp = (i32)vi->pat_disp;
            return mem;
        }
        mem.base = vi->vr;
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

static const char *const cc_names_[10] = {"e",  "ne", "l",  "le", "g",
                                          "ge", "b",  "be", "a",  "ae"};

const char *x64_cc_name(u8 cc)
{
    return cc < 10 ? cc_names_[cc] : "?";
}

/* --- block-parameter moves (parallel-copy semantics) ---------------------- */

typedef struct PMove {
    X64VReg dst;
    X64Operand src;
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
                X64Inst *in = emit(is, X64_OP_MOV, widths[i]);

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
                X64VReg scratch = newv(is);
                X64Inst *in = emit(is, X64_OP_MOV, widths[i]);

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
        mv[n].dst = is->vals[tb->params[i].v].vr;
        mv[n].src = to_src(is, &e->args[i], width_of((IrType)e->args[i].type));
        mv[n].done = false;
        widths[n] = width_of((IrType)e->args[i].type);
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
    switch (in->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_IMUL: {
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
        X64Inst *x = emit(is, alu_op((IrOp)in->op), w);

        x->def = d;
        x->a = ovreg(a0);
        if (in->ops[1].kind == IROP_ICONST) {
            x->b = oimm((i64)in->ops[1].a);
        } else {
            /* variable count: CL fixed-reg constraint (hardware masks
             * &63/&31; over-width shifts are C UB — matching gcc). */
            x->b = ovreg(to_vreg(is, &in->ops[1]));
            x->b.fixed = X64_RCX + 1;
        }
        is->vals[in->result.v].vr = d;
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
            x = emit(is, X64_OP_CMP, w);
            x->a = ovreg(a);
            x->b = to_src(is, &in->ops[1], w);
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
         * free. Float bitcasts land in Sprint 23. */
        if (in->op == IR_BITCAST &&
            (in->type == IRT_F32 || in->type == IRT_F64 ||
             in->ops[0].type == IRT_F32 || in->ops[0].type == IRT_F64))
            CGF_ICE("x86_64 isel: f32/f64/f80/f128 isel lands in Sprint 23");
        is->vals[in->result.v].vr = to_vreg(is, &in->ops[0]);
        break;
    }
    case IR_ALLOCA: {
        /* Frame layout is Sprint 22's; the MIR carries a lea off a
         * frame pseudo-symbol resolved there. For now: a fresh vreg
         * defined by LEA of a per-alloca slot symbol is deferred — keep
         * the alloca as an opaque LEA-from-frame marker. */
        X64VReg d = newv(is);
        X64Inst *x = emit(is, X64_OP_LEA, X64_Q);

        x->def = d;
        x->a.kind = X64O_MEM;
        x->a.mem.base.v = 0; /* frame-relative; Sprint 22 rewrites */
        x->a.mem.disp = 0;
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_LOAD: {
        X64Width w = width_of((IrType)in->type);
        X64VReg d = newv(is);
        X64Mem mem = fold_addr(is, &in->ops[0]);
        X64Inst *x = emit(is, X64_OP_LOAD, w);

        x->def = d;
        x->a.kind = X64O_MEM;
        x->a.mem = mem;
        is->vals[in->result.v].vr = d;
        break;
    }
    case IR_STORE: {
        X64Width w = width_of((IrType)in->ops[0].type);
        X64Operand v = to_src(is, &in->ops[0], w);
        X64Mem mem = fold_addr(is, &in->ops[1]);
        X64Inst *x = emit(is, X64_OP_STORE, w);

        x->a = v;
        x->b.kind = X64O_MEM;
        x->b.mem = mem;
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
            x->a.mem.index = is->vals[(u32)in->ops[1].a].vr;
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
         * cmovcc arm yet — Sprint 24 files the upstream extension. */
        X64Width w = width_of((IrType)in->type);
        X64VReg d = newv(is);
        u32 tb = new_block(is, "sel.t");
        u32 jb = new_block(is, "sel.j");
        X64VReg c = to_vreg(is, &in->ops[0]);
        X64Inst *x;

        {
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
        {
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
        if (in->nops) {
            X64Width w = width_of((IrType)in->ops[0].type);
            X64VReg r = newv(is);
            X64Operand rv = to_src(is, &in->ops[0], w);
            X64Inst *x = emit(is, X64_OP_MOV, w);

            x->def = r;
            x->def_fixed = X64_RAX + 1;
            x->a = rv;
        }
        emit(is, X64_OP_RET, X64_Q);
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
                /* compare tree (linear form of the balanced tree: the
                 * balance matters at Sprint 53's tuning, correctness
                 * here) — insertion order for determinism. */
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
    case IR_CALL:
        CGF_ICE("x86_64 isel: call isel lands in Sprint 23");
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
    case IR_FNEG:
    case IR_FCMP:
    case IR_FPEXT:
    case IR_FPTRUNC:
    case IR_FPTOSI:
    case IR_FPTOUI:
    case IR_SITOFP:
    case IR_UITOFP:
        CGF_ICE("x86_64 isel: f32/f64/f80/f128 isel lands in Sprint 23");
    case IR_VA_START:
    case IR_STACKSAVE:
    case IR_STACKRESTORE:
    case IR_ATOMICRMW:
    case IR_CMPXCHG:
    case IR_MEMCPY:
    case IR_MEMSET:
        CGF_ICE("x86_64 isel: '%s' isel lands in Sprint 24",
                ir_op_name((IrOp)in->op));
    default:
        CGF_ICE("x86_64 isel: unhandled IR op %u", in->op);
    }
}

X64Func *x64_isel_function(const IrModule *m, const IrFunc *f, Arena *a)
{
    Isel is;
    X64Func *xf = arena_alloc(a, sizeof(X64Func), _Alignof(X64Func));
    u32 bi, i;

    memset(xf, 0, sizeof(*xf));
    xf->name = f->name;
    xf->arena = a;
    xf->m = m;
    memset(&is, 0, sizeof(is));
    is.arena = a;
    is.m = m;
    is.f = f;
    is.xf = xf;
    is.vals =
        arena_alloc(a, (f->nvals + 1) * sizeof(ValInfo), _Alignof(ValInfo));
    memset(is.vals, 0, (f->nvals + 1) * sizeof(ValInfo));
    is.use_count = arena_alloc(a, (f->nvals + 1) * sizeof(u32), _Alignof(u32));
    memset(is.use_count, 0, (f->nvals + 1) * sizeof(u32));

    /* Pre-pass: MIR block per IR block; vregs for every param; USE
     * counts (branch fusion needs them). */
    for (bi = 0; bi < f->nblocks; bi++)
        new_block(&is, f->blocks[bi].name);
    for (i = 0; i < f->nparams; i++)
        is.vals[f->param_vals[i].v].vr = newv(&is);
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrBlock *b = &f->blocks[bi];
        const IrInst *in;

        for (i = 0; i < b->nparams; i++)
            is.vals[b->params[i].v].vr = newv(&is);
        for (in = b->first; in; in = in->next) {
            u32 k, j;

            for (k = 0; k < in->nops; k++)
                if (in->ops[k].kind == IROP_VALUE)
                    is.use_count[(u32)in->ops[k].a]++;
            for (k = 0; k < in->nedges; k++)
                for (j = 0; j < in->edges[k].nargs; j++)
                    if (in->edges[k].args[j].kind == IROP_VALUE)
                        is.use_count[(u32)in->edges[k].args[j].a]++;
        }
    }

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        is.cur = bi;
        is.last_flags_inst = 0;
        is.last_flags_val = 0;
        for (in = f->blocks[bi].first; in; in = in->next)
            sel_inst(&is, in, &f->blocks[bi]);
    }
    return xf;
}
