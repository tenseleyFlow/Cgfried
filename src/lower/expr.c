#include "lower/lower.h"

#include <stdio.h>
#include <string.h>

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
        /* Left-to-right: the base pointer first, then the index. */
        IrOperand base = lower_rvalue(lo, e->lhs);
        IrOperand idx = lower_rvalue(lo, e->rhs);
        TypeLayout el = layout_of(lo->sema, sem(e));
        IrOperand wide =
            lower_scalar_convert(lo, idx, sem(e->rhs), type_basic(TY_LONG));
        ValueId scaled =
            ir_build2(&lo->b, IR_IMUL, IRT_I64, wide, lower_i64((i64)el.size));
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

        ir_build_memcpy(&lo->b, ir_op_value(lo->fn, agg_tmp), v,
                        lower_i64((i64)l.size), (u32)l.align, 0);
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

        ir_build_memcpy(&lo->b, ir_op_value(lo->fn, agg_tmp), v,
                        lower_i64((i64)l.size), (u32)l.align, 0);
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
            TypeLayout el = layout_of(lo->sema, lt->base);
            ValueId ai = ir_build1(&lo->b, IR_BITCAST, IRT_I64, a);
            ValueId bi = ir_build1(&lo->b, IR_BITCAST, IRT_I64, b);
            ValueId d =
                ir_build2(&lo->b, IR_ISUB, IRT_I64, ir_op_value(lo->fn, ai),
                          ir_op_value(lo->fn, bi));
            ValueId q =
                ir_build2(&lo->b, IR_SDIV, IRT_I64, ir_op_value(lo->fn, d),
                          lower_i64((i64)(el.size ? el.size : 1)));

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

            ir_build_memcpy(&lo->b, lv.addr, src, lower_i64((i64)l.size),
                            (u32)l.align, lv.is_volatile ? IRF_VOLATILE : 0);
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

static IrOperand lower_va_arg(Lower *lo, AstNode *e)
{
    Type *t = sem(e);
    IrOperand ap = lower_rvalue(lo, e->lhs);
    AbiArg plan;
    TypeLayout l = layout_of(lo->sema, t);
    bool is_agg = lower_is_aggregate(t);
    bool fp_path = false;
    u32 n = 1;
    bool reg_ok = true;

    abi_classify_arg(lo, t, &plan);
    if (plan.kind == ABI_ARG_SCALAR) {
        IrType st = lower_irtype(lo, t);

        if (st == IRT_F32 || st == IRT_F64)
            fp_path = true;
        else if (st == IRT_F80 || st == IRT_F128)
            reg_ok = false; /* long double: always the overflow area */
    } else if (plan.kind == ABI_ARG_EIGHTBYTES) {
        bool all_int = true;
        bool all_sse = true;
        u32 k;

        n = plan.n;
        for (k = 0; k < plan.n; k++) {
            if (plan.t[k] == IRT_F64)
                all_int = false;
            else
                all_sse = false;
        }
        if (all_sse)
            fp_path = true;
        else if (!all_int)
            reg_ok = false; /* mixed eightbytes: overflow (corner —
                               noted for Sprint 50's revisit) */
    } else {
        reg_ok = false; /* MEMORY class: no register branch at all */
    }

    {
        BlockId join = lower_new_block(lo, "va.join");
        ValueId addr = ir_block_param(lo->m, lo->fn, join, IRT_PTR);
        i64 field = fp_path ? 4 : 0;
        i64 bump = fp_path ? 16 * (i64)n : 8 * (i64)n;
        i64 limit = fp_path ? 176 - 16 * (i64)n : 48 - 8 * (i64)n;
        u64 slot = l.size <= 8 ? 8 : (l.size + 15) & ~15ull;

        if (reg_ok) {
            BlockId reg = lower_new_block(lo, "va.reg");
            BlockId mem = lower_new_block(lo, "va.mem");
            Lvalue offlv;
            IrOperand off;

            memset(&offlv, 0, sizeof(offlv));
            offlv.addr = va_field_addr(lo, ap, field);
            offlv.unit = IRT_I32;
            offlv.align = 4;
            off = lower_load(lo, offlv);
            {
                ValueId c = ir_build_icmp(&lo->b, ICMP_ULE, off,
                                          ir_op_iconst(IRT_I32, limit));

                ir_build_condbr(&lo->b, ir_op_value(lo->fn, c), reg, NULL, 0,
                                mem, NULL, 0);
            }
            lower_at(lo, reg);
            {
                Lvalue rsalv;
                IrOperand rsa;
                ValueId wide, sum, bumped;
                IrOperand from_reg;

                memset(&rsalv, 0, sizeof(rsalv));
                rsalv.addr = va_field_addr(lo, ap, 16);
                rsalv.unit = IRT_PTR;
                rsalv.align = 8;
                rsa = lower_load(lo, rsalv);
                wide = ir_build1(&lo->b, IR_ZEXT, IRT_I64, off);
                sum = ir_build_ptradd(&lo->b, rsa, ir_op_value(lo->fn, wide));
                bumped = ir_build2(&lo->b, IR_IADD, IRT_I32, off,
                                   ir_op_iconst(IRT_I32, bump));
                va_store_u32(lo, ap, field, ir_op_value(lo->fn, bumped));
                from_reg = ir_op_value(lo->fn, sum);
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
                ValueId masked =
                    ir_build2(&lo->b, IR_AND, IRT_I64, ir_op_value(lo->fn, up),
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

            ir_build_memcpy(&lo->b, ir_op_value(lo->fn, tmp),
                            ir_op_value(lo->fn, addr), lower_i64((i64)l.size),
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

static void lower_va_builtin(Lower *lo, AstNode *e)
{
    IrOperand ap = lower_rvalue(lo, e->args[0]);

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
        va_store_u32(
            lo, ap, 0,
            ir_op_iconst(IRT_I32,
                         8 * (i64)(lo->named_gp > 6 ? 6 : lo->named_gp)));
        va_store_u32(
            lo, ap, 4,
            ir_op_iconst(IRT_I32,
                         48 + 16 * (i64)(lo->named_fp > 8 ? 8 : lo->named_fp)));
        ir_build_va_start(&lo->b, ap);
        return;
    case SEMA_BUILTIN_VA_COPY: {
        IrOperand src = lower_rvalue(lo, e->args[1]);

        /* The whole 24-byte record; the register save area is shared. */
        ir_build_memcpy(&lo->b, ap, src, lower_i64(24), 8, 0);
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
    bool hidden;
    IrOperand fp;
    IrOperand args[130];
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
    hidden = aret.kind == ABI_RET_SRET || aret.kind == ABI_RET_PAIR;

    /* Left-to-right: the callee expression evaluates before any
     * argument (only observable for indirect calls). */
    memset(&fp, 0, sizeof(fp));
    if (!callee)
        fp = lower_rvalue(lo, e->lhs);

    if (hidden) {
        sret_tmp = lower_temp(lo, ret);
        args[nargs] = ir_op_value(lo->fn, sret_tmp);
        args[nargs].b = ir_arg_annot(aret.arg_annot, aret.size);
        nargs++;
    }
    for (i = 0; i < e->nargs && nargs + 2 <= 130; i++) {
        AstNode *a = e->args[i];
        AbiArg plan;
        IrOperand av = lower_rvalue(lo, a);

        abi_classify_arg(lo, sem(a), &plan);
        switch (plan.kind) {
        case ABI_ARG_EIGHTBYTES: {
            /* The value travels as 1-2 bit-carrying scalars. Stage
             * through an eightbyte-rounded temp so the second load never
             * reads past the object. */
            u64 rounded = (u64)plan.n * 8;
            ValueId tmp = ir_build_alloca_typed(&lo->b, lower_i64((i64)rounded),
                                                plan.align > 8 ? plan.align : 8,
                                                lower_efftype(lo, sem(a)));
            u32 k;

            ir_build_memcpy(&lo->b, ir_op_value(lo->fn, tmp), av,
                            lower_i64((i64)plan.size), plan.align, 0);
            for (k = 0; k < plan.n; k++) {
                Lvalue lv;
                IrOperand addr = ir_op_value(lo->fn, tmp);

                if (k) {
                    ValueId p2 = ir_build_ptradd(
                        &lo->b, ir_op_value(lo->fn, tmp), lower_i64(8));

                    addr = ir_op_value(lo->fn, p2);
                }
                memset(&lv, 0, sizeof(lv));
                lv.addr = addr;
                lv.unit = plan.t[k];
                lv.etype = lower_efftype(lo, sem(a));
                lv.align = 8;
                args[nargs++] = lower_load(lo, lv);
            }
            break;
        }
        case ABI_ARG_BYVAL: {
            /* THE mandatory call-site copy: the callee may scribble on
             * its parameter, and without a fresh temporary that scribble
             * lands on the caller's object. The pointer is IR-level
             * bookkeeping; byval(N) tells codegen to copy the pointee
             * onto the stack. */
            ValueId tmp = lower_temp(lo, sem(a));

            ir_build_memcpy(&lo->b, ir_op_value(lo->fn, tmp), av,
                            lower_i64((i64)plan.size), plan.align, 0);
            args[nargs] = ir_op_value(lo->fn, tmp);
            args[nargs].b = ir_arg_annot(IR_ARG_BYVAL, plan.size);
            nargs++;
            break;
        }
        default:
            args[nargs++] = av;
            break;
        }
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
                /* The blunt setjmp policy: calling any of the family
                 * marks the whole function (see IrFunc.calls_setjmp). */
                if (strcmp(callee->name, "setjmp") == 0 ||
                    strcmp(callee->name, "sigsetjmp") == 0 ||
                    strcmp(callee->name, "_setjmp") == 0)
                    lo->fn->calls_setjmp = true;
                rv = ir_build_call(&lo->b, irret, FUNCREF_EXTERNAL,
                                   lower_global_sym(lo, callee), args, nargs);
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
