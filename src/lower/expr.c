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
    return lv->is_volatile ? IRF_VOLATILE : 0;
}

IrOperand lower_load(Lower *lo, Lvalue lv)
{
    ValueId raw;

    raw = ir_build_load(&lo->b, lv.unit, lv.addr, lv.align, lv_flags(&lv));
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
        ir_build_store(&lo->b, v, lv.addr, lv.align, lv_flags(&lv));
        return v;
    }
    {
        /* Read-modify-write; and the RESULT of the assignment is the
         * re-narrowed stored value, not the incoming RHS. */
        u32 unit_bits = 8u << (lv.unit - IRT_I8);
        u64 mask = lv.bit_width >= 64 ? ~0ull : ((1ull << lv.bit_width) - 1);
        ValueId old =
            ir_build_load(&lo->b, lv.unit, lv.addr, lv.align, lv_flags(&lv));
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

        ir_build_store(&lo->b, ir_op_value(lo->fn, ins), lv.addr, lv.align,
                       lv_flags(&lv));
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
    if (t && (t->quals & CGF_QUAL_VOLATILE))
        lv.is_volatile = true;
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
        if (sem(e) && (sem(e)->quals & CGF_QUAL_ATOMIC))
            lower_unimplemented(lo, e->span, "an _Atomic object access", 20);
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
    TypeLayout el = layout_of(lo->sema, ptr_ty->base);
    IrOperand wide = lower_scalar_convert(lo, n, idx_ty, type_basic(TY_LONG));
    ValueId scaled =
        ir_build2(&lo->b, IR_IMUL, IRT_I64, wide, lower_i64((i64)el.size));
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
        ValueId r = ir_build2(&lo->b, arith_op_for(lo, e->op, sem(e)), t, a, b);

        return ir_op_value(lo->fn, r);
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
        IrOperand old = lower_load(lo, lv);
        IrOperand rhs = lower_rvalue(lo, e->rhs);

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
            r = ir_build2(&lo->b, arith_op_for(lo, op, common),
                          lower_irtype(lo, common), a, b2);
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

static IrOperand lower_call(Lower *lo, AstNode *e)
{
    Symbol *callee = direct_callee(e->lhs);
    Type *fty = NULL;
    Type *ret;
    bool agg_ret;
    IrOperand fp;
    IrOperand args[66];
    u32 nargs = 0;
    ValueId sret_tmp = VALUE_INVALID;
    ValueId rv;
    u32 i;

    /* The callee's function type: through the symbol, or through the
     * called pointer's pointee. */
    if (callee)
        fty = callee->type;
    else if (sem(e->lhs) && sem(e->lhs)->kind == TY_PTR)
        fty = sem(e->lhs)->base;
    ret = fty ? fty->base : sem(e);
    agg_ret = lower_is_aggregate(ret);

    /* Left-to-right: the callee expression evaluates before any
     * argument (only observable for indirect calls). */
    memset(&fp, 0, sizeof(fp));
    if (!callee)
        fp = lower_rvalue(lo, e->lhs);

    if (agg_ret) {
        sret_tmp = lower_temp(lo, ret);
        args[nargs++] = ir_op_value(lo->fn, sret_tmp);
    }
    for (i = 0; i < e->nargs && nargs < 66; i++) {
        AstNode *a = e->args[i];
        IrOperand av = lower_rvalue(lo, a);

        if (lower_is_aggregate(sem(a))) {
            /* THE mandatory call-site copy: the callee may scribble on
             * its parameter, and without a fresh temporary that scribble
             * lands on the caller's object. */
            ValueId tmp = lower_temp(lo, sem(a));
            TypeLayout l = layout_of(lo->sema, sem(a));

            ir_build_memcpy(&lo->b, ir_op_value(lo->fn, tmp), av,
                            lower_i64((i64)l.size), (u32)l.align, 0);
            args[nargs++] = ir_op_value(lo->fn, tmp);
        } else {
            args[nargs++] = av;
        }
    }

    {
        IrType irret = agg_ret || !ret || ret->kind == TY_VOID
                           ? IRT_VOID
                           : lower_irtype(lo, ret);

        if (callee) {
            u32 fidx;

            /* A variadic INTERNAL callee is still called through its
             * SYMBOL: the verifier's internal-call arity check has no
             * notion of "at least N", and the fixed-vs-variadic split is
             * Sprint 19's ABI business anyway. */
            if (lower_internal_func(lo, callee, &fidx) &&
                !(fty && fty->variadic)) {
                rv = ir_build_call(&lo->b, irret, FUNCREF_INTERNAL, fidx, args,
                                   nargs);
            } else {
                rv = ir_build_call(&lo->b, irret, FUNCREF_EXTERNAL,
                                   lower_global_sym(lo, callee), args, nargs);
            }
        } else {
            rv = ir_build_call_indirect(&lo->b, irret, fp, args, nargs);
        }
    }

    if (agg_ret)
        return ir_op_value(lo->fn, sret_tmp);
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
    IrOperand old = lower_load(lo, lv);
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
            ir_build2(&lo->b, inc ? IR_IADD : IR_ISUB, lower_irtype(lo, t), old,
                      ir_op_iconst(lower_irtype(lo, t), 1));

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
        return lower_load(lo, lv);
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
            r = ir_build2(&lo->b, IR_ISUB, lower_irtype(lo, t),
                          ir_op_iconst(lower_irtype(lo, t), 0), v);
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
        return lower_load(lo, lv);
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
            /* sizeof(VLA) is a runtime value — Sprint 20's. */
            lower_unimplemented(lo, e->span,
                                "sizeof of a variable-length "
                                "array",
                                20);
            return ir_op_undef(IRT_I64);
        }
        return ir_op_iconst(lower_irtype(lo, sem(e)), (i64)cv.i);
    }
    case AST_EXPR_GENERIC:
        /* Sema selected the arm and parked it in `mid`. */
        return lower_rvalue(lo, e->mid);
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
