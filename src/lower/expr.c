#include "lower/lower.h"

#include <stdio.h>
#include <string.h>

const CgfAttr *lower_clone_cgf_attrs(Lower *lo, const CgfAttr *attrs,
                                     const u32 *ir_args, u32 nargs);

/* Expression lowering. The tree arrives fully typed with every implicit
 * conversion materialized (Sprint 13), so this file is a TRANSLATOR, not
 * a rule engine: the only type computations it performs are the ones C
 * defines on lowered FORM rather than on the tree — pointer-arithmetic
 * scaling, the compound-assignment round-trip through the UAC type, and
 * the bitfield read/modify/write sequences.
 *
 * Operand evaluation is STRICT LEFT-TO-RIGHT everywhere (the lower.h
 * law): lhs before rhs, callee before arguments, arguments in source
 * order, assignment ADDRESS before assigned value. */

static Type *sem(AstNode *e)
{
    return e->sem_type;
}

static bool type_is_fp(const Type *t)
{
    return t && (t->kind == TY_FLOAT || t->kind == TY_DOUBLE ||
                 t->kind == TY_LDOUBLE);
}

static bool is_signed_ty(Lower *lo, Type *t)
{
    return conv_is_signed(lo->sema, t);
}

static ValueId build_source_arith(Lower *lo, IrOp op, IrType irty, IrOperand x,
                                  IrOperand y, Type *source_ty)
{
    bool nsw = (op == IR_IADD || op == IR_ISUB || op == IR_IMUL) &&
               type_is_integer(source_ty) && is_signed_ty(lo, source_ty) &&
               !lo->sema->lang->fwrapv;

    return ir_build2_flags(&lo->b, op, irty, x, y, nsw ? IRF_NSW : 0);
}

static IrOperand fp_zero(Lower *lo, Type *t)
{
    return ir_op_fconst(lower_irtype(lo, t), 0, 0);
}

/* `v != 0` for a scalar of C type t (i32 result). The fp compare is une:
 * NaN is truthy. */
static IrOperand truth_ne(Lower *lo, IrOperand v, Type *t)
{
    ValueId r;

    if (type_is_fp(t))
        r = ir_build_fcmp(&lo->b, FCMP_UNE, v, fp_zero(lo, t));
    else
        r = ir_build_icmp(&lo->b, ICMP_NE, v, ir_op_iconst((IrType)v.type, 0));
    return ir_op_value(lo->fn, r);
}

/* --- loads and stores ----------------------------------------------------- */

static u8 lv_flags(const Lvalue *lv)
{
    return (u8)((lv->is_volatile ? IRF_VOLATILE : 0) |
                (lv->is_atomic ? IRF_SEQ_CST : 0));
}

IrOperand lower_load(Lower *lo, Lvalue lv)
{
    ValueId raw;

    raw = ir_build_load_typed(&lo->b, lv.unit, lv.addr, lv.align, lv_flags(&lv),
                              lv.etype);
    if (!lv.is_bitfield)
        return ir_op_value(lo->fn, raw);
    {
        /* shl to put the field's top bit at the unit's top, then a right
         * shift back down: ashr sign-extends signed fields for free,
         * lshr zero-fills unsigned ones. */
        u32 unit_bits = 8u << (lv.unit - IRT_I8);
        u32 up = unit_bits - lv.bit_shift - lv.bit_width;
        u32 down = unit_bits - lv.bit_width;
        ValueId hi =
            ir_build2(&lo->b, IR_SHL, lv.unit, ir_op_value(lo->fn, raw),
                      ir_op_iconst(lv.unit, (i64)up));
        ValueId out = ir_build2(&lo->b, lv.is_signed ? IR_ASHR : IR_LSHR,
                                lv.unit, ir_op_value(lo->fn, hi),
                                ir_op_iconst(lv.unit, (i64)down));

        return ir_op_value(lo->fn, out);
    }
}

IrOperand lower_store(Lower *lo, Lvalue lv, IrOperand v)
{
    if (!lv.is_bitfield) {
        ir_build_store_typed(&lo->b, v, lv.addr, lv.align, lv_flags(&lv),
                             lv.etype);
        return v;
    }
    {
        /* Read-modify-write; and the RESULT of the assignment is the
         * re-narrowed stored value, not the incoming RHS. */
        u32 unit_bits = 8u << (lv.unit - IRT_I8);
        u64 mask = lv.bit_width >= 64 ? ~0ull : ((1ull << lv.bit_width) - 1);
        ValueId old = ir_build_load_typed(&lo->b, lv.unit, lv.addr, lv.align,
                                          lv_flags(&lv), lv.etype);
        ValueId clr =
            ir_build2(&lo->b, IR_AND, lv.unit, ir_op_value(lo->fn, old),
                      ir_op_iconst(lv.unit, (i64) ~(mask << lv.bit_shift)));
        ValueId nv = ir_build2(&lo->b, IR_AND, lv.unit, v,
                               ir_op_iconst(lv.unit, (i64)mask));
        ValueId shifted =
            ir_build2(&lo->b, IR_SHL, lv.unit, ir_op_value(lo->fn, nv),
                      ir_op_iconst(lv.unit, (i64)lv.bit_shift));
        ValueId ins =
            ir_build2(&lo->b, IR_OR, lv.unit, ir_op_value(lo->fn, clr),
                      ir_op_value(lo->fn, shifted));

        ir_build_store_typed(&lo->b, ir_op_value(lo->fn, ins), lv.addr,
                             lv.align, lv_flags(&lv), lv.etype);
        /* Re-narrow for the result: shl/shr pair, signedness-aware. */
        {
            u32 up = unit_bits - lv.bit_width;
            ValueId hi =
                ir_build2(&lo->b, IR_SHL, lv.unit, ir_op_value(lo->fn, nv),
                          ir_op_iconst(lv.unit, (i64)up));
            ValueId res = ir_build2(&lo->b, lv.is_signed ? IR_ASHR : IR_LSHR,
                                    lv.unit, ir_op_value(lo->fn, hi),
                                    ir_op_iconst(lv.unit, (i64)up));

            return ir_op_value(lo->fn, res);
        }
    }
}

/* --- scalar conversions --------------------------------------------------- */

IrOperand lower_scalar_convert(Lower *lo, IrOperand v, Type *from, Type *to)
{
    IrType ft, tt;
    bool fint, tint;

    if (!from || !to)
        return v;
    if (to->kind == TY_VOID)
        return v; /* value discarded; nothing to emit */
    ft = lower_irtype(lo, from);
    tt = lower_irtype(lo, to);
    fint = !type_is_fp(from);
    tint = !type_is_fp(to);

    /* _Bool is a CONVERSION, not a truncation: any nonzero maps to 1. */
    if (to->kind == TY_BOOL && !(from->kind == TY_BOOL)) {
        IrOperand one = truth_ne(lo, v, from);
        ValueId t8 = ir_build1(&lo->b, IR_TRUNC, IRT_I8, one);

        return ir_op_value(lo->fn, t8);
    }

    if (ft == tt)
        return v;
    if (fint && tint) {
        if (ft == IRT_PTR && tt == IRT_PTR)
            return v;
        if (ft == IRT_PTR) {
            ValueId as_i = ir_build1(&lo->b, IR_BITCAST, IRT_I64, v);

            if (tt == IRT_I64)
                return ir_op_value(lo->fn, as_i);
            return ir_op_value(lo->fn, ir_build1(&lo->b, IR_TRUNC, tt,
                                                 ir_op_value(lo->fn, as_i)));
        }
        if (tt == IRT_PTR) {
            IrOperand wide = v;

            if (ft != IRT_I64) {
                ValueId w = ir_build1(
                    &lo->b, is_signed_ty(lo, from) ? IR_SEXT : IR_ZEXT, IRT_I64,
                    v);

                wide = ir_op_value(lo->fn, w);
            }
            return ir_op_value(lo->fn,
                               ir_build1(&lo->b, IR_BITCAST, IRT_PTR, wide));
        }
        if (tt > ft)
            return ir_op_value(
                lo->fn,
                ir_build1(&lo->b, is_signed_ty(lo, from) ? IR_SEXT : IR_ZEXT,
                          tt, v));
        return ir_op_value(lo->fn, ir_build1(&lo->b, IR_TRUNC, tt, v));
    }
    if (fint && !tint)
        return ir_op_value(
            lo->fn,
            ir_build1(&lo->b, is_signed_ty(lo, from) ? IR_SITOFP : IR_UITOFP,
                      tt, v));
    if (!fint && tint)
        return ir_op_value(
            lo->fn,
            ir_build1(&lo->b, is_signed_ty(lo, to) ? IR_FPTOSI : IR_FPTOUI, tt,
                      v));
    /* float -> float */
    return ir_op_value(
        lo->fn, ir_build1(&lo->b, tt > ft ? IR_FPEXT : IR_FPTRUNC, tt, v));
}

/* --- member lookup (anonymous members included) --------------------------- */

static bool member_offset(Lower *lo, Type *rec, const char *name, Member **out,
                          u64 *off)
{
    Member *m;

    if (!rec || !rec->tag)
        return false;
    layout_record(lo->sema, rec);
    for (m = rec->tag->members; m; m = m->next) {
        if (m->name == name) {
            *out = m;
            *off += m->offset;
            return true;
        }
        if (!m->name && m->type &&
            (m->type->kind == TY_STRUCT || m->type->kind == TY_UNION)) {
            u64 sub = *off + m->offset;

            if (member_offset(lo, m->type, name, out, &sub)) {
                *off = sub;
                return true;
            }
        }
    }
    return false;
}

/* --- lvalues -------------------------------------------------------------- */

static Lvalue lv_of(Lower *lo, IrOperand addr, Type *t)
{
    Lvalue lv;

    memset(&lv, 0, sizeof(lv));
    lv.addr = addr;
    lv.etype = lower_efftype(lo, t);
    if (t && (t->quals & CGF_QUAL_VOLATILE))
        lv.is_volatile = true;
    if (t && (t->quals & CGF_QUAL_ATOMIC))
        lv.is_atomic = true;
    if (t && lower_is_aggregate(t)) {
        TypeLayout l = layout_of(lo->sema, t);

        lv.unit = IRT_PTR; /* unused for aggregates; loads never happen */
        lv.align = (u32)(l.align ? l.align : 1);
    } else if (t && t->kind != TY_FUNC) {
        TypeLayout l = layout_of(lo->sema, t);

        lv.unit = lower_irtype(lo, t);
        lv.align = (u32)(l.align ? l.align : 1);
        lv.is_signed = type_is_integer(t) && is_signed_ty(lo, t);
    } else {
        lv.unit = IRT_PTR;
        lv.align = 1;
    }
    return lv;
}

static IrOperand addr_plus(Lower *lo, IrOperand base, i64 off)
{
    ValueId r;

    if (off == 0)
        return base;
    if (base.kind == IROP_SYMBOL) {
        /* Constant offsets fold into the symbol addend — no ptradd. */
        IrOperand o = base;

        o.a = (u64)((i64)o.a + off);
        return o;
    }
    r = ir_build_ptradd(&lo->b, base, lower_i64(off));
    return ir_op_value(lo->fn, r);
}

Lvalue lower_lvalue(Lower *lo, AstNode *e)
{
    switch (e->kind) {
    case AST_EXPR_PAREN:
        return lower_lvalue(lo, e->lhs);
    case AST_EXPR_IDENT: {
        Lvalue lv;

        if (!e->sym) {
            lv = lv_of(lo, ir_op_undef(IRT_PTR), sem(e));
            return lv;
        }
        lv = lv_of(lo, lower_sym_addr(lo, e->sym), sem(e));
        return lv;
    }
    case AST_EXPR_STRING: {
        u32 s = lower_string_lit(lo, e);

        return lv_of(lo, ir_op_symbol(IRT_PTR, s, 0), sem(e));
    }
    case AST_EXPR_UNARY:
        if (e->op == PUNCT_STAR) {
            IrOperand p = lower_rvalue(lo, e->lhs);

            return lv_of(lo, p, sem(e));
        }
        break;
    case AST_EXPR_INDEX: {
        /* Preserve source evaluation order, then normalize the commutative
         * C subscript rule: `a[b]` is `*(a + b)`, so either operand may be
         * the pointer (musl deliberately uses `index[pointer]`). */
        IrOperand left = lower_rvalue(lo, e->lhs);
        IrOperand right = lower_rvalue(lo, e->rhs);
        bool left_is_pointer = sem(e->lhs) && sem(e->lhs)->kind == TY_PTR;
        IrOperand base = left_is_pointer ? left : right;
        IrOperand idx = left_is_pointer ? right : left;
        Type *idx_type = left_is_pointer ? sem(e->rhs) : sem(e->lhs);
        /* lower_type_size, NOT layout_of: the element of a multidimensional
         * VLA is itself a VLA, whose static layout size is 0. `int m[r][c]`
         * then scaled every row index by zero and every row of the matrix
         * aliased row 0 -- silently, at every optimization level, with
         * sizeof(m) and sizeof(m[0]) both still correct because they take
         * this same runtime size by a different route. The `p + n` path in
         * ptr_index always did this correctly; only the dedicated subscript
         * shortcut did not. */
        IrOperand el = lower_type_size(lo, sem(e));
        IrOperand wide =
            lower_scalar_convert(lo, idx, idx_type, type_basic(TY_LONG));
        ValueId scaled = ir_build2(&lo->b, IR_IMUL, IRT_I64, wide, el);
        ValueId sum =
            ir_build_ptradd(&lo->b, base, ir_op_value(lo->fn, scaled));

        return lv_of(lo, ir_op_value(lo->fn, sum), sem(e));
    }
    case AST_EXPR_MEMBER: {
        IrOperand base;
        Member *m = NULL;
        u64 off = 0;
        Type *rec;
        Lvalue lv;

        if (e->is_arrow) {
            base = lower_rvalue(lo, e->lhs);
            rec = sem(e->lhs) ? sem(e->lhs)->base : NULL;
        } else {
            /* For a dot access the base may be an lvalue OR a struct
             * RVALUE (`mk().x`, `(c ? a : b).f`): either way, lowering
             * an aggregate expression yields its ADDRESS (of the object
             * or of a materialized temporary), which is exactly what a
             * member offset applies to. */
            base = lower_rvalue(lo, e->lhs);
            rec = sem(e->lhs);
        }
        if (!rec || !member_offset(lo, rec, e->name, &m, &off) || !m) {
            return lv_of(lo, base, sem(e));
        }
        if (!m->is_bitfield) {
            lv = lv_of(lo, addr_plus(lo, base, (i64)off), sem(e));
            if (rec->kind == TY_UNION)
                lv.etype = ETYPE_UNION;
            return lv;
        }
        {
            /* The container window: container_size bytes, aligned to
             * itself; the shift is the field's position within it. The
             * bit_offset Sprint 14 computed is relative to the RECORD, so
             * rebase it against the offset we accumulated (which includes
             * anonymous-member nesting). */
            u64 cbits = m->container_size * 8;
            u64 rec_bit = m->bit_offset;
            u64 unit_index = rec_bit / cbits;
            u64 unit_byte = unit_index * m->container_size;
            u64 shift = rec_bit - unit_byte * 8;
            u64 outer = off - m->offset; /* anonymous nesting bytes */

            memset(&lv, 0, sizeof(lv));
            lv.addr = addr_plus(lo, base, (i64)(outer + unit_byte));
            switch (m->container_size) {
            case 1:
                lv.unit = IRT_I8;
                break;
            case 2:
                lv.unit = IRT_I16;
                break;
            case 4:
                lv.unit = IRT_I32;
                break;
            default:
                lv.unit = IRT_I64;
                break;
            }
            lv.align = (u32)m->container_size;
            lv.etype = rec->kind == TY_UNION ? ETYPE_UNION
                                             : lower_efftype(lo, m->type);
            lv.is_bitfield = true;
            lv.bit_shift = (u8)shift;
            lv.bit_width = (u8)m->bit_width;
            lv.is_signed = is_signed_ty(lo, m->type);
            if (sem(e) && (sem(e)->quals & CGF_QUAL_VOLATILE))
                lv.is_volatile = true;
            return lv;
        }
    }
    case AST_EXPR_COMPOUND_LIT: {
        /* Block-scope compound literal: a distinct object per evaluation
         * (6.5.2.5p3), automatic storage — an alloca, then its braced
         * initializer runs. */
        ValueId t = lower_temp(lo, sem(e));
        IrOperand addr = ir_op_value(lo->fn, t);

        lower_local_init(lo, addr, sem(e), e->init);
        return lv_of(lo, addr, sem(e));
    }
    default:
        break;
    }
    /* An lvalue kind sema accepted but this switch does not know is a
     * lowering bug, not a user error. */
    CGF_ICE("lower_lvalue: unhandled lvalue kind %d", (int)e->kind);
}

/* --- short-circuit and conditional ---------------------------------------- */

/* a && b / a || b: three blocks, the result travels as a block PARAM —
 * zero allocas, and nesting composes with no special cases. */
static IrOperand lower_logical(Lower *lo, AstNode *e)
{
    bool is_and = e->op == PUNCT_AMPAMP;
    IrOperand ca = lower_cond(lo, e->lhs);
    BlockId rhs = lower_new_block(lo, is_and ? "and.rhs" : "or.rhs");
    BlockId join = lower_new_block(lo, is_and ? "and.join" : "or.join");
    ValueId res = ir_block_param(lo->m, lo->fn, join, IRT_I32);
    IrOperand shortval = ir_op_iconst(IRT_I32, is_and ? 0 : 1);
    IrOperand cb;

    if (is_and)
        ir_build_condbr(&lo->b, ca, rhs, NULL, 0, join, &shortval, 1);
    else
        ir_build_condbr(&lo->b, ca, join, &shortval, 1, rhs, NULL, 0);
    lower_at(lo, rhs);
    cb = lower_cond(lo, e->rhs);
    ir_build_br(&lo->b, join, &cb, 1);
    lower_at(lo, join);
    return ir_op_value(lo->fn, res);
}

static IrOperand lower_ternary(Lower *lo, AstNode *e)
{
    IrOperand c = lower_cond(lo, e->lhs);
    BlockId tb = lower_new_block(lo, "cond.then");
    BlockId eb = lower_new_block(lo, "cond.else");
    BlockId join = lower_new_block(lo, "cond.join");
    Type *rt = sem(e);
    bool is_void = rt && rt->kind == TY_VOID;
    bool is_agg = lower_is_aggregate(rt);
    ValueId agg_tmp = VALUE_INVALID;
    ValueId res = VALUE_INVALID;
    IrOperand v;

    if (is_agg)
        agg_tmp = lower_temp(lo, rt); /* one temp, both arms memcpy in */
    else if (!is_void)
        res = ir_block_param(lo->m, lo->fn, join, lower_irtype(lo, rt));

    ir_build_condbr(&lo->b, c, tb, NULL, 0, eb, NULL, 0);

    lower_at(lo, tb);
    v = lower_rvalue(lo, e->mid);
    if (is_agg) {
        TypeLayout l = layout_of(lo->sema, rt);

        lower_memcpy_aggregate(lo, ir_op_value(lo->fn, agg_tmp), v, rt,
                               (u32)l.align, 0);
        ir_build_br(&lo->b, join, NULL, 0);
    } else if (is_void) {
        ir_build_br(&lo->b, join, NULL, 0);
    } else {
        ir_build_br(&lo->b, join, &v, 1);
    }

    lower_at(lo, eb);
    v = lower_rvalue(lo, e->rhs);
    if (is_agg) {
        TypeLayout l = layout_of(lo->sema, rt);

        lower_memcpy_aggregate(lo, ir_op_value(lo->fn, agg_tmp), v, rt,
                               (u32)l.align, 0);
        ir_build_br(&lo->b, join, NULL, 0);
    } else if (is_void) {
        ir_build_br(&lo->b, join, NULL, 0);
    } else {
        ir_build_br(&lo->b, join, &v, 1);
    }

    lower_at(lo, join);
    if (is_agg)
        return ir_op_value(lo->fn, agg_tmp);
    if (is_void)
        return ir_op_undef(IRT_I32); /* no one may look */
    return ir_op_value(lo->fn, res);
}

/* --- binary operators ----------------------------------------------------- */

static IrIcmp icmp_pred_for(u16 op, bool sign)
{
    switch (op) {
    case PUNCT_EQEQ:
        return ICMP_EQ;
    case PUNCT_NOTEQ:
        return ICMP_NE;
    case PUNCT_LT:
        return sign ? ICMP_SLT : ICMP_ULT;
    case PUNCT_LE:
        return sign ? ICMP_SLE : ICMP_ULE;
    case PUNCT_GT:
        return sign ? ICMP_SGT : ICMP_UGT;
    default:
        return sign ? ICMP_SGE : ICMP_UGE;
    }
}

static IrFcmp fcmp_pred_for(u16 op)
{
    switch (op) {
    case PUNCT_EQEQ:
        return FCMP_OEQ;
    case PUNCT_NOTEQ:
        return FCMP_UNE; /* NaN != NaN is TRUE */
    case PUNCT_LT:
        return FCMP_OLT;
    case PUNCT_LE:
        return FCMP_OLE;
    case PUNCT_GT:
        return FCMP_OGT;
    default:
        return FCMP_OGE;
    }
}

static bool is_cmp_op(u16 op)
{
    return op == PUNCT_EQEQ || op == PUNCT_NOTEQ || op == PUNCT_LT ||
           op == PUNCT_GT || op == PUNCT_LE || op == PUNCT_GE;
}

/* The arithmetic op for `op` on C type t (integer signedness decides the
 * division/shift flavor). */
static IrOp arith_op_for(Lower *lo, u16 op, Type *t)
{
    bool fp = type_is_fp(t);
    bool sign = !fp && is_signed_ty(lo, t);

    switch (op) {
    case PUNCT_PLUS:
        return fp ? IR_FADD : IR_IADD;
    case PUNCT_MINUS:
        return fp ? IR_FSUB : IR_ISUB;
    case PUNCT_STAR:
        return fp ? IR_FMUL : IR_IMUL;
    case PUNCT_SLASH:
        return fp ? IR_FDIV : sign ? IR_SDIV : IR_UDIV;
    case PUNCT_PERCENT:
        return sign ? IR_SREM : IR_UREM;
    case PUNCT_AMP:
        return IR_AND;
    case PUNCT_PIPE:
        return IR_OR;
    case PUNCT_CARET:
        return IR_XOR;
    case PUNCT_SHL:
        return IR_SHL;
    case PUNCT_SHR:
        return sign ? IR_ASHR : IR_LSHR;
    default:
        CGF_ICE("arith_op_for: not an arithmetic op %u", op);
    }
}

/* Pointer + integer with scaling; `neg` for p - n. */
static IrOperand ptr_index(Lower *lo, IrOperand p, IrOperand n, Type *ptr_ty,
                           Type *idx_ty, bool neg)
{
    IrOperand esz = lower_type_size(lo, ptr_ty->base);
    IrOperand wide = lower_scalar_convert(lo, n, idx_ty, type_basic(TY_LONG));
    ValueId scaled = ir_build2(&lo->b, IR_IMUL, IRT_I64, wide, esz);
    IrOperand off = ir_op_value(lo->fn, scaled);
    ValueId r;

    if (neg) {
        ValueId z = ir_build2(&lo->b, IR_ISUB, IRT_I64, lower_i64(0), off);

        off = ir_op_value(lo->fn, z);
    }
    r = ir_build_ptradd(&lo->b, p, off);
    return ir_op_value(lo->fn, r);
}

static IrOperand lower_binary(Lower *lo, AstNode *e)
{
    Type *lt = sem(e->lhs);
    Type *rt = sem(e->rhs);

    if (e->op == PUNCT_COMMA) {
        (void)lower_rvalue(lo, e->lhs); /* side effects only */
        return lower_rvalue(lo, e->rhs);
    }
    if (e->op == PUNCT_AMPAMP || e->op == PUNCT_PIPEPIPE)
        return lower_logical(lo, e);

    if (is_cmp_op(e->op)) {
        IrOperand a = lower_rvalue(lo, e->lhs);
        IrOperand b = lower_rvalue(lo, e->rhs);
        ValueId r;

        if (type_is_fp(lt)) {
            r = ir_build_fcmp(&lo->b, fcmp_pred_for(e->op), a, b);
        } else if (lt && lt->kind == TY_PTR && rt && rt->kind == TY_PTR) {
            r = ir_build_icmp(&lo->b, icmp_pred_for(e->op, false), a, b);
        } else if (lt && lt->kind == TY_PTR) {
            /* ptr vs integer (gcc-warned, still compiles): compare the
             * pointer's bits. */
            IrOperand bi = lower_scalar_convert(lo, b, rt, lt);

            r = ir_build_icmp(&lo->b, icmp_pred_for(e->op, false), a, bi);
        } else if (rt && rt->kind == TY_PTR) {
            IrOperand ai = lower_scalar_convert(lo, a, lt, rt);

            r = ir_build_icmp(&lo->b, icmp_pred_for(e->op, false), ai, b);
        } else {
            r = ir_build_icmp(&lo->b,
                              icmp_pred_for(e->op, is_signed_ty(lo, lt)), a, b);
        }
        return ir_op_value(lo->fn, r);
    }

    /* Pointer arithmetic before the plain table. */
    if (e->op == PUNCT_PLUS || e->op == PUNCT_MINUS) {
        bool lp = lt && lt->kind == TY_PTR;
        bool rp = rt && rt->kind == TY_PTR;

        if (lp && rp) {
            /* p - q: byte difference / element size, i64 result. */
            IrOperand a = lower_rvalue(lo, e->lhs);
            IrOperand b = lower_rvalue(lo, e->rhs);
            /* Runtime size for the same reason the subscript below needs
             * one: pointers into a multidimensional VLA have a VLA pointee,
             * whose static size is 0. The zero-size guard stays for the
             * non-VLA cases that reach here (void pointees), where dividing
             * by the real 0 would trap rather than mean anything. */
            TypeLayout ell = layout_of(lo->sema, lt->base);
            IrOperand el = lt->base && lt->base->is_vla
                               ? lower_type_size(lo, lt->base)
                               : lower_i64((i64)(ell.size ? ell.size : 1));
            ValueId ai = ir_build1(&lo->b, IR_BITCAST, IRT_I64, a);
            ValueId bi = ir_build1(&lo->b, IR_BITCAST, IRT_I64, b);
            ValueId d =
                ir_build2(&lo->b, IR_ISUB, IRT_I64, ir_op_value(lo->fn, ai),
                          ir_op_value(lo->fn, bi));
            ValueId q =
                ir_build2(&lo->b, IR_SDIV, IRT_I64, ir_op_value(lo->fn, d), el);

            return ir_op_value(lo->fn, q);
        }
        if (lp || rp) {
            IrOperand a = lower_rvalue(lo, e->lhs);
            IrOperand b = lower_rvalue(lo, e->rhs);

            if (lp)
                return ptr_index(lo, a, b, lt, rt, e->op == PUNCT_MINUS);
            /* n + p (PLUS only; sema rejected ptr on the right of -) */
            return ptr_index(lo, b, a, rt, lt, false);
        }
    }

    {
        IrOperand a = lower_rvalue(lo, e->lhs);
        IrOperand b = lower_rvalue(lo, e->rhs);
        IrType t = lower_irtype(lo, sem(e));
        ValueId r;

        /* Shift counts promote INDEPENDENTLY of the left operand (the
         * 6.5.7 rule sema encoded); the IR wants one width — convert
         * the count to the result type (the & 63/31 hardware mask makes
         * the narrowing harmless; over-width shifts are C UB anyway).
         * Found by the Sprint 21 MIR pipeline; latent since Sprint 18. */
        if ((e->op == PUNCT_SHL || e->op == PUNCT_SHR) && sem(e->rhs) != sem(e))
            b = lower_scalar_convert(lo, b, sem(e->rhs), sem(e));
        r = build_source_arith(lo, arith_op_for(lo, e->op, sem(e)), t, a, b,
                               sem(e));
        return ir_op_value(lo->fn, r);
    }
}

/* --- atomic read-modify-write (Sprint 20, seq_cst only) --------------------
 *
 * add/sub/and/or/xor on integer _Atomic lvalues map to `atomicrmw`; every
 * other compound op (mul/div/mod/shifts, and all float arithmetic) takes
 * the cmpxchg retry loop. cmpxchg returns the OLD memory value; success
 * is `icmp eq old, expected`. Floats ride an integer container through
 * bitcasts. The result of the C expression is the NEW value (old for
 * postfix ++/--, which is exactly what atomicrmw hands back). */

static IrOp rmw_compute_op(Lower *lo, u16 op, Type *t)
{
    return arith_op_for(lo, op, t);
}

static bool rmw_direct(u16 op)
{
    return op == PUNCT_PLUS || op == PUNCT_MINUS || op == PUNCT_AMP ||
           op == PUNCT_PIPE || op == PUNCT_CARET;
}

static IrAtomicRmw rmw_kind(u16 op)
{
    switch (op) {
    case PUNCT_PLUS:
        return RMW_ADD;
    case PUNCT_MINUS:
        return RMW_SUB;
    case PUNCT_AMP:
        return RMW_AND;
    case PUNCT_PIPE:
        return RMW_OR;
    default:
        return RMW_XOR;
    }
}

/* new = old <op> rhs, computed in the lvalue's own type (for the RMW-able
 * ops the mod-2^w homomorphism makes wide-then-narrow equal to
 * narrow-then-op; for the loop ops we compute in the C-correct common
 * type and convert back). Returns the expression's RESULT value:
 * `want_old` picks the pre-value (postfix ++/--). */
static IrOperand lower_atomic_update(Lower *lo, Lvalue lv, Type *lt, u16 op,
                                     IrOperand rhs, Type *rt, bool want_old)
{
    bool is_int = type_is_integer(lt);

    if (is_int && rmw_direct(op)) {
        IrOperand v = lower_scalar_convert(lo, rhs, rt, lt);
        ValueId old =
            ir_build_atomicrmw(&lo->b, rmw_kind(op), lv.unit, lv.addr, v);

        if (want_old)
            return ir_op_value(lo->fn, old);
        {
            /* atomicrmw defines signed add/sub modulo 2^N; this expression
             * result merely reconstructs the new value and therefore must
             * not claim the ordinary signed-overflow license. */
            ValueId nv = ir_build2(&lo->b, rmw_compute_op(lo, op, lt), lv.unit,
                                   ir_op_value(lo->fn, old), v);

            return ir_op_value(lo->fn, nv);
        }
    }
    {
        /* The cmpxchg retry loop, on the value's integer container. */
        IrType ct = lv.unit <= IRT_I64   ? lv.unit
                    : lv.unit == IRT_F32 ? IRT_I32
                                         : IRT_I64; /* f64; f80+ rejected */
        Lvalue clv = lv;
        IrOperand init;
        BlockId retry = lower_new_block(lo, "rmw.retry");
        BlockId done = lower_new_block(lo, "rmw.done");
        ValueId oldp = ir_block_param(lo->m, lo->fn, retry, ct);
        ValueId resp = ir_block_param(lo->m, lo->fn, done, ct);
        IrOperand oldo, newo;

        clv.unit = ct;
        clv.is_bitfield = false;
        init = lower_load(lo, clv);
        ir_build_br(&lo->b, retry, &init, 1);
        lower_at(lo, retry);
        oldo = ir_op_value(lo->fn, oldp);
        {
            /* container -> C value -> common type, op, and back */
            IrOperand oldv = oldo;
            Type *common;
            IrOperand a, b2, backc;
            ValueId r;

            if (!is_int) {
                ValueId f = ir_build1(&lo->b, IR_BITCAST, lv.unit, oldo);

                oldv = ir_op_value(lo->fn, f);
            }
            if (op == PUNCT_SHL || op == PUNCT_SHR)
                common = conv_promote_type(lo->sema, lt);
            else
                common = conv_uac_type(lo->sema, lt, rt);
            a = lower_scalar_convert(lo, oldv, lt, common);
            b2 = lower_scalar_convert(lo, rhs, rt, common);
            r = build_source_arith(lo, arith_op_for(lo, op, common),
                                   lower_irtype(lo, common), a, b2, common);
            backc =
                lower_scalar_convert(lo, ir_op_value(lo->fn, r), common, lt);
            if (!is_int) {
                ValueId bi = ir_build1(&lo->b, IR_BITCAST, ct, backc);

                newo = ir_op_value(lo->fn, bi);
            } else {
                newo = backc;
            }
        }
        {
            ValueId got = ir_build_cmpxchg(&lo->b, ct, lv.addr, oldo, newo);
            ValueId ok =
                ir_build_icmp(&lo->b, ICMP_EQ, ir_op_value(lo->fn, got), oldo);
            IrOperand res_arg = want_old ? oldo : newo;
            IrOperand retry_arg = ir_op_value(lo->fn, got);

            ir_build_condbr(&lo->b, ir_op_value(lo->fn, ok), done, &res_arg, 1,
                            retry, &retry_arg, 1);
        }
        lower_at(lo, done);
        {
            IrOperand res = ir_op_value(lo->fn, resp);

            if (!is_int) {
                ValueId f = ir_build1(&lo->b, IR_BITCAST, lv.unit, res);

                return ir_op_value(lo->fn, f);
            }
            return res;
        }
    }
}

/* --- assignment ----------------------------------------------------------- */

static IrOperand lower_assign(Lower *lo, AstNode *e)
{
    if (e->op == PUNCT_ASSIGN) {
        if (lower_is_aggregate(sem(e->lhs))) {
            /* ONE memcpy. Never memberwise: unions and padding both
             * observe the difference (the §8 law). */
            Lvalue lv = lower_lvalue(lo, e->lhs);
            IrOperand src = lower_rvalue(lo, e->rhs);
            TypeLayout l = layout_of(lo->sema, sem(e->lhs));

            lower_memcpy_aggregate(lo, lv.addr, src, sem(e->lhs), (u32)l.align,
                                   lv.is_volatile ? IRF_VOLATILE : 0);
            return lv.addr;
        }
        {
            /* Address first, value second — left-to-right, pinned. */
            Lvalue lv = lower_lvalue(lo, e->lhs);
            IrOperand v = lower_rvalue(lo, e->rhs);

            return lower_store(lo, lv, v);
        }
    }
    {
        /* Compound assignment: ONE address evaluation, then the
         * old-op-convert-back dance in the UAC type. The tree carries no
         * materialized casts here (the LHS is an lvalue, not a value), so
         * this is the one place lowering asks conv_* for a type. */
        static const u16 base_op[] = {
            [PUNCT_STAR_ASSIGN] = PUNCT_STAR,
            [PUNCT_SLASH_ASSIGN] = PUNCT_SLASH,
            [PUNCT_PERCENT_ASSIGN] = PUNCT_PERCENT,
            [PUNCT_PLUS_ASSIGN] = PUNCT_PLUS,
            [PUNCT_MINUS_ASSIGN] = PUNCT_MINUS,
            [PUNCT_SHL_ASSIGN] = PUNCT_SHL,
            [PUNCT_SHR_ASSIGN] = PUNCT_SHR,
            [PUNCT_AMP_ASSIGN] = PUNCT_AMP,
            [PUNCT_CARET_ASSIGN] = PUNCT_CARET,
            [PUNCT_PIPE_ASSIGN] = PUNCT_PIPE,
        };
        u16 op = base_op[e->op];
        Type *lt = sem(e->lhs);
        Type *rt = sem(e->rhs);
        Lvalue lv = lower_lvalue(lo, e->lhs);
        IrOperand old;
        IrOperand rhs;

        if (lv.is_atomic) {
            rhs = lower_rvalue(lo, e->rhs);
            return lower_atomic_update(lo, lv, lt, op, rhs, rt, false);
        }
        old = lower_load(lo, lv);
        rhs = lower_rvalue(lo, e->rhs);

        if (lt->kind == TY_PTR) {
            /* p += n / p -= n. */
            IrOperand np = ptr_index(lo, old, rhs, lt, rt, op == PUNCT_MINUS);

            return lower_store(lo, lv, np);
        }
        {
            Type *common;
            IrOperand a, b2;
            ValueId r;
            IrOperand back;

            if (op == PUNCT_SHL || op == PUNCT_SHR)
                common = conv_promote_type(lo->sema, lt);
            else
                common = conv_uac_type(lo->sema, lt, rt);
            a = lower_scalar_convert(lo, old, lt, common);
            b2 = lower_scalar_convert(lo, rhs, rt, common);
            r = build_source_arith(lo, arith_op_for(lo, op, common),
                                   lower_irtype(lo, common), a, b2, common);
            back = lower_scalar_convert(lo, ir_op_value(lo->fn, r), common, lt);
            return lower_store(lo, lv, back);
        }
    }
}

/* --- calls -----------------------------------------------------------------
 */

/* Peels parens and the implicit function-to-pointer decay to find a
 * direct callee symbol, if there is one. */
static Symbol *direct_callee(AstNode *fn)
{
    for (;;) {
        if (!fn)
            return NULL;
        if (fn->kind == AST_EXPR_PAREN) {
            fn = fn->lhs;
            continue;
        }
        if (fn->kind == AST_EXPR_CAST && fn->implicit) {
            fn = fn->lhs;
            continue;
        }
        if (fn->kind == AST_EXPR_UNARY && fn->op == PUNCT_STAR) {
            /* (*f)(...) on a function designator is the same call. */
            fn = fn->lhs;
            continue;
        }
        if (fn->kind == AST_EXPR_IDENT && fn->sym && fn->sym->kind == SYM_FUNC)
            return fn->sym;
        return NULL;
    }
}

/* --- varargs (SysV x86-64, Sprint 19) -------------------------------------
 *
 * The va_list record is OURS (sema synthesized it), so the field offsets
 * are constants here: gp_offset +0, fp_offset +4, overflow_arg_area +8,
 * reg_save_area +16. va_start stores the two offsets (compile-time
 * constants of the CURRENT function's named-register consumption) and
 * leaves the two pointers to the IR-level `va_start` op — only codegen
 * knows where the register save area and the first stack argument live.
 * va_arg expands to the classification-driven branch diamond right here;
 * there is no va_arg instruction, and Sprint 50's Apple-arm64 divergence
 * will be a different EXPANSION, not a different op. */

static IrOperand va_field_addr(Lower *lo, IrOperand ap, i64 off)
{
    ValueId p;

    if (off == 0)
        return ap;
    p = ir_build_ptradd(&lo->b, ap, lower_i64(off));
    return ir_op_value(lo->fn, p);
}

static void va_store_u32(Lower *lo, IrOperand ap, i64 off, IrOperand v)
{
    Lvalue lv;

    memset(&lv, 0, sizeof(lv));
    lv.addr = va_field_addr(lo, ap, off);
    lv.unit = IRT_I32;
    lv.align = 4;
    lower_store(lo, lv, v);
}

/* --- varargs (Linux AAPCS64, Sprint 48) ------------------------------------
 *
 * The five-field record (sema synthesized it, so these offsets are ours):
 * __stack +0, __gr_top +8, __vr_top +16, __gr_offs +24, __vr_offs +28.
 *
 * The backwards-offset design is the thing to understand. Both offsets are
 * NEGATIVE and count UP toward zero: the argument at offset o lives at
 * `top + o`, and reaching zero means the register save area is exhausted.
 * That is why the register test is two comparisons, not one — an argument
 * needing two registers can START below zero and CROSS it, and an argument
 * that crosses was passed on the stack in its entirety. Testing only the
 * starting offset reads half an argument out of the save area and half out
 * of nowhere. */

static bool lower_is_aapcs64(Lower *lo)
{
    return lo->sema->target.kind == CGF_TARGET_ARM64_LINUX ||
           lo->sema->target.kind == CGF_TARGET_ARM64_MACOS;
}

#define VA64_STACK 0
#define VA64_GR_TOP 8
#define VA64_VR_TOP 16
#define VA64_GR_OFFS 24
#define VA64_VR_OFFS 28

static IrOperand va64_load_ptr(Lower *lo, IrOperand ap, i64 off)
{
    Lvalue lv;

    memset(&lv, 0, sizeof(lv));
    lv.addr = va_field_addr(lo, ap, off);
    lv.unit = IRT_PTR;
    lv.align = 8;
    return lower_load(lo, lv);
}

static void va64_store_ptr(Lower *lo, IrOperand ap, i64 off, IrOperand v)
{
    Lvalue lv;

    memset(&lv, 0, sizeof(lv));
    lv.addr = va_field_addr(lo, ap, off);
    lv.unit = IRT_PTR;
    lv.align = 8;
    lower_store(lo, lv, v);
}

static IrOperand lower_va_arg_aapcs64(Lower *lo, AstNode *e, IrOperand ap)
{
    Type *t = sem(e);
    TypeLayout l = layout_of(lo->sema, t);
    bool is_agg = lower_is_aggregate(t);
    AbiArg plan;
    bool fp_path = false;
    bool indirect = false;
    i64 nslots = 1;
    i64 hfa_leaf = 0; /* HFA leaf width in bytes; 0 when not an HFA */
    i64 bump;
    i64 top_off, offs_off;
    BlockId join, stack;
    ValueId addr;

    abi_classify_arg(lo, t, &plan);
    switch (plan.kind) {
    case ABI_ARG_SCALAR: {
        IrType st = lower_irtype(lo, t);

        if (st == IRT_F32 || st == IRT_F64)
            fp_path = true;
        else if (st == IRT_F80 || st == IRT_F128)
            lower_unimplemented(
                lo, e->span, "va_arg of a 128-bit floating type on arm64", 49);
        break;
    }
    case ABI_ARG_EIGHTBYTES:
        /* A composite of 16 bytes or fewer travels in one or two general
         * registers, so its save-area image is already contiguous. */
        nslots = plan.n;
        break;
    case ABI_ARG_HFA:
        /* Each leaf occupies its own 16-byte q slot in the save area, so the
         * aggregate is NOT contiguous there: three floats sit at +0, +16 and
         * +32, not at +0, +4, +8. The register path gathers them leaf by leaf
         * below. Everything else about the walk is the ordinary FP one --
         * each leaf spends a v-register, so the offset advances by 16 apiece.
         */
        fp_path = true;
        nslots = plan.n;
        hfa_leaf = (i64)ir_type_size(plan.t[0]);
        break;
    default:
        /* Over 16 bytes: the caller passed a POINTER, so the slot holds an
         * address and the object lives behind it. */
        indirect = true;
        break;
    }
    bump = fp_path ? 16 * nslots : 8 * nslots;
    top_off = fp_path ? VA64_VR_TOP : VA64_GR_TOP;
    offs_off = fp_path ? VA64_VR_OFFS : VA64_GR_OFFS;

    join = lower_new_block(lo, "va.join");
    addr = ir_block_param(lo->m, lo->fn, join, IRT_PTR);
    stack = lower_new_block(lo, "va.stack");
    {
        BlockId cross = lower_new_block(lo, "va.cross");
        BlockId reg = lower_new_block(lo, "va.reg");
        Lvalue offlv;
        IrOperand off;
        ValueId below, bumped, exhausted;

        memset(&offlv, 0, sizeof(offlv));
        offlv.addr = va_field_addr(lo, ap, offs_off);
        offlv.unit = IRT_I32;
        offlv.align = 4;
        offlv.is_signed = true;
        off = lower_load(lo, offlv);

        below = ir_build_icmp(&lo->b, ICMP_SLT, off, ir_op_iconst(IRT_I32, 0));
        ir_build_condbr(&lo->b, ir_op_value(lo->fn, below), cross, NULL, 0,
                        stack, NULL, 0);

        lower_at(lo, cross);
        bumped = ir_build2(&lo->b, IR_IADD, IRT_I32, off,
                           ir_op_iconst(IRT_I32, bump));
        exhausted = ir_build_icmp(&lo->b, ICMP_SLE, ir_op_value(lo->fn, bumped),
                                  ir_op_iconst(IRT_I32, 0));
        ir_build_condbr(&lo->b, ir_op_value(lo->fn, exhausted), reg, NULL, 0,
                        stack, NULL, 0);

        lower_at(lo, reg);
        {
            IrOperand top = va64_load_ptr(lo, ap, top_off);
            ValueId wide = ir_build1(&lo->b, IR_SEXT, IRT_I64, off);
            ValueId at =
                ir_build_ptradd(&lo->b, top, ir_op_value(lo->fn, wide));
            IrOperand from_reg = ir_op_value(lo->fn, at);

            if (hfa_leaf && nslots > 1) {
                /* Gather the leaves out of their 16-byte q slots into a
                 * contiguous temporary at the leaf width. The same reshaping
                 * the SysV path needs for a multi-eightbyte SSE aggregate,
                 * and for the same reason: a save area is laid out by
                 * REGISTER, not by object. */
                ValueId tmp = ir_build_alloca_typed(
                    &lo->b, lower_i64(nslots * hfa_leaf),
                    l.align > 8 ? (u32)l.align : 8, lower_efftype(lo, t));
                IrType lt = hfa_leaf == 4 ? IRT_F32 : IRT_F64;
                i64 k;

                for (k = 0; k < nslots; k++) {
                    Lvalue src, dst;
                    IrOperand sp = ir_op_value(lo->fn, at);
                    IrOperand dp = ir_op_value(lo->fn, tmp);
                    IrOperand leaf;

                    if (k) {
                        sp = ir_op_value(
                            lo->fn,
                            ir_build_ptradd(&lo->b, sp, lower_i64(k * 16)));
                        dp = ir_op_value(
                            lo->fn, ir_build_ptradd(&lo->b, dp,
                                                    lower_i64(k * hfa_leaf)));
                    }
                    memset(&src, 0, sizeof(src));
                    src.addr = sp;
                    src.unit = lt;
                    src.align = (u32)hfa_leaf;
                    leaf = lower_load(lo, src);
                    memset(&dst, 0, sizeof(dst));
                    dst.addr = dp;
                    dst.unit = lt;
                    dst.align = (u32)hfa_leaf;
                    lower_store(lo, dst, leaf);
                }
                from_reg = ir_op_value(lo->fn, tmp);
            }
            va_store_u32(lo, ap, offs_off, ir_op_value(lo->fn, bumped));
            ir_build_br(&lo->b, join, &from_reg, 1);
        }
    }
    lower_at(lo, stack);
    {
        IrOperand st = va64_load_ptr(lo, ap, VA64_STACK);
        u64 slot = indirect ? 8 : ((u64)l.size + 7) & ~7ull;
        ValueId next;
        IrOperand from_stack;

        if (!indirect && l.align > 8) {
            ValueId as_i = ir_build1(&lo->b, IR_BITCAST, IRT_I64, st);
            ValueId up =
                ir_build2(&lo->b, IR_IADD, IRT_I64, ir_op_value(lo->fn, as_i),
                          lower_i64((i64)l.align - 1));
            ValueId masked =
                ir_build2(&lo->b, IR_AND, IRT_I64, ir_op_value(lo->fn, up),
                          lower_i64(-(i64)l.align));

            st = ir_op_value(lo->fn, ir_build1(&lo->b, IR_BITCAST, IRT_PTR,
                                               ir_op_value(lo->fn, masked)));
            slot = ((u64)l.size + (u64)l.align - 1) & ~((u64)l.align - 1);
        }
        next = ir_build_ptradd(&lo->b, st, lower_i64((i64)slot));
        va64_store_ptr(lo, ap, VA64_STACK, ir_op_value(lo->fn, next));
        from_stack = st;
        ir_build_br(&lo->b, join, &from_stack, 1);
    }
    lower_at(lo, join);
    {
        IrOperand at = ir_op_value(lo->fn, addr);

        if (indirect) {
            Lvalue plv;

            memset(&plv, 0, sizeof(plv));
            plv.addr = at;
            plv.unit = IRT_PTR;
            plv.align = 8;
            at = lower_load(lo, plv);
        }
        if (is_agg) {
            ValueId tmp = lower_temp(lo, t);

            lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), at, t,
                                   (u32)(l.align > 8 ? 8 : l.align), 0);
            return ir_op_value(lo->fn, tmp);
        }
        {
            Lvalue lv;

            memset(&lv, 0, sizeof(lv));
            lv.addr = at;
            lv.unit = lower_irtype(lo, t);
            lv.align = (u32)(l.align > 8 ? 8 : l.align);
            lv.is_signed = type_is_integer(t) && is_signed_ty(lo, t);
            return lower_load(lo, lv);
        }
    }
}

/* The cursor a va_* builtin operates on, as a POINTER to it.
 *
 * An array va_list has already decayed to that pointer, so its value is the
 * address. Apple's is a `char *` object, and its value is the CURSOR rather
 * than a pointer to it -- taking the address is what makes the two uniform,
 * and reading the value instead would have every va_arg advance a copy. */
static IrOperand lower_va_cursor(Lower *lo, AstNode *e)
{
    if (lo->sema->target.kind == CGF_TARGET_ARM64_MACOS)
        return lower_lvalue(lo, e).addr;
    return lower_rvalue(lo, e);
}

/* Apple's arm64 va_arg, and the whole of it.
 *
 * Every anonymous argument is on the stack, so there is no register save
 * area, no offset pair, and no classification diamond -- just a cursor:
 *
 *     cursor = *ap
 *     cursor = align_up(cursor, alignof(T))     // only when align > 8
 *     value  = *(T *)cursor
 *     *ap    = cursor + round_up(sizeof(T), 8)
 *
 * Slots are 8 bytes even for a char. The alignment step matters only for
 * 16-aligned types; clang emits the and/orr dance for __int128 and nothing at
 * all for double, and `long double` takes an ordinary 8-byte slot because
 * Apple makes it a double.
 *
 * AN AGGREGATE IS NOT ALWAYS BY VALUE. Anonymity removes the register, never
 * the shape: an HFA and anything <= 16 bytes sits in the varargs area
 * directly (clang reads a 16-byte struct with a single `ldp` straight out of
 * it), but a non-HFA over 16 bytes is INDIRECT there exactly as it is in a
 * register -- the slot holds a POINTER, the cursor advances by 8, and the
 * value is read through it:
 *
 *     ldr x8, [x29, #16]     // the pointer out of the slot
 *     ldr q0, [x8]           // 20 bytes THROUGH it
 *
 * versus the 24-byte HFA one function along, which advances by 24 and reads
 * in place. Treating every aggregate as by-value read a pointer's bytes as
 * though they were the struct. The ABI ledger recorded this gap as
 * caller-only; it is not, and `abi_classify_arg` is asked rather than
 * re-deriving the rule, so the two halves cannot drift.
 *
 * `ap` is a plain `char *` OBJECT here, not the one-element array the other
 * two targets use, so the cursor is read and written through `ap` itself. */
static IrOperand lower_va_arg_apple(Lower *lo, AstNode *e, IrOperand ap)
{
    Type *t = sem(e);
    TypeLayout l = layout_of(lo->sema, t);
    u64 slot = ((u64)l.size + 7) & ~7ull;
    bool indirect = false;
    Lvalue cur;
    IrOperand at;
    ValueId next;

    if (lower_is_aggregate(t)) {
        AbiArg plan;

        abi_classify_arg(lo, t, &plan);
        indirect = plan.kind == ABI_ARG_BYVAL;
        if (indirect)
            slot = 8; /* the slot holds a pointer, not the object */
    }
    memset(&cur, 0, sizeof(cur));
    cur.addr = ap;
    cur.unit = IRT_PTR;
    cur.align = 8;
    at = lower_load(lo, cur);
    if (l.align > 8 && !indirect) {
        ValueId as_i = ir_build1(&lo->b, IR_BITCAST, IRT_I64, at);
        ValueId up =
            ir_build2(&lo->b, IR_IADD, IRT_I64, ir_op_value(lo->fn, as_i),
                      lower_i64((i64)l.align - 1));
        ValueId masked =
            ir_build2(&lo->b, IR_AND, IRT_I64, ir_op_value(lo->fn, up),
                      lower_i64(-(i64)l.align));

        at = ir_op_value(lo->fn, ir_build1(&lo->b, IR_BITCAST, IRT_PTR,
                                           ir_op_value(lo->fn, masked)));
        slot = ((u64)l.size + (u64)l.align - 1) & ~((u64)l.align - 1);
    }
    next = ir_build_ptradd(&lo->b, at, lower_i64((i64)slot));
    lower_store(lo, cur, ir_op_value(lo->fn, next));
    if (indirect) {
        /* The slot held the caller's pointer: dereference it once, and the
         * object it names is what gets copied out below. The cursor has
         * already advanced by the 8 bytes the pointer occupied. */
        Lvalue slotlv;

        memset(&slotlv, 0, sizeof(slotlv));
        slotlv.addr = at;
        slotlv.unit = IRT_PTR;
        slotlv.align = 8;
        at = lower_load(lo, slotlv);
    }
    /* `at` is the ADDRESS of the argument; the contract is the value. An
     * aggregate is copied out because the varargs area is the caller's
     * memory and the callee may not alias it. */
    if (lower_is_aggregate(t)) {
        ValueId tmp = lower_temp(lo, t);

        lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), at, t,
                               (u32)(l.align > 8 ? 8 : l.align), 0);
        return ir_op_value(lo->fn, tmp);
    }
    {
        Lvalue lv;

        memset(&lv, 0, sizeof(lv));
        lv.addr = at;
        lv.unit = lower_irtype(lo, t);
        lv.align = (u32)(l.align > 8 ? 8 : l.align);
        lv.is_signed = type_is_integer(t) && is_signed_ty(lo, t);
        return lower_load(lo, lv);
    }
}

static IrOperand lower_va_arg(Lower *lo, AstNode *e)
{
    Type *t = sem(e);
    IrOperand ap = lower_va_cursor(lo, e->lhs);
    AbiArg plan;

    if (lo->sema->target.kind == CGF_TARGET_ARM64_MACOS)
        return lower_va_arg_apple(lo, e, ap);
    if (lower_is_aapcs64(lo))
        return lower_va_arg_aapcs64(lo, e, ap);
    {
        TypeLayout l = layout_of(lo->sema, t);
        bool is_agg = lower_is_aggregate(t);
        u32 n = 1;
        bool reg_ok = true;
        /* Eightbytes PER CLASS rather than a single "is this the FP path"
         * flag. An aggregate can need both banks at once, and the flag could
         * not say so -- which is why every mixed pair used to be pushed to
         * the overflow area, where the caller had not put it. */
        i64 ngp = 0, nfp = 0;

        abi_classify_arg(lo, t, &plan);
        if (plan.kind == ABI_ARG_SCALAR) {
            IrType st = lower_irtype(lo, t);

            if (st == IRT_F32 || st == IRT_F64)
                nfp = 1;
            else if (st == IRT_F80 || st == IRT_F128)
                reg_ok = false; /* long double: always the overflow area */
            else
                ngp = 1;
        } else if (plan.kind == ABI_ARG_EIGHTBYTES) {
            u32 k;

            n = plan.n;
            for (k = 0; k < plan.n; k++) {
                if (plan.t[k] == IRT_F64)
                    nfp++;
                else
                    ngp++;
            }
        } else {
            reg_ok = false; /* MEMORY class: no register branch at all */
        }
        if (!ngp && !nfp)
            reg_ok = false;

        {
            BlockId join = lower_new_block(lo, "va.join");
            ValueId addr = ir_block_param(lo->m, lo->fn, join, IRT_PTR);
            /* SysV 3.5.7: the overflow cursor advances by the size rounded up
             * to EIGHT. Sixteen is the PRE-alignment for a type whose own
             * alignment exceeds 8, which the overflow path below applies
             * separately -- rounding the advance to 16 as well left a hole
             * before the next anonymous argument. Invisible with one MEMORY
             * vararg, because only the SECOND one reads from a cursor the
             * first moved. Both arm64 va_arg paths already round to 8. */
            u64 slot = ((u64)l.size + 7) & ~7ull;

            if (reg_ok) {
                BlockId reg = lower_new_block(lo, "va.reg");
                BlockId mem = lower_new_block(lo, "va.mem");
                IrOperand gp_off = {0};
                IrOperand fp_off = {0};
                IrOperand cond;

                /* An aggregate can need BOTH banks (`struct { double d; short
                 * s; }` is one SSE eightbyte and one INTEGER), and it goes to
                 * the registers only if there is room in each. Forcing every
                 * mixed pair to the overflow area -- the corner this file used
                 * to defer -- reads from where the caller never wrote it. */
                if (ngp) {
                    Lvalue lv;

                    memset(&lv, 0, sizeof(lv));
                    lv.addr = va_field_addr(lo, ap, 0);
                    lv.unit = IRT_I32;
                    lv.align = 4;
                    gp_off = lower_load(lo, lv);
                }
                if (nfp) {
                    Lvalue lv;

                    memset(&lv, 0, sizeof(lv));
                    lv.addr = va_field_addr(lo, ap, 4);
                    lv.unit = IRT_I32;
                    lv.align = 4;
                    fp_off = lower_load(lo, lv);
                }
                {
                    ValueId c;
                    bool have = false;

                    memset(&c, 0, sizeof(c));
                    if (ngp) {
                        c = ir_build_icmp(&lo->b, ICMP_ULE, gp_off,
                                          ir_op_iconst(IRT_I32, 48 - 8 * ngp));
                        have = true;
                    }
                    if (nfp) {
                        ValueId cf = ir_build_icmp(
                            &lo->b, ICMP_ULE, fp_off,
                            ir_op_iconst(IRT_I32, 176 - 16 * nfp));

                        c = have ? ir_build2(&lo->b, IR_AND, IRT_I32,
                                             ir_op_value(lo->fn, c),
                                             ir_op_value(lo->fn, cf))
                                 : cf;
                    }
                    cond = ir_op_value(lo->fn, c);
                }
                ir_build_condbr(&lo->b, cond, reg, NULL, 0, mem, NULL, 0);

                lower_at(lo, reg);
                {
                    Lvalue rsalv;
                    IrOperand rsa, from_reg;
                    /* Only the bank an argument actually uses is computed;
                     * zeroed so the unused one is never a stale read. */
                    IrOperand gp_base = {0};
                    IrOperand fp_base = {0};
                    i64 gp_seen = 0, fp_seen = 0;
                    u32 k;

                    memset(&rsalv, 0, sizeof(rsalv));
                    rsalv.addr = va_field_addr(lo, ap, 16);
                    rsalv.unit = IRT_PTR;
                    rsalv.align = 8;
                    rsa = lower_load(lo, rsalv);
                    if (ngp) {
                        ValueId w = ir_build1(&lo->b, IR_ZEXT, IRT_I64, gp_off);

                        gp_base = ir_op_value(
                            lo->fn, ir_build_ptradd(&lo->b, rsa,
                                                    ir_op_value(lo->fn, w)));
                    }
                    if (nfp) {
                        ValueId w = ir_build1(&lo->b, IR_ZEXT, IRT_I64, fp_off);

                        fp_base = ir_op_value(
                            lo->fn, ir_build_ptradd(&lo->b, rsa,
                                                    ir_op_value(lo->fn, w)));
                    }

                    if (n == 1) {
                        /* One eightbyte: the save area slot IS the value. */
                        from_reg = nfp ? fp_base : gp_base;
                    } else {
                        /* Several: GATHER. The GP half packs eightbytes at
                         * 8, the FP half gives each xmm its own SIXTEEN, and
                         * a mixed aggregate interleaves the two -- so the
                         * value is never contiguous in the save area and a
                         * straight 16-byte read picks up padding. AAPCS64
                         * gathers HFA leaves out of their q slots for the
                         * same reason. */
                        ValueId tmp =
                            ir_build_alloca_typed(&lo->b, lower_i64((i64)n * 8),
                                                  8, lower_efftype(lo, t));

                        for (k = 0; k < n; k++) {
                            bool kfp = plan.kind == ABI_ARG_EIGHTBYTES &&
                                       plan.t[k] == IRT_F64;
                            IrOperand sp = kfp ? fp_base : gp_base;
                            IrOperand dp = ir_op_value(lo->fn, tmp);
                            i64 soff = kfp ? fp_seen++ * 16 : gp_seen++ * 8;
                            Lvalue src, dst;
                            IrOperand word;

                            if (soff)
                                sp = ir_op_value(
                                    lo->fn, ir_build_ptradd(&lo->b, sp,
                                                            lower_i64(soff)));
                            if (k)
                                dp = ir_op_value(
                                    lo->fn,
                                    ir_build_ptradd(&lo->b, dp,
                                                    lower_i64((i64)k * 8)));
                            memset(&src, 0, sizeof(src));
                            src.addr = sp;
                            src.unit = IRT_I64;
                            src.align = 8;
                            word = lower_load(lo, src);
                            memset(&dst, 0, sizeof(dst));
                            dst.addr = dp;
                            dst.unit = IRT_I64;
                            dst.align = 8;
                            lower_store(lo, dst, word);
                        }
                        from_reg = ir_op_value(lo->fn, tmp);
                    }

                    /* Both cursors advance, each by its own class's count. */
                    if (ngp) {
                        ValueId b = ir_build2(&lo->b, IR_IADD, IRT_I32, gp_off,
                                              ir_op_iconst(IRT_I32, 8 * ngp));

                        va_store_u32(lo, ap, 0, ir_op_value(lo->fn, b));
                    }
                    if (nfp) {
                        ValueId b = ir_build2(&lo->b, IR_IADD, IRT_I32, fp_off,
                                              ir_op_iconst(IRT_I32, 16 * nfp));

                        va_store_u32(lo, ap, 4, ir_op_value(lo->fn, b));
                    }
                    ir_build_br(&lo->b, join, &from_reg, 1);
                }
                lower_at(lo, mem);
            }
            /* The overflow path (straight-line when no register branch). */
            {
                Lvalue ovlv;
                IrOperand ov;
                ValueId nov;
                IrOperand from_mem;

                memset(&ovlv, 0, sizeof(ovlv));
                ovlv.addr = va_field_addr(lo, ap, 8);
                ovlv.unit = IRT_PTR;
                ovlv.align = 8;
                ov = lower_load(lo, ovlv);
                if (l.align > 8) {
                    /* Align the cursor up before reading (16-aligned types:
                     * long double and 16-aligned aggregates). */
                    ValueId as_i = ir_build1(&lo->b, IR_BITCAST, IRT_I64, ov);
                    ValueId up = ir_build2(&lo->b, IR_IADD, IRT_I64,
                                           ir_op_value(lo->fn, as_i),
                                           lower_i64((i64)l.align - 1));
                    ValueId masked = ir_build2(&lo->b, IR_AND, IRT_I64,
                                               ir_op_value(lo->fn, up),
                                               lower_i64(-(i64)l.align));
                    ValueId back = ir_build1(&lo->b, IR_BITCAST, IRT_PTR,
                                             ir_op_value(lo->fn, masked));

                    ov = ir_op_value(lo->fn, back);
                }
                nov = ir_build_ptradd(&lo->b, ov, lower_i64((i64)slot));
                {
                    Lvalue novlv;

                    memset(&novlv, 0, sizeof(novlv));
                    novlv.addr = va_field_addr(lo, ap, 8);
                    novlv.unit = IRT_PTR;
                    novlv.align = 8;
                    lower_store(lo, novlv, ir_op_value(lo->fn, nov));
                }
                from_mem = ov;
                ir_build_br(&lo->b, join, &from_mem, 1);
            }
            lower_at(lo, join);
            if (is_agg) {
                ValueId tmp = lower_temp(lo, t);

                lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp),
                                       ir_op_value(lo->fn, addr), t,
                                       (u32)(l.align > 8 ? 8 : l.align), 0);
                return ir_op_value(lo->fn, tmp);
            }
            {
                Lvalue lv;

                memset(&lv, 0, sizeof(lv));
                lv.addr = ir_op_value(lo->fn, addr);
                lv.unit = lower_irtype(lo, t);
                lv.align = (u32)(l.align > 8 ? 8 : l.align);
                lv.is_signed = type_is_integer(t) && is_signed_ty(lo, t);
                return lower_load(lo, lv);
            }
        }
    }
}

static void lower_va_builtin(Lower *lo, AstNode *e)
{
    IrOperand ap = lower_va_cursor(lo, e->args[0]);

    switch (e->op) {
    case SEMA_BUILTIN_VA_START:
        if (!lo->cur_functype || !lo->cur_functype->variadic) {
            if (!lo->failed)
                diag_emit(lo->dc, DIAG_ERROR, e->span,
                          "'va_start' used in a function with a fixed "
                          "argument list");
            lo->failed = true;
            return;
        }
        if (lo->sema->target.kind == CGF_TARGET_ARM64_MACOS) {
            /* Nothing to seed. Apple's va_list holds ONE cursor and every
             * anonymous argument is already on the stack, so va_start is
             * exactly "point at the first of them" -- which only codegen
             * knows the address of, and ir_build_va_start below asks for. */
        } else if (lower_is_aapcs64(lo)) {
            /* Negative, counting up toward zero: the unused tail of each
             * save area. Eight registers per bank, the named parameters
             * having already consumed their share. */
            i64 gp = (i64)(lo->named_gp > 8 ? 8 : lo->named_gp);
            i64 fp = (i64)(lo->named_fp > 8 ? 8 : lo->named_fp);

            va_store_u32(lo, ap, VA64_GR_OFFS,
                         ir_op_iconst(IRT_I32, -(8 * (8 - gp))));
            va_store_u32(lo, ap, VA64_VR_OFFS,
                         ir_op_iconst(IRT_I32, -(16 * (8 - fp))));
        } else {
            va_store_u32(
                lo, ap, 0,
                ir_op_iconst(IRT_I32,
                             8 * (i64)(lo->named_gp > 6 ? 6 : lo->named_gp)));
            va_store_u32(
                lo, ap, 4,
                ir_op_iconst(
                    IRT_I32,
                    48 + 16 * (i64)(lo->named_fp > 8 ? 8 : lo->named_fp)));
        }
        ir_build_va_start(&lo->b, ap);
        return;
    case SEMA_BUILTIN_VA_COPY: {
        IrOperand src = lower_va_cursor(lo, e->args[1]);

        /* The whole record; both save areas stay shared. AAPCS64's is 32
         * bytes (three pointers and two offsets), SysV's 24. Apple's is one
         * pointer -- and copying it is the ONLY way to get an independent
         * cursor there, since its va_list is not an array and assignment
         * would alias rather than decay. */
        ir_build_memcpy(
            &lo->b, ap, src,
            lower_i64(lo->sema->target.kind == CGF_TARGET_ARM64_MACOS ? 8
                      : lower_is_aapcs64(lo)                          ? 32
                                                                      : 24),
            8, 0);
        return;
    }
    default: /* va_end: the SysV va_end is a no-op — nothing at all */
        return;
    }
}

/* The Sprint 28 builtins that are NOT calls: each lowers to IR we
 * already have. The mem/str family deliberately does NOT appear here —
 * v0.1.0 lowers those to real libc calls (inline expansion is a
 * Phase 7/11 optimization, and pre-optimizing it here would make the
 * -O0 output untrue to the source). */
static bool lower_simple_builtin(Lower *lo, AstNode *e, IrOperand *out)
{
    switch (e->op) {
    case SEMA_BUILTIN_UNREACHABLE:
        ir_build_unreachable(&lo->b);
        /* unreachable TERMINATES the block, but the C statement it came
         * from can be followed by more (dead) code — open a fresh block
         * so lowering has somewhere to put it; ir_func_remove_unreachable
         * drops it before the verifier ever sees it. */
        lower_at(lo, lower_new_block(lo, "dead"));
        *out = ir_op_undef(IRT_I32);
        return true;
    case SEMA_BUILTIN_TRAP:
        /* Both trap and unreachable emit ud2 today (gcc emits ud2 for
         * __builtin_trap too), so they share the IR opcode. They are NOT
         * the same concept — unreachable is UB-if-reached and an
         * optimizer may delete code around it, while trap is a DEFINED
         * stop — so when Phase 7 starts reasoning about unreachable,
         * trap needs its own opcode. Recorded rather than pre-built:
         * the emitted code is correct today. */
        ir_build_unreachable(&lo->b);
        lower_at(lo, lower_new_block(lo, "dead"));
        *out = ir_op_undef(IRT_I32);
        return true;
    case SEMA_BUILTIN_EXPECT:
        /* Honest no-op in v0.1.0 (documented): the value IS the first
         * argument. Branch weights arrive with the optimizer, and a
         * fake metadata bit now would be a claim we cannot honor. */
        *out = lower_rvalue(lo, e->args[0]);
        return true;
    case SEMA_BUILTIN_ALLOCA: {
        /* 16-byte alignment: max_align_t's, so the block is usable for
         * any object the caller puts there. Sprint 20's dynamic alloca
         * machinery (stacksave/restore tokens) handles the frame. */
        IrOperand n = lower_rvalue(lo, e->args[0]);

        *out = ir_op_value(lo->b.f, ir_build_alloca(&lo->b, n, 16));
        return true;
    }
    case SEMA_BUILTIN_CONSTANT_P:
        /* gcc's contract: 0 when the answer is not known. We answer
         * from the constant engine at THIS point in compilation, so a
         * value that only becomes constant after inlining reads 0 —
         * documented, and the same answer gcc gives at -O0. */
        {
            ConstValue cv = constexpr_eval(lo->sema, e->args[0], CE_FOLD);

            *out = ir_op_iconst(
                IRT_I32, (cv.kind == CV_INT || cv.kind == CV_FLOAT) ? 1 : 0);
        }
        return true;
    /* IEEE bit patterns, written as bits — never as a host double
     * (the no-host-FPU law: these are the same values Sprint 15's
     * softfloat produces, and a host literal would be a second source
     * of truth). __builtin_nan("") is the default quiet NaN; a
     * non-empty payload string is not supported and sema rejects it. */
    case SEMA_BUILTIN_HUGE_VAL:
    case SEMA_BUILTIN_INF:
        *out = ir_op_fconst(IRT_F64, 0x7ff0000000000000ULL, 0);
        return true;
    case SEMA_BUILTIN_HUGE_VALF:
    case SEMA_BUILTIN_INFF:
        *out = ir_op_fconst(IRT_F32, 0x7f800000ULL, 0);
        return true;
    case SEMA_BUILTIN_NAN:
        *out = ir_op_fconst(IRT_F64, 0x7ff8000000000000ULL, 0);
        return true;
    case SEMA_BUILTIN_NANF:
        *out = ir_op_fconst(IRT_F32, 0x7fc00000ULL, 0);
        return true;
    }
    return false;
}

/* memcpy/memmove/memset/memcmp/strlen: a direct external call by name.
 * All five take and return only scalars, so the abstract-call machinery
 * (aggregate copies, sret) is not needed here. */
static IrOperand lower_libc_builtin(Lower *lo, AstNode *e)
{
    static const struct {
        u16 marker;
        const char *name;
        IrType ret;
    } libc[] = {
        {SEMA_BUILTIN_MEMCPY, "memcpy", IRT_PTR},
        {SEMA_BUILTIN_MEMMOVE, "memmove", IRT_PTR},
        {SEMA_BUILTIN_MEMSET, "memset", IRT_PTR},
        {SEMA_BUILTIN_MEMCMP, "memcmp", IRT_I32},
        {SEMA_BUILTIN_STRLEN, "strlen", IRT_I64},
    };
    IrOperand args[3];
    u32 i, n = 0;
    size_t k;

    for (k = 0; k < CGF_ARRAY_LEN(libc); k++) {
        if (libc[k].marker != e->op)
            continue;
        for (i = 0; i < e->nargs && i < 3; i++)
            args[n++] = lower_rvalue(lo, e->args[i]);
        return ir_op_value(lo->fn,
                           ir_build_call(&lo->b, libc[k].ret, FUNCREF_EXTERNAL,
                                         ir_sym(lo->m, libc[k].name), args, n));
    }
    CGF_ICE("builtin %#x has no lowering", (unsigned)e->op);
}

static IrOperand lower_call(Lower *lo, AstNode *e)
{
    Symbol *callee;
    Type *fty = NULL;
    Type *ret;
    AbiRet aret;
    AbiBudget budget;
    bool hidden;
    bool stacked = false;
    IrOperand fp;
    IrOperand args[130];
    u32 attr_ir_args[130] = {0};
    u32 nargs = 0;
    ValueId sret_tmp = VALUE_INVALID;
    ValueId rv;
    u32 i;
    bool call_noreturn = false;

    /* The va_* builtins carry sema's marker instead of a callee. */
    if (e->op >= SEMA_BUILTIN_VA_START && e->op <= SEMA_BUILTIN_VA_COPY) {
        lower_va_builtin(lo, e);
        return ir_op_undef(IRT_I32); /* void; no one may look */
    }
    if (e->op >= SEMA_BUILTIN_FIRST) {
        IrOperand bo;

        if (lower_simple_builtin(lo, e, &bo))
            return bo;
        /* The mem/str builtins ARE their libc functions in v0.1.0
         * (inline expansion is Phase 7/11). They have no Symbol — sema
         * recognized the name without declaring anything — so the call
         * is built directly against the libc symbol NAME. */
        return lower_libc_builtin(lo, e);
    }

    callee = direct_callee(e->lhs);
    if (callee) {
        static const char *const known[] = {
            "abort", "exit", "_Exit", "quick_exit", "longjmp", "siglongjmp"};
        size_t ni;

        call_noreturn = (callee->func_specs & AST_FS_NORETURN) != 0;
        for (ni = 0; !call_noreturn && callee->linkage == LINK_EXTERNAL &&
                     ni < CGF_ARRAY_LEN(known);
             ni++)
            call_noreturn = strcmp(callee->name, known[ni]) == 0;
        if (!call_noreturn && callee->linkage == LINK_EXTERNAL &&
            lo->sema->target.kind == CGF_TARGET_X86_64_FREEBSD)
            call_noreturn = strcmp(callee->name, "err") == 0 ||
                            strcmp(callee->name, "errx") == 0;
    }
    /* The callee's function type: through the symbol, or through the
     * called pointer's pointee. */
    if (callee)
        fty = callee->type;
    else if (sem(e->lhs) && sem(e->lhs)->kind == TY_PTR)
        fty = sem(e->lhs)->base;
    ret = fty ? fty->base : sem(e);
    abi_classify_ret(lo, ret, &aret);
    hidden = aret.kind == ABI_RET_SRET || aret.kind == ABI_RET_PAIR ||
             aret.kind == ABI_RET_HFA;

    /* Left-to-right: the callee expression evaluates before any
     * argument (only observable for indirect calls). */
    memset(&fp, 0, sizeof(fp));
    if (!callee)
        fp = lower_rvalue(lo, e->lhs);

    if (hidden) {
        sret_tmp = lower_temp(lo, ret);
        args[nargs] = ir_op_value(lo->fn, sret_tmp);
        args[nargs].b = aret.arg_annot == IR_ARG_HFA
                            ? ir_arg_annot_hfa(aret.size, aret.n)
                            : ir_arg_annot(aret.arg_annot, aret.size);
        nargs++;
    }
    abi_budget_init(lo, &budget, &aret);
    for (i = 0; i < e->nargs && nargs + 2 <= 130; i++) {
        AstNode *a = e->args[i];
        AbiArg plan;
        IrOperand av = lower_rvalue(lo, a);
        /* The named/anonymous boundary is knowable ONLY here: the IR records
         * that a callee is variadic, never where its prototype stopped. An
         * unprototyped callee has no boundary to record. */
        u8 anon = fty && fty->variadic && fty->has_proto && i >= fty->nparams
                      ? (u8)IROPF_ANON
                      : 0u;
        u32 first_arg = nargs;

        /* Signedness of a sub-int argument, for the one ABI that makes the
         * caller widen it (see IROPF_SEXT). Default argument promotion has
         * already run, so an anonymous argument is never narrow -- this
         * only ever fires on a declared parameter. */
        if (sem(a) && type_is_integer(sem(a)) &&
            layout_of(lo->sema, sem(a)).size < 4)
            anon |= conv_is_signed(lo->sema, sem(a)) ? (u8)IROPF_SEXT
                                                     : (u8)IROPF_ZEXT;

        attr_ir_args[i] = nargs + 1;

        abi_classify_arg(lo, sem(a), &plan);
        /* Placement depends on what earlier arguments already spent, so this
         * may turn a register-class aggregate into a stacked one. The
         * DEFINITION walk in lower.c runs the identical sequence -- if the
         * two ever diverge, the halves of a call disagree silently. */
        abi_arg_place(lo, &plan, &budget, (anon & (u8)IROPF_ANON) != 0);
        if (plan.kind == ABI_ARG_SCALAR) {
            IrType st = lower_irtype(lo, sem(a));

            if (st == IRT_F32 || st == IRT_F64)
                budget.fp++;
            else if (st != IRT_F80 && st != IRT_F128)
                budget.gp++;
        }
        /* A stacked aggregate travels as eightbyte leaves on AAPCS64 (which
         * is what abi_arg_place re-planned it into) and as a byval pointer
         * on SysV, where that spelling already means
         * by-value-on-the-stack. */
        if (plan.kind == ABI_ARG_STACK) {
            plan.kind =
                (u8)(lower_is_aapcs64(lo) ? ABI_ARG_EIGHTBYTES : ABI_ARG_BYVAL);
            stacked = true;
        } else {
            stacked = false;
        }
        switch (plan.kind) {
        case ABI_ARG_EIGHTBYTES:
        case ABI_ARG_HFA: {
            /* The value travels as N bit-carrying scalars. Stage through a
             * temp rounded to N strides so the last load never reads past
             * the object. The STRIDE is the leaf width: 8 for an eightbyte
             * pair, 4 or 8 for an HFA whose leaves are floats or doubles.
             * Using 8 for an HFA of floats would gather every other leaf. */
            u64 stride =
                plan.kind == ABI_ARG_HFA ? (u64)ir_type_size(plan.t[0]) : 8u;
            u64 rounded = (u64)plan.n * stride;
            ValueId tmp = ir_build_alloca_typed(&lo->b, lower_i64((i64)rounded),
                                                plan.align > 8 ? plan.align : 8,
                                                lower_efftype(lo, sem(a)));
            u32 k;

            lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), av, sem(a),
                                   plan.align, 0);
            for (k = 0; k < plan.n; k++) {
                Lvalue lv;
                IrOperand addr = ir_op_value(lo->fn, tmp);

                if (k) {
                    ValueId p2 =
                        ir_build_ptradd(&lo->b, ir_op_value(lo->fn, tmp),
                                        lower_i64((i64)(stride * k)));

                    addr = ir_op_value(lo->fn, p2);
                }
                memset(&lv, 0, sizeof(lv));
                lv.addr = addr;
                lv.unit = plan.t[k];
                lv.etype = lower_efftype(lo, sem(a));
                lv.align = (u32)stride;
                args[nargs++] = lower_load(lo, lv);
            }
            break;
        }
        case ABI_ARG_BYVAL:
        case ABI_ARG_STACK: {
            /* THE mandatory call-site copy: the callee may scribble on
             * its parameter, and without a fresh temporary that scribble
             * lands on the caller's object. The pointer is IR-level
             * bookkeeping; byval(N) tells codegen to copy the pointee
             * onto the stack.
             *
             * STACK shares the shape and adds `onstack`, which is inert on
             * SysV (byval already means by-value-on-the-stack there) and is
             * the whole distinction on AAPCS64, where a plain byval passes
             * the ADDRESS in a general register instead. */
            ValueId tmp = lower_temp(lo, sem(a));

            lower_memcpy_aggregate(lo, ir_op_value(lo->fn, tmp), av, sem(a),
                                   plan.align, 0);
            args[nargs] = ir_op_value(lo->fn, tmp);
            args[nargs].b = ir_arg_annot(IR_ARG_BYVAL, plan.size);
            if (plan.kind == ABI_ARG_STACK)
                args[nargs].argflags |= (u8)IROPF_ONSTACK;
            nargs++;
            break;
        }
        default:
            args[nargs++] = av;
            break;
        }
        /* One C argument can become several operands (an EIGHTBYTES or HFA
         * aggregate); every one of them shares the argument's provenance.
         * A stacked aggregate must mark ALL its leaves, or the backend
         * places the first few in registers and splits the very thing this
         * is here to keep whole. */
        if (anon || stacked)
            for (; first_arg < nargs; first_arg++)
                args[first_arg].argflags |=
                    (u8)(anon | (stacked ? (u8)IROPF_ONSTACK : 0u));
    }

    {
        IrType irret = aret.kind == ABI_RET_SCALAR  ? lower_irtype(lo, ret)
                       : aret.kind == ABI_RET_SMALL ? aret.small_t
                                                    : IRT_VOID;

        if (callee) {
            u32 fidx;

            if (lower_internal_func(lo, callee, &fidx)) {
                rv = ir_build_call(&lo->b, irret, FUNCREF_INTERNAL, fidx, args,
                                   nargs);
            } else {
                u32 symidx = lower_global_sym(lo, callee);

                /* Keep the validated declaration contract beside the
                 * external symbol.  A later unannotated redeclaration must
                 * not erase an earlier contract for the same symbol. */
                if (callee->cgf_attrs && !lo->m->sym_cgf_attrs[symidx])
                    lo->m->sym_cgf_attrs[symidx] = lower_clone_cgf_attrs(
                        lo, callee->cgf_attrs, attr_ir_args, i);
                /* The blunt setjmp policy: calling any of the family
                 * marks the whole function (see IrFunc.calls_setjmp). */
                if (strcmp(callee->name, "setjmp") == 0 ||
                    strcmp(callee->name, "sigsetjmp") == 0 ||
                    strcmp(callee->name, "_setjmp") == 0)
                    lo->fn->calls_setjmp = true;
                rv = ir_build_call(&lo->b, irret, FUNCREF_EXTERNAL, symidx,
                                   args, nargs);
            }
        } else {
            rv = ir_build_call_indirect(&lo->b, irret, fp, args, nargs);
        }
        /* AL protocol (Sprint 23): only the front end knows the callee's
         * C type is variadic; the call instruction carries the fact. */
        if (fty && fty->variadic)
            ir_call_mark_variadic(&lo->b);
        if (call_noreturn)
            ir_call_mark_noreturn(&lo->b);
    }

    if (hidden)
        return ir_op_value(lo->fn, sret_tmp);
    if (aret.kind == ABI_RET_SMALL) {
        /* The eightbyte came back as a scalar; give the expression layer
         * the ADDRESS it expects for an aggregate value. */
        ValueId tmp = ir_build_alloca_typed(&lo->b, lower_i64(8), 8,
                                            lower_efftype(lo, ret));
        Lvalue lv;

        memset(&lv, 0, sizeof(lv));
        lv.addr = ir_op_value(lo->fn, tmp);
        lv.unit = aret.small_t;
        lv.etype = lower_efftype(lo, ret);
        lv.align = 8;
        lower_store(lo, lv, ir_op_value(lo->fn, rv));
        return ir_op_value(lo->fn, tmp);
    }
    if (!ret || ret->kind == TY_VOID)
        return ir_op_undef(IRT_I32);
    return ir_op_value(lo->fn, rv);
}

/* --- unary -----------------------------------------------------------------
 */

static IrOperand lower_incdec(Lower *lo, AstNode *e)
{
    Lvalue lv = lower_lvalue(lo, e->lhs);
    Type *t = sem(e->lhs);
    IrOperand old;

    if (lv.is_atomic && t->kind != TY_PTR) {
        u16 op = e->op == PUNCT_PLUSPLUS ? PUNCT_PLUS : PUNCT_MINUS;
        IrOperand one = type_is_fp(t)
                            ? ir_op_iconst(IRT_I32, 1) /* converted below */
                            : ir_op_iconst(lower_irtype(lo, t), 1);

        return lower_atomic_update(lo, lv, t, op, one,
                                   type_is_fp(t) ? type_basic(TY_INT) : t,
                                   e->is_postfix);
    }
    old = lower_load(lo, lv);
    IrOperand nv;
    bool inc = e->op == PUNCT_PLUSPLUS;

    if (t->kind == TY_PTR) {
        TypeLayout el = layout_of(lo->sema, t->base);
        ValueId r = ir_build_ptradd(
            &lo->b, old, lower_i64(inc ? (i64)el.size : -(i64)el.size));

        nv = ir_op_value(lo->fn, r);
    } else if (type_is_fp(t)) {
        Sf one;
        SfStatus st;
        SfFormat f = constexpr_format_of(lo->sema, t);
        uint8_t bits[16];
        u64 lop = 0, hip = 0;
        int k;

        one = sf_from_int(1, false, f, &st);
        sf_to_bits(one, f, bits);
        for (k = 0; k < 8; k++)
            lop |= (u64)bits[k] << (k * 8);
        for (k = 8; k < 16; k++)
            hip |= (u64)bits[k] << ((k - 8) * 8);
        {
            ValueId r =
                ir_build2(&lo->b, inc ? IR_FADD : IR_FSUB, lower_irtype(lo, t),
                          old, ir_op_fconst(lower_irtype(lo, t), lop, hip));

            nv = ir_op_value(lo->fn, r);
        }
    } else {
        ValueId r =
            build_source_arith(lo, inc ? IR_IADD : IR_ISUB, lower_irtype(lo, t),
                               old, ir_op_iconst(lower_irtype(lo, t), 1), t);

        nv = ir_op_value(lo->fn, r);
    }
    {
        IrOperand stored = lower_store(lo, lv, nv);

        return e->is_postfix ? old : stored;
    }
}

static IrOperand lower_unary(Lower *lo, AstNode *e)
{
    switch (e->op) {
    case PUNCT_AMP: {
        Lvalue lv = lower_lvalue(lo, e->lhs);

        return lv.addr;
    }
    case PUNCT_STAR: {
        Lvalue lv;

        if (lower_is_aggregate(sem(e)) || (sem(e) && sem(e)->kind == TY_FUNC)) {
            /* *f on a function or *p on an aggregate: the VALUE is the
             * address itself. */
            return lower_rvalue(lo, e->lhs);
        }
        lv = lower_lvalue(lo, e);
        {
            Span saved = ir_builder_span(&lo->b);
            IrOperand value;

            /* Flow diagnostics belong on the scalar read, not on the
             * enclosing statement span installed by lower_stmt(). */
            ir_builder_set_span(&lo->b, e->span);
            value = lower_load(lo, lv);
            ir_builder_set_span(&lo->b, saved);
            return value;
        }
    }
    case PUNCT_PLUS:
        return lower_rvalue(lo, e->lhs);
    case PUNCT_MINUS: {
        IrOperand v = lower_rvalue(lo, e->lhs);
        Type *t = sem(e);
        ValueId r;

        if (type_is_fp(t))
            r = ir_build1(&lo->b, IR_FNEG, lower_irtype(lo, t), v);
        else
            r = build_source_arith(lo, IR_ISUB, lower_irtype(lo, t),
                                   ir_op_iconst(lower_irtype(lo, t), 0), v, t);
        return ir_op_value(lo->fn, r);
    }
    case PUNCT_TILDE: {
        IrOperand v = lower_rvalue(lo, e->lhs);
        ValueId r = ir_build2(&lo->b, IR_XOR, lower_irtype(lo, sem(e)), v,
                              ir_op_iconst(lower_irtype(lo, sem(e)), -1));

        return ir_op_value(lo->fn, r);
    }
    case PUNCT_BANG: {
        /* !x is x == 0 — one compare, i32 result. */
        IrOperand v = lower_rvalue(lo, e->lhs);
        Type *t = sem(e->lhs);
        ValueId r;

        if (type_is_fp(t))
            r = ir_build_fcmp(&lo->b, FCMP_OEQ, v, fp_zero(lo, t));
        else
            r = ir_build_icmp(&lo->b, ICMP_EQ, v,
                              ir_op_iconst((IrType)v.type, 0));
        return ir_op_value(lo->fn, r);
    }
    case PUNCT_PLUSPLUS:
    case PUNCT_MINUSMINUS:
        return lower_incdec(lo, e);
    default:
        CGF_ICE("lower_unary: unhandled op %u", e->op);
    }
}

/* --- rvalue dispatch -------------------------------------------------------
 */

IrOperand lower_rvalue(Lower *lo, AstNode *e)
{
    switch (e->kind) {
    case AST_EXPR_INT:
    case AST_EXPR_CHAR:
        return ir_op_iconst(lower_irtype(lo, sem(e)),
                            (i64)(e->tok ? e->tok->int_val : 0));
    case AST_EXPR_FLOAT: {
        SfFormat f = constexpr_format_of(lo->sema, sem(e));
        Sf v = constexpr_float_literal(lo->sema, e);
        uint8_t bits[16];
        u64 lop = 0, hip = 0;
        int k;

        sf_to_bits(v, f, bits);
        for (k = 0; k < 8; k++)
            lop |= (u64)bits[k] << (k * 8);
        for (k = 8; k < 16; k++)
            hip |= (u64)bits[k] << ((k - 8) * 8);
        return ir_op_fconst(lower_irtype(lo, sem(e)), lop, hip);
    }
    case AST_EXPR_PAREN:
        return lower_rvalue(lo, e->lhs);
    case AST_EXPR_IDENT: {
        Symbol *sym = e->sym;
        Lvalue lv;

        if (sym && sym->kind == SYM_ENUM_CONST)
            return ir_op_iconst(lower_irtype(lo, sem(e)), sym->enum_value);
        if (sym && sym->kind == SYM_FUNC)
            return ir_op_symbol(IRT_PTR, lower_global_sym(lo, sym), 0);
        if (lower_is_aggregate(sem(e)) || (sem(e) && sem(e)->kind == TY_FUNC)) {
            lv = lower_lvalue(lo, e);
            return lv.addr;
        }
        lv = lower_lvalue(lo, e);
        {
            Span saved = ir_builder_span(&lo->b);
            IrOperand value;

            ir_builder_set_span(&lo->b, e->span);
            value = lower_load(lo, lv);
            if (sym && lo->initializing_sym == sym)
                ir_load_mark_self_init(&lo->b);
            ir_builder_set_span(&lo->b, saved);
            return value;
        }
    }
    case AST_EXPR_STRING: {
        u32 s = lower_string_lit(lo, e);

        return ir_op_symbol(IRT_PTR, s, 0);
    }
    case AST_EXPR_CAST: {
        Type *to = sem(e);
        Type *from = sem(e->lhs);

        /* Array/function decay: the "value" of the operand is already an
         * address; the cast is a no-op re-labelling. */
        if (from && (from->kind == TY_ARRAY || from->kind == TY_FUNC))
            return lower_rvalue(lo, e->lhs);
        if (lower_is_aggregate(to))
            return lower_rvalue(lo, e->lhs);
        {
            IrOperand v = lower_rvalue(lo, e->lhs);

            return lower_scalar_convert(lo, v, from, to);
        }
    }
    case AST_EXPR_UNARY:
        return lower_unary(lo, e);
    case AST_EXPR_BINARY:
        if (e->op == PUNCT_ASSIGN ||
            (e->op >= PUNCT_STAR_ASSIGN && e->op <= PUNCT_PIPE_ASSIGN))
            return lower_assign(lo, e);
        return lower_binary(lo, e);
    case AST_EXPR_COND:
        return lower_ternary(lo, e);
    case AST_EXPR_CALL:
        return lower_call(lo, e);
    case AST_EXPR_INDEX:
    case AST_EXPR_MEMBER: {
        Lvalue lv = lower_lvalue(lo, e);

        if (lower_is_aggregate(sem(e)))
            return lv.addr;
        return lower_load(lo, lv);
    }
    case AST_EXPR_COMPOUND_LIT: {
        Lvalue lv = lower_lvalue(lo, e);

        if (lower_is_aggregate(sem(e)))
            return lv.addr;
        return lower_load(lo, lv);
    }
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ALIGNOF: {
        ConstValue cv = constexpr_eval(lo->sema, e, CE_FOLD);

        if (cv.kind != CV_INT) {
            /* sizeof(VLA): the DECLARED vla's size was evaluated once at
             * declaration and cached (never re-evaluated — the fixture
             * with a side-effecting bound pins it); a bare VLA type-name
             * evaluates here, which is C17's rule. */
            Type *vt = e->lhs ? sem(e->lhs)
                              : sema_type_from_ast(lo->sema, e->type, e->span);

            if (vt && vt->is_vla)
                return lower_type_size(lo, vt);
            return ir_op_undef(IRT_I64);
        }
        return ir_op_iconst(lower_irtype(lo, sem(e)), (i64)cv.i);
    }
    case AST_EXPR_GENERIC:
        /* Sema selected the arm and parked it in `mid`. */
        return lower_rvalue(lo, e->mid);
    case AST_EXPR_VA_ARG:
        return lower_va_arg(lo, e);
    case AST_EXPR_OFFSETOF: {
        /* Always an ICE — sema typed it and the folder computed the
         * byte offset, so there is nothing to evaluate at run time. */
        ConstValue cv = constexpr_eval(lo->sema, e, CE_ICE);

        if (cv.kind != CV_INT)
            CGF_ICE("offsetof did not fold at lowering");
        return ir_op_iconst(lower_irtype(lo, sem(e)), (i64)cv.i);
    }
    default:
        CGF_ICE("lower_rvalue: unhandled expr kind %d", (int)e->kind);
    }
}

/* --- conditions ------------------------------------------------------------
 */

IrOperand lower_cond(Lower *lo, AstNode *e)
{
    /* Fold the double compare: anything that already yields 0/1 i32 is
     * used as-is. */
    switch (e->kind) {
    case AST_EXPR_PAREN:
        return lower_cond(lo, e->lhs);
    case AST_EXPR_BINARY:
        if (is_cmp_op(e->op) || e->op == PUNCT_AMPAMP ||
            e->op == PUNCT_PIPEPIPE)
            return lower_rvalue(lo, e);
        break;
    case AST_EXPR_UNARY:
        if (e->op == PUNCT_BANG)
            return lower_rvalue(lo, e);
        break;
    case AST_EXPR_CAST:
        /* Sema's conv_to_bool wrapper: the truth test IS the conversion;
         * test the operand directly rather than materializing a _Bool. */
        if (e->implicit && sem(e) && sem(e)->kind == TY_BOOL)
            return lower_cond(lo, e->lhs);
        break;
    default:
        break;
    }
    {
        IrOperand v = lower_rvalue(lo, e);

        return truth_ne(lo, v, sem(e));
    }
}
