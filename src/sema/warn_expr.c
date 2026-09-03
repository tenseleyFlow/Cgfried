#include "sema/warn_expr.h"

#include <limits.h>

#include "warn/warn.h"

static bool is_comparison(u16 op)
{
    return op == PUNCT_LT || op == PUNCT_GT || op == PUNCT_LE ||
           op == PUNCT_GE || op == PUNCT_EQEQ || op == PUNCT_NOTEQ;
}

static bool is_assignment(u16 op)
{
    return op >= PUNCT_ASSIGN && op <= PUNCT_PIPE_ASSIGN;
}

static AstNode *strip_implicit(AstNode *e)
{
    while (e && e->kind == AST_EXPR_CAST && e->implicit && e->lhs)
        e = e->lhs;
    return e;
}

static AstNode *strip_parens(AstNode *e)
{
    while (e && e->kind == AST_EXPR_PAREN && e->lhs)
        e = e->lhs;
    return e;
}

static AstNode *source_expr(AstNode *e)
{
    AstNode *old;

    do {
        old = e;
        e = strip_implicit(e);
        e = strip_parens(e);
    } while (e != old);
    return e;
}

static bool has_explicit_cast(AstNode *e)
{
    e = strip_parens(e);
    while (e && e->kind == AST_EXPR_CAST && e->implicit)
        e = strip_parens(e->lhs);
    return e && e->kind == AST_EXPR_CAST && !e->implicit;
}

static bool int_constant(Sema *s, AstNode *e, ConstValue *out)
{
    ConstValue v;

    if (!e || !e->sem_type || !type_is_integer(e->sem_type))
        return false;
    v = constexpr_eval(s, e, CE_FOLD);
    if (v.kind != CV_INT)
        return false;
    if (out)
        *out = v;
    return true;
}

static u64 low_mask(u32 bits)
{
    return bits >= 64 ? UINT64_MAX : ((1ull << bits) - 1);
}

static i64 signed_value(Sema *s, const ConstValue *v)
{
    u32 bits = conv_int_bits(s, v->type);
    u64 value = v->i & low_mask(bits);

    if (bits && bits < 64 && (value & (1ull << (bits - 1))))
        value |= ~low_mask(bits);
    return (i64)value;
}

static bool constant_nonnegative(Sema *s, AstNode *e)
{
    ConstValue v;

    if (!int_constant(s, e, &v))
        return false;
    return !conv_is_signed(s, v.type) || signed_value(s, &v) >= 0;
}

static bool constant_fits(Sema *s, const ConstValue *v, Type *to)
{
    u32 bits = conv_int_bits(s, to);
    bool from_signed = conv_is_signed(s, v->type);
    bool to_signed = conv_is_signed(s, to);
    i64 sv = signed_value(s, v);

    if (!bits)
        return false;
    if (!to_signed) {
        if (from_signed && sv < 0)
            return false;
        return bits >= 64 || v->i <= low_mask(bits);
    }
    if (from_signed && sv < 0) {
        i64 min = bits >= 64 ? INT64_MIN : -(i64)(1ull << (bits - 1));

        return sv >= min;
    }
    return bits >= 64 || v->i <= ((1ull << (bits - 1)) - 1);
}

/* GCC's -Woverflow asks whether an integer constant fits either value range
 * representable by the destination width, not only the destination's signed
 * range.  For example, 0x80000000UL -> int preserves a 32-bit pattern and is
 * a -Wsign-conversion concern; 0x100000000UL -> int loses bits and is an
 * overflow.  Negative constants analogously fit down to the signed minimum. */
static bool constant_fits_width(Sema *s, const ConstValue *v, Type *to)
{
    u32 bits = conv_int_bits(s, to);
    bool from_signed = conv_is_signed(s, v->type);
    i64 sv = signed_value(s, v);

    if (!bits)
        return false;
    if (from_signed && sv < 0) {
        i64 min = bits >= 64 ? INT64_MIN : -(i64)(1ull << (bits - 1));

        return sv >= min;
    }
    return bits >= 64 || v->i <= low_mask(bits);
}

static bool mask_bounds_conversion(Sema *s, AstNode *e, Type *to)
{
    ConstValue mask;

    e = source_expr(e);
    if (!e || e->kind != AST_EXPR_BINARY || e->op != PUNCT_AMP)
        return false;
    if (int_constant(s, source_expr(e->lhs), &mask) &&
        constant_fits(s, &mask, to))
        return true;
    return int_constant(s, source_expr(e->rhs), &mask) &&
           constant_fits(s, &mask, to);
}

static i64 converted_signed_value(Sema *s, const ConstValue *v, Type *to)
{
    ConstValue converted = *v;

    converted.type = to;
    converted.i &= low_mask(conv_int_bits(s, to));
    return signed_value(s, &converted);
}

static Type *promoted_original_type(Sema *s, AstNode *e)
{
    e = source_expr(e);
    if (!e || !e->sem_type)
        return NULL;
    if (e->sem_is_bitfield)
        return conv_promote_bitfield_type(s, e->sem_type, e->sem_bitfield_width,
                                          e->sem_bitfield_is_signed);
    return type_is_integer(e->sem_type) ? conv_promote_type(s, e->sem_type)
                                        : e->sem_type;
}

static bool bool_comparison_constant_bad(Sema *s, AstNode *e)
{
    ConstValue v;

    return int_constant(s, e, &v) && v.i > 1;
}

static bool same_value_expr(AstNode *a, AstNode *b)
{
    a = source_expr(a);
    b = source_expr(b);
    if (!a || !b || a->kind != b->kind)
        return false;
    if (a->kind == AST_EXPR_IDENT)
        return a->sym && a->sym == b->sym;
    return false;
}

static bool has_side_effect(const AstNode *e)
{
    u32 i;

    if (!e)
        return false;
    if (e->kind == AST_EXPR_CALL || e->kind == AST_EXPR_VA_ARG)
        return true;
    if (e->kind == AST_EXPR_UNARY &&
        (e->op == PUNCT_PLUSPLUS || e->op == PUNCT_MINUSMINUS))
        return true;
    if (e->kind == AST_EXPR_BINARY && is_assignment(e->op))
        return true;
    if (e->kind == AST_EXPR_SIZEOF && !e->unevaluated)
        return true; /* sizeof(VLA) evaluates its bound. */
    if (e->is_lvalue && e->sem_type && (e->sem_type->quals & CGF_QUAL_VOLATILE))
        return true;
    if (has_side_effect(e->lhs) || has_side_effect(e->mid) ||
        has_side_effect(e->rhs))
        return true;
    for (i = 0; i < e->nargs; i++)
        if (has_side_effect(e->args[i]))
            return true;
    return false;
}

static void warn_unused_value(Sema *s, AstNode *e)
{
    AstNode *written = source_expr(e);

    if (!written || written->poisoned || has_side_effect(written) ||
        (written->sem_type && written->sem_type->kind == TY_VOID))
        return;
    warn_at(s->lang->warnings, WARN_UNUSED_VALUE, written->span,
            "expression result unused");
}

/* True when an expression's result is a BOOLEAN 0 or 1 by construction: the
 * logical and relational operators all yield int 0 or int 1 (6.5.3.3p5,
 * 6.5.8p6, 6.5.9p3, 6.5.13p3, 6.5.14p3), so the value can never be negative
 * however it is compared.
 *
 * gcc suppresses -Wsign-compare on these, and musl's fopencookie.c leans on
 * it directly: `remain > !!f->buf_size` with an unsigned `remain`. */
static bool boolean_valued(AstNode *e)
{
    e = source_expr(e);
    if (!e)
        return false;
    if (e->kind == AST_EXPR_UNARY && e->op == PUNCT_BANG)
        return true;
    if (e->kind != AST_EXPR_BINARY)
        return false;
    switch (e->op) {
    case PUNCT_LT:
    case PUNCT_GT:
    case PUNCT_LE:
    case PUNCT_GE:
    case PUNCT_EQEQ:
    case PUNCT_NOTEQ:
    case PUNCT_AMPAMP:
    case PUNCT_PIPEPIPE:
        return true;
    default:
        return false;
    }
}

/* True when an operand's value is provably non-negative because PROMOTION
 * put it there: an unsigned type narrower than int widens to int without
 * changing its range, so `unsigned char` spans 0..255 and `unsigned short`
 * 0..65535 -- every value representable, none of them negative.
 *
 * gcc suppresses -Wsign-compare on exactly this, and musl leans on it: the
 * whole of intscan.c compares `val[c]` (an unsigned char through a pointer)
 * against an `unsigned base`. Seven of those in one file were the largest
 * cluster the zero-false-positive musl gate reported when extended asm made
 * the file parse.
 *
 * The test is the ORIGINAL type, not the promoted one -- promoted_original_
 * type already returns the promoted type, which is where the information
 * that it was ever unsigned is lost. */
static bool promoted_from_narrow_unsigned(Sema *s, AstNode *e)
{
    Type *t;

    e = source_expr(e);
    if (!e || !e->sem_type)
        return false;
    t = e->sem_type;
    if (!type_is_integer(t) || conv_is_signed(s, t))
        return false;
    /* Narrower than int, so promotion cannot make it negative. */
    return layout_of(s, t).size < layout_of(s, type_basic(TY_INT)).size;
}

/* A conservative value range for a small class of integer expressions, in
 * i64. Returns false for "unknown"; a true result means the expression's
 * value is provably within [*lo, *hi].
 *
 * THIS EXISTS TO PROVE NON-NEGATIVITY, nothing more, which is why the
 * catalogue is deliberately tiny: a narrow type's own width, a folded
 * constant, a 0/1-valued logical result, and the operators that combine
 * them without changing sign. gcc reaches the same conclusions through
 * full value-range propagation; we need only the corner that keeps
 * -Wsign-compare quiet on code that is obviously fine.
 *
 * Both musl sites that motivated it are here:
 *   iconv.c    `c < 4*map[-1]`         -- a product of a narrow unsigned
 *   strftime.c `d >= (*p=='C'?3:5)`    -- a conditional of two constants
 *
 * Bounds are capped at +/-2^31 so a product cannot overflow i64; anything
 * wider is reported unknown rather than guessed. */
#define RANGE_CAP 2147483647LL

static bool int_range(Sema *s, AstNode *e, i64 *lo, i64 *hi, u32 depth)
{
    ConstValue cv;
    i64 alo, ahi, blo, bhi;
    Type *t;

    if (!e || depth > 8)
        return false;
    e = source_expr(e);
    if (!e || !e->sem_type)
        return false;

    if (int_constant(s, e, &cv)) {
        i64 v = conv_is_signed(s, cv.type) ? signed_value(s, &cv) : (i64)cv.i;

        if (v < -RANGE_CAP || v > RANGE_CAP)
            return false;
        *lo = *hi = v;
        return true;
    }
    if (boolean_valued(e)) {
        *lo = 0;
        *hi = 1;
        return true;
    }
    if (e->kind == AST_EXPR_BINARY) {
        switch (e->op) {
        case PUNCT_PLUS:
        case PUNCT_STAR:
            if (!int_range(s, e->lhs, &alo, &ahi, depth + 1) ||
                !int_range(s, e->rhs, &blo, &bhi, depth + 1))
                return false;
            if (e->op == PUNCT_PLUS) {
                *lo = alo + blo;
                *hi = ahi + bhi;
            } else {
                i64 p[4];
                u32 i;

                p[0] = alo * blo;
                p[1] = alo * bhi;
                p[2] = ahi * blo;
                p[3] = ahi * bhi;
                *lo = *hi = p[0];
                for (i = 1; i < 4; i++) {
                    if (p[i] < *lo)
                        *lo = p[i];
                    if (p[i] > *hi)
                        *hi = p[i];
                }
            }
            return *lo >= -RANGE_CAP && *hi <= RANGE_CAP;
        default:
            return false;
        }
    }
    if (e->kind == AST_EXPR_COND) {
        /* The join of the two arms. `c ? 3 : 5` is [3,5]. */
        if (!int_range(s, e->mid ? e->mid : e->lhs, &alo, &ahi, depth + 1) ||
            !int_range(s, e->rhs, &blo, &bhi, depth + 1))
            return false;
        *lo = alo < blo ? alo : blo;
        *hi = ahi > bhi ? ahi : bhi;
        return true;
    }
    /* An UNSIGNED type narrower than int spans 0..2^n-1 and promotes to a
     * signed int that still cannot be negative -- the original rule, now one
     * case among several. */
    t = e->sem_type;
    if (type_is_integer(t) && !conv_is_signed(s, t)) {
        u32 bits = conv_int_bits(s, t);

        if (bits == 0 || bits >= 32)
            return false;
        *lo = 0;
        *hi = (i64)1 << bits;
        return true;
    }
    return false;
}

/* The whole point of int_range: is this operand certainly not negative? */
static bool provably_nonnegative(Sema *s, AstNode *e)
{
    i64 lo, hi;

    if (promoted_from_narrow_unsigned(s, e) || boolean_valued(e))
        return true;
    return int_range(s, e, &lo, &hi, 0) && lo >= 0;
}

static void warn_sign_compare(Sema *s, AstNode *e)
{
    Type *lt = promoted_original_type(s, e->lhs);
    Type *rt = promoted_original_type(s, e->rhs);
    Type *common = e->lhs ? e->lhs->sem_type : NULL;
    ConstValue constant;

    if (!lt || !rt || !type_is_integer(lt) || !type_is_integer(rt) ||
        conv_is_signed(s, lt) == conv_is_signed(s, rt))
        return;
    /* When UAC converts the unsigned operand into a wider signed type, no
     * signed value changes domain (LP64 `long` versus `unsigned int`). */
    if (common && type_is_integer(common) && conv_is_signed(s, common))
        return;
    if (conv_is_signed(s, lt) && constant_nonnegative(s, source_expr(e->lhs)))
        return;
    if (conv_is_signed(s, rt) && constant_nonnegative(s, source_expr(e->rhs)))
        return;
    if (conv_is_signed(s, lt) && provably_nonnegative(s, e->lhs))
        return;
    if (conv_is_signed(s, rt) && provably_nonnegative(s, e->rhs))
        return;
    /* GCC suppresses equality/inequality when the unsigned side is a
     * constant representable by the signed side.  Relational comparisons
     * deliberately retain the warning because their ordering semantics are
     * still changed by UAC. */
    if (e->op == PUNCT_EQEQ || e->op == PUNCT_NOTEQ) {
        if (conv_is_signed(s, lt) &&
            int_constant(s, source_expr(e->rhs), &constant) &&
            constant_fits(s, &constant, lt))
            return;
        if (conv_is_signed(s, rt) &&
            int_constant(s, source_expr(e->lhs), &constant) &&
            constant_fits(s, &constant, rt))
            return;
    }
    warn_at(s->lang->warnings, WARN_SIGN_COMPARE, e->span,
            "comparison of integer expressions of different signedness: "
            "'%s' and '%s'",
            type_to_str(s->arena, lt), type_to_str(s->arena, rt));
}

static void warn_char_subscript(Sema *s, AstNode *e)
{
    AstNode *idx = source_expr(e->rhs);
    AstNode *base = source_expr(e->lhs);

    if (base && base->sem_type && type_is_integer(base->sem_type)) {
        idx = base;
        base = source_expr(e->rhs);
    }
    (void)base;
    if (idx && idx->sem_type && idx->sem_type->kind == TY_CHAR &&
        !int_constant(s, idx, NULL))
        warn_at(s->lang->warnings, WARN_CHAR_SUBSCRIPTS, idx->span,
                "array subscript has type 'char'");
}

static bool underwent_signed_to_unsigned_uac(Sema *s, AstNode *converted)
{
    AstNode *written = source_expr(converted);
    Type *from = written ? written->sem_type : NULL;
    Type *to = converted ? converted->sem_type : NULL;
    ConstValue cv;

    if (!from || !to || !type_is_integer(from) || !type_is_integer(to) ||
        !conv_is_signed(s, from) || conv_is_signed(s, to))
        return false;
    /* A nonnegative constant keeps its mathematical value after UAC, so
     * it can still prove facts such as `unsigned_value >= 0`. */
    if (int_constant(s, written, &cv) && signed_value(s, &cv) >= 0)
        return false;
    return true;
}

static Type *pointer_operand_type(AstNode *e)
{
    /* Keep the implicit array/function-to-pointer conversion: its source
     * expression has array/function type, but arithmetic acts on the
     * materialized pointer type.  Only written parentheses are transparent
     * here. */
    e = strip_parens(e);
    if (!e || !e->sem_type)
        return NULL;
    if (e->sem_type->kind == TY_PTR)
        return e->sem_type;
    if (e->sem_type->kind == TY_ARRAY)
        return e->sem_type;
    return NULL;
}

static void warn_pointer_arith(Sema *s, AstNode *e)
{
    Type *p = pointer_operand_type(e->lhs);

    if (!p)
        p = pointer_operand_type(e->rhs);
    if (!p || !p->base ||
        (p->base->kind != TY_VOID && p->base->kind != TY_FUNC))
        return;
    warn_pedwarn_at(s->lang->warnings, WARN_POINTER_ARITH, e->span,
                    "pointer of type '%s' used in arithmetic",
                    type_to_str(s->arena, p));
}

static void warn_type_limits(Sema *s, AstNode *e)
{
    AstNode *var = source_expr(e->lhs);
    AstNode *constant = source_expr(e->rhs);
    Type *vt;
    ConstValue cv;
    u16 op = e->op;
    bool always = false;
    bool truth = false;
    u32 bits;
    bool sign;
    i64 c;

    if (e->span.origin & SPAN_ORIGIN_ANY_MACRO)
        return;
    /* Once UAC has moved a signed operand into the unsigned domain, its
     * original signed range cannot prove the comparison constant.  GCC
     * leaves those cases to -Wsign-compare. */
    if (underwent_signed_to_unsigned_uac(s, e->lhs) ||
        underwent_signed_to_unsigned_uac(s, e->rhs))
        return;
    if (!int_constant(s, constant, &cv)) {
        var = source_expr(e->rhs);
        constant = source_expr(e->lhs);
        if (!int_constant(s, constant, &cv))
            return;
        if (op == PUNCT_LT)
            op = PUNCT_GT;
        else if (op == PUNCT_GT)
            op = PUNCT_LT;
        else if (op == PUNCT_LE)
            op = PUNCT_GE;
        else if (op == PUNCT_GE)
            op = PUNCT_LE;
    }
    if (!var || !var->sem_type || !type_is_integer(var->sem_type) ||
        int_constant(s, var, NULL))
        return;
    vt = var->sem_type;
    bits = conv_int_bits(s, vt);
    sign = conv_is_signed(s, vt);
    c = signed_value(s, &cv);

    if (!sign && c == 0) {
        if (op == PUNCT_GE || op == PUNCT_NOTEQ) {
            always = op == PUNCT_GE;
            truth = true;
        } else if (op == PUNCT_LT) {
            always = true;
            truth = false;
        }
    }
    if (bits < 64) {
        i64 min = sign ? -(i64)(1ull << (bits - 1)) : 0;
        u64 max = sign ? ((1ull << (bits - 1)) - 1) : low_mask(bits);

        if (conv_is_signed(s, cv.type) && c < min) {
            always = true;
            truth = op == PUNCT_GT || op == PUNCT_GE || op == PUNCT_NOTEQ;
        } else if (cv.i > max && (!conv_is_signed(s, cv.type) || c >= 0)) {
            always = true;
            truth = op == PUNCT_LT || op == PUNCT_LE || op == PUNCT_NOTEQ;
        }
    }
    if (always)
        warn_at_ex(s->lang->warnings, WARN_TYPE_LIMITS, e->span,
                   WARN_SUPPRESS_IN_MACRO,
                   "comparison is always %s due to limited range of data "
                   "type",
                   truth ? "true" : "false");
}

static void warn_truth_expr(Sema *s, AstNode *e)
{
    AstNode *written = strip_implicit(e);
    AstNode *plain = strip_parens(written);

    if (!written || !plain)
        return;
    if (plain->kind == AST_EXPR_BINARY && is_assignment(plain->op) &&
        written->kind != AST_EXPR_PAREN)
        warn_at(s->lang->warnings, WARN_PARENTHESES, plain->span,
                "suggest parentheses around assignment used as truth value");
    plain = source_expr(e);
    if (plain && plain->kind == AST_EXPR_IDENT && plain->sym &&
        (plain->sym->kind == SYM_FUNC ||
         (plain->sym->type && plain->sym->type->kind == TY_ARRAY)))
        warn_at(s->lang->warnings, WARN_ADDRESS, plain->span,
                "the address of '%s' will never be NULL", plain->name);
}

static void warn_comparison(Sema *s, AstNode *e)
{
    AstNode *lhs = source_expr(e->lhs);
    AstNode *rhs = source_expr(e->rhs);
    Type *lt = lhs ? lhs->sem_type : NULL;
    Type *rt = rhs ? rhs->sem_type : NULL;

    warn_sign_compare(s, e);
    warn_type_limits(s, e);
    if ((e->op == PUNCT_EQEQ || e->op == PUNCT_NOTEQ) &&
        ((lt && lt->kind == TY_BOOL && bool_comparison_constant_bad(s, rhs)) ||
         (rt && rt->kind == TY_BOOL && bool_comparison_constant_bad(s, lhs))))
        warn_at(s->lang->warnings, WARN_BOOL_COMPARE, e->span,
                "comparison of constant with boolean expression is always "
                "false");
    /* IEEE operands may be NaNs, so even x == x is not constant. */
    if (!(lt && type_is_floating(lt)) && !(rt && type_is_floating(rt)) &&
        same_value_expr(lhs, rhs))
        warn_at_ex(s->lang->warnings, WARN_TAUTOLOGICAL_COMPARE, e->span,
                   WARN_SUPPRESS_IN_MACRO,
                   "self-comparison always evaluates "
                   "to a constant");
    if (lhs && lhs->kind == AST_EXPR_UNARY && lhs->op == PUNCT_BANG &&
        strip_implicit(e->lhs)->kind != AST_EXPR_PAREN &&
        !(rhs && rhs->kind == AST_EXPR_UNARY && rhs->op == PUNCT_BANG))
        warn_at(s->lang->warnings, WARN_LOGICAL_NOT_PARENTHESES, e->span,
                "logical not is only applied to the left hand side of "
                "comparison");
}

static void warn_precedence(Sema *s, AstNode *e)
{
    AstNode *lhs = strip_implicit(e->lhs);
    AstNode *rhs = strip_implicit(e->rhs);

    if (e->op == PUNCT_PIPEPIPE) {
        if (lhs && lhs->kind == AST_EXPR_BINARY && lhs->op == PUNCT_AMPAMP)
            warn_at(s->lang->warnings, WARN_PARENTHESES, lhs->span,
                    "suggest parentheses around '&&' within '||'");
        else if (rhs && rhs->kind == AST_EXPR_BINARY && rhs->op == PUNCT_AMPAMP)
            warn_at(s->lang->warnings, WARN_PARENTHESES, rhs->span,
                    "suggest parentheses around '&&' within '||'");
    }
    if (e->op == PUNCT_SHL || e->op == PUNCT_SHR) {
        if (lhs && lhs->kind == AST_EXPR_BINARY &&
            (lhs->op == PUNCT_PLUS || lhs->op == PUNCT_MINUS))
            warn_at(s->lang->warnings, WARN_PARENTHESES, lhs->span,
                    "suggest parentheses around arithmetic inside shift");
        if (rhs && rhs->kind == AST_EXPR_BINARY &&
            (rhs->op == PUNCT_PLUS || rhs->op == PUNCT_MINUS))
            warn_at(s->lang->warnings, WARN_PARENTHESES, rhs->span,
                    "suggest parentheses around arithmetic inside shift");
    }
}

/* `nonnull`: a null pointer CONSTANT passed where the callee said one may
 * never go. Only a constant -- proving a runtime pointer non-null is the
 * memory-safety engine's job, not a syntactic warning's.
 *
 * The bare form means every POINTER parameter, so it needs the callee's
 * prototype; the listed form does not, but using the same walk for both
 * keeps one notion of "argument N". */
/* A null pointer VALUE, which is deliberately wider than 6.3.2.3p3's null
 * pointer CONSTANT. `(int *)0` is not an NPC by the standard's definition
 * -- only a zero ICE, or one cast to `void *`, qualifies -- yet gcc warns
 * for it here, measured. Both spellings mean the same thing to a reader, so
 * stripping an explicit pointer cast before the NPC test is what matches.
 *
 * conv_is_npc itself stays strict: its other caller decides the TYPE of a
 * conditional expression, where the standard's narrow rule is the correct
 * one. Widening it there would change what `c ? (int *)0 : 1` means. */
static bool arg_is_null_pointer(Sema *s, AstNode *a)
{
    AstNode *core = strip_implicit(a);

    while (core && core->kind == AST_EXPR_CAST && core->sem_type &&
           core->sem_type->kind == TY_PTR)
        core = strip_implicit(core->lhs);
    return core && conv_is_npc(s, core);
}

static void warn_nonnull(Sema *s, AstNode *e)
{
    AstNode *callee = strip_implicit(e->lhs);
    Type *ft;
    Symbol *sym;
    u32 i;

    if (!callee || callee->kind != AST_EXPR_IDENT)
        return;
    sym = callee->sym;
    if (!sym || (!sym->gnu.nonnull_all && !sym->gnu.nonnull_mask))
        return;
    ft = callee->sem_type;
    if (ft && ft->kind == TY_PTR)
        ft = ft->base;
    for (i = 0; i < e->nargs && i < 64; i++) {
        bool listed = (sym->gnu.nonnull_mask >> i) & 1u;

        if (!listed && sym->gnu.nonnull_all) {
            /* The bare form applies to POINTER parameters only, so it needs
             * the prototype to know which those are. */
            listed = ft && ft->kind == TY_FUNC && ft->has_proto &&
                     i < ft->nparams && ft->params[i] &&
                     ft->params[i]->kind == TY_PTR;
        }
        if (!listed || !e->args[i])
            continue;
        if (!arg_is_null_pointer(s, e->args[i]))
            continue;
        warn_at(s->lang->warnings, WARN_NONNULL, e->args[i]->span,
                "argument %u null where non-null expected", (unsigned)(i + 1));
    }
}

/* `warn_unused_result`: the callee asked to be told when its value is
 * thrown away.
 *
 * THE CAST TO VOID DOES NOT SUPPRESS IT. `(void)must()` still warns in gcc,
 * which is the opposite of -Wunused-value, where the cast IS the author's
 * acknowledgement. So this looks THROUGH a cast to void rather than
 * treating one as consent -- and that asymmetry is the whole reason this
 * cannot be folded into warn_unused_value. */
static void warn_unused_result(Sema *s, AstNode *e)
{
    AstNode *core = strip_implicit(e);
    AstNode *callee;
    Symbol *sym;

    while (core && (core->kind == AST_EXPR_PAREN ||
                    (core->kind == AST_EXPR_CAST && core->sem_type &&
                     core->sem_type->kind == TY_VOID)))
        core = strip_implicit(core->lhs);
    if (!core || core->kind != AST_EXPR_CALL)
        return;
    callee = strip_implicit(core->lhs);
    if (!callee || callee->kind != AST_EXPR_IDENT)
        return;
    sym = callee->sym;
    if (!sym || !sym->gnu.warn_unused_result)
        return;
    warn_at(s->lang->warnings, WARN_UNUSED_RESULT, core->span,
            "ignoring return value of '%s' declared with attribute "
            "'warn_unused_result'",
            sym->name ? sym->name : "?");
}

static void check_expr(Sema *s, AstNode *e, unsigned context)
{
    u32 i;

    if (!e || e->poisoned || !e->sem_type)
        return;
    if (context & SEMA_WARN_EXPR_TRUTH)
        warn_truth_expr(s, e);
    if (context & SEMA_WARN_EXPR_DISCARDED) {
        warn_unused_value(s, e);
        warn_unused_result(s, e);
    }

    switch (e->kind) {
    case AST_EXPR_UNARY:
        if ((e->op == PUNCT_PLUSPLUS || e->op == PUNCT_MINUSMINUS) &&
            pointer_operand_type(e->lhs))
            warn_pointer_arith(s, e);
        check_expr(s, e->lhs, SEMA_WARN_EXPR_VALUE);
        break;
    case AST_EXPR_BINARY:
        if (is_comparison(e->op))
            warn_comparison(s, e);
        if (e->op == PUNCT_ASSIGN && e->lhs && e->lhs->sem_type)
            sema_warn_implicit_conversion(s, e->lhs->sem_type, e->rhs);
        if (e->op == PUNCT_PLUS || e->op == PUNCT_MINUS)
            warn_pointer_arith(s, e);
        warn_precedence(s, e);
        check_expr(s, e->lhs,
                   e->op == PUNCT_COMMA ? SEMA_WARN_EXPR_DISCARDED
                                        : SEMA_WARN_EXPR_VALUE);
        check_expr(s, e->rhs,
                   (e->op == PUNCT_AMPAMP || e->op == PUNCT_PIPEPIPE)
                       ? SEMA_WARN_EXPR_TRUTH
                       : SEMA_WARN_EXPR_VALUE);
        break;
    case AST_EXPR_COND:
        check_expr(s, e->lhs, SEMA_WARN_EXPR_TRUTH);
        check_expr(s, e->mid, SEMA_WARN_EXPR_VALUE);
        check_expr(s, e->rhs, SEMA_WARN_EXPR_VALUE);
        if (e->lhs && strip_implicit(e->lhs)->kind == AST_EXPR_BINARY &&
            (strip_implicit(e->lhs)->op == PUNCT_PLUS ||
             strip_implicit(e->lhs)->op == PUNCT_MINUS))
            warn_at(s->lang->warnings, WARN_PARENTHESES, e->lhs->span,
                    "suggest parentheses around arithmetic in operand of "
                    "'?:'");
        break;
    case AST_EXPR_INDEX:
        warn_char_subscript(s, e);
        check_expr(s, e->lhs, SEMA_WARN_EXPR_VALUE);
        check_expr(s, e->rhs, SEMA_WARN_EXPR_VALUE);
        break;
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ALIGNOF:
        if (e->lhs && !e->unevaluated)
            check_expr(s, e->lhs, SEMA_WARN_EXPR_VALUE);
        break;
    case AST_EXPR_CALL: {
        Type *callee_type = e->lhs ? e->lhs->sem_type : NULL;

        warn_nonnull(s, e);

        if (callee_type && callee_type->kind == TY_PTR)
            callee_type = callee_type->base;
        if (callee_type && callee_type->kind == TY_FUNC &&
            callee_type->has_proto)
            for (i = 0; i < e->nargs && i < callee_type->nparams; i++)
                sema_warn_implicit_conversion(s, callee_type->params[i],
                                              e->args[i]);
    }
        check_expr(s, e->lhs, SEMA_WARN_EXPR_VALUE);
        for (i = 0; i < e->nargs; i++)
            check_expr(s, e->args[i], SEMA_WARN_EXPR_VALUE);
        break;
    case AST_EXPR_GENERIC:
        for (i = 0; i < e->nitems; i++)
            check_expr(s, e->items[i], SEMA_WARN_EXPR_VALUE);
        break;
    default:
        check_expr(s, e->lhs, SEMA_WARN_EXPR_VALUE);
        check_expr(s, e->mid, SEMA_WARN_EXPR_VALUE);
        check_expr(s, e->rhs, SEMA_WARN_EXPR_VALUE);
        for (i = 0; i < e->nargs; i++)
            check_expr(s, e->args[i], SEMA_WARN_EXPR_VALUE);
        break;
    }
}

void sema_warn_expr(Sema *s, AstNode *expr, unsigned context)
{
    if (!s || !s->lang || !s->lang->warnings)
        return;
    check_expr(s, expr, context);
}

static u32 floating_precision(Sema *s, const Type *t)
{
    TargetLayout tl;

    if (!t)
        return 0;
    if (t->kind == TY_FLOAT || t->kind == TY_FLOAT32)
        return 24;
    if (t->kind == TY_DOUBLE || t->kind == TY_FLOAT64 || t->kind == TY_FLOAT32X)
        return 53;
    if (t->kind == TY_FLOAT128)
        return 113; /* binary128 on every target, unlike long double */
    if (t->kind != TY_LDOUBLE && t->kind != TY_FLOAT64X)
        return 0;
    tl = cgf_target_layout(s->target);
    if (tl.ldbl_kind == CGF_LDBL_IEEE128)
        return 113;
    if (tl.ldbl_kind == CGF_LDBL_X87_80)
        return 64;
    return 53;
}

static bool float_constant_changes(Sema *s, AstNode *source, Type *destination)
{
    ConstValue cv = constexpr_eval(s, source, CE_FOLD);
    SfStatus st = {0};

    if (cv.kind != CV_FLOAT)
        return true;
    if (type_is_floating(destination))
        (void)sf_convert(cv.f, constexpr_format_of(s, cv.type),
                         constexpr_format_of(s, destination), &st);
    else
        (void)sf_to_int(cv.f, (int)conv_int_bits(s, destination),
                        !conv_is_signed(s, destination), &st);
    return st.inexact || st.overflow || st.underflow || st.invalid;
}

static bool int_constant_changes_to_float(Sema *s, const ConstValue *cv,
                                          Type *destination)
{
    SfStatus st = {0};
    i64 signed_bits = signed_value(s, cv);
    bool negative = conv_is_signed(s, cv->type) && signed_bits < 0;
    u64 magnitude = negative ? 0ull - (u64)signed_bits : cv->i;

    (void)sf_from_int(magnitude, negative, constexpr_format_of(s, destination),
                      &st);
    return st.inexact || st.overflow || st.underflow || st.invalid;
}

void sema_warn_implicit_conversion(Sema *s, Type *destination,
                                   AstNode *converted)
{
    AstNode *source;
    Type *from;
    ConstValue cv;
    bool have_constant;
    bool from_signed;
    bool to_signed;
    u32 from_bits;
    u32 to_bits;

    if (!s || !s->lang || !s->lang->warnings || !destination || !converted ||
        has_explicit_cast(converted))
        return;
    source = source_expr(converted);
    if (!source || !source->sem_type || source->poisoned)
        return;
    from = source->sem_type;
    if (!type_is_arithmetic(from) || !type_is_arithmetic(destination) ||
        type_compatible(from, destination))
        return;
    /* Conversion to _Bool is a truth-value conversion, not a one-bit
     * truncation: every nonzero arithmetic value becomes 1.  Consequently
     * neither narrowing, sign, nor overflow diagnostics apply. */
    if (destination->kind == TY_BOOL)
        return;
    have_constant = int_constant(s, source, &cv);

    if (type_is_integer(from) && type_is_integer(destination)) {
        from_signed = conv_is_signed(s, from);
        to_signed = conv_is_signed(s, destination);
        from_bits = conv_int_bits(s, from);
        to_bits = conv_int_bits(s, destination);
        if (have_constant) {
            bool fits_value = constant_fits(s, &cv, destination);

            if (!constant_fits_width(s, &cv, destination)) {
                i64 before = signed_value(s, &cv);
                i64 after = converted_signed_value(s, &cv, destination);

                warn_at(s->lang->warnings, WARN_OVERFLOW, converted->span,
                        "overflow in conversion from '%s' to '%s' changes "
                        "value from '%lld' to '%lld'",
                        type_to_str(s->arena, from),
                        type_to_str(s->arena, destination), (long long)before,
                        (long long)after);
            } else if (!fits_value && from_signed != to_signed) {
                warn_at(s->lang->warnings, WARN_SIGN_CONVERSION,
                        converted->span,
                        "conversion to '%s' from '%s' changes the sign of "
                        "the result",
                        type_to_str(s->arena, destination),
                        type_to_str(s->arena, from));
            } else if (!fits_value) {
                warn_at(s->lang->warnings, WARN_CONVERSION, converted->span,
                        "conversion from '%s' to '%s' changes value",
                        type_to_str(s->arena, from),
                        type_to_str(s->arena, destination));
            }
            return; /* A fitting integer constant is the idiomatic case. */
        }
        if (mask_bounds_conversion(s, source, destination))
            return;
        if (from_signed != to_signed)
            warn_at(s->lang->warnings, WARN_SIGN_CONVERSION, converted->span,
                    "conversion to '%s' from '%s' may change the sign of the "
                    "result",
                    type_to_str(s->arena, destination),
                    type_to_str(s->arena, from));
        if (to_bits < from_bits)
            warn_at(s->lang->warnings, WARN_CONVERSION, converted->span,
                    "conversion from '%s' to '%s' may change value",
                    type_to_str(s->arena, from),
                    type_to_str(s->arena, destination));
        return;
    }

    if (type_is_integer(from) && type_is_floating(destination)) {
        u32 value_bits =
            conv_int_bits(s, from) - (conv_is_signed(s, from) ? 1u : 0u);

        if ((have_constant &&
             int_constant_changes_to_float(s, &cv, destination)) ||
            (!have_constant && value_bits > floating_precision(s, destination)))
            warn_at(s->lang->warnings, WARN_CONVERSION, converted->span,
                    "conversion from '%s' to '%s' may change value",
                    type_to_str(s->arena, from),
                    type_to_str(s->arena, destination));
        return;
    }
    if (type_is_floating(from) && type_is_integer(destination)) {
        if (source->kind != AST_EXPR_FLOAT ||
            float_constant_changes(s, source, destination))
            warn_at(s->lang->warnings, WARN_CONVERSION, converted->span,
                    "conversion from '%s' to '%s' may change value",
                    type_to_str(s->arena, from),
                    type_to_str(s->arena, destination));
        return;
    }
    if (type_is_floating(from) && type_is_floating(destination) &&
        floating_precision(s, destination) < floating_precision(s, from) &&
        (source->kind != AST_EXPR_FLOAT ||
         float_constant_changes(s, source, destination)))
        warn_at(s->lang->warnings, WARN_CONVERSION, converted->span,
                "conversion from '%s' to '%s' may change value",
                type_to_str(s->arena, from),
                type_to_str(s->arena, destination));
}
