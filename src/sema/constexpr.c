#include <string.h>

#include "sema/sema.h"
#include "warn/warn.h"

/* Constant expressions (C11 6.6), and the bridge from a float literal's
 * SPELLING to a correctly-rounded value in the target's format.
 *
 * The literal grammar lives here rather than in softfp because softfp
 * must stay library-clean — Sprint 49 links it into libcgf_rt, where
 * there is no C parser. softfp takes digits and an exponent; deciding
 * which characters those are is the compiler's job. */

/* Which format a target uses for each floating type. `long double` is the
 * cross-target trap: x87 80-bit on x86-64, IEEE binary128 on arm64-linux,
 * and plain double on arm64-macos. */
SfFormat constexpr_format_of(Sema *s, const Type *t)
{
    TargetLayout tl = cgf_target_layout(s->target);

    if (!t)
        return SF_BINARY64;
    switch (t->kind) {
    case TY_FLOAT:
    case TY_FLOAT32:
        return SF_BINARY32;
    case TY_DOUBLE:
    case TY_FLOAT64:
    case TY_FLOAT32X:
        return SF_BINARY64;
    case TY_FLOAT128:
        /* Target-independent by definition -- that is the type's whole
         * purpose on x86-64, where long double is x87 80-bit. */
        return SF_BINARY128;
    case TY_LDOUBLE:
    case TY_FLOAT64X:
        switch (tl.ldbl_kind) {
        case CGF_LDBL_X87_80:
            return SF_X87_80;
        case CGF_LDBL_IEEE128:
            return SF_BINARY128;
        case CGF_LDBL_IS_DOUBLE:
            return SF_BINARY64;
        }
        return SF_BINARY64;
    default:
        return SF_BINARY64;
    }
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static bool is_hex(char c)
{
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Parses a C floating constant's spelling into `f`. The spelling is
 * exactly what the lexer saw, suffix included; the classification (float
 * / double / long double) already happened in Sprint 8, so the FORMAT
 * comes from the caller and the suffix is only skipped here. */
Sf constexpr_parse_float(const char *sp, SfFormat f, SfStatus *st)
{
    char digits[4096];
    size_t ndigits = 0;
    int32_t dec_exp = 0;
    size_t i = 0;
    Sf zero;

    memset(&zero, 0, sizeof(zero));
    memset(st, 0, sizeof(*st));
    if (!sp)
        return zero;

    if (sp[0] == '0' && (sp[1] == 'x' || sp[1] == 'X')) {
        /* Hex float: exact by construction, so it rounds exactly once. */
        int32_t bin_exp = 0;
        bool seen_dot = false;
        size_t frac_digits = 0;

        i = 2;
        while (sp[i] && (is_hex(sp[i]) || sp[i] == '.')) {
            if (sp[i] == '.') {
                seen_dot = true;
            } else {
                if (ndigits < sizeof(digits))
                    digits[ndigits++] = sp[i];
                if (seen_dot)
                    frac_digits++;
            }
            i++;
        }
        if (sp[i] == 'p' || sp[i] == 'P') {
            int sign = 1;
            int32_t v = 0;

            i++;
            if (sp[i] == '+' || sp[i] == '-') {
                if (sp[i] == '-')
                    sign = -1;
                i++;
            }
            while (is_digit(sp[i])) {
                v = v * 10 + (sp[i] - '0');
                i++;
            }
            bin_exp = sign * v;
        }
        /* Each fractional hex digit is four binary places. */
        bin_exp -= (int32_t)(frac_digits * 4);
        return sf_from_hex(digits, ndigits, bin_exp, f, st);
    }

    {
        bool seen_dot = false;
        size_t frac_digits = 0;

        while (sp[i] && (is_digit(sp[i]) || sp[i] == '.')) {
            if (sp[i] == '.') {
                seen_dot = true;
            } else {
                if (ndigits < sizeof(digits))
                    digits[ndigits++] = sp[i];
                if (seen_dot)
                    frac_digits++;
            }
            i++;
        }
        if (sp[i] == 'e' || sp[i] == 'E') {
            int sign = 1;
            int32_t v = 0;

            i++;
            if (sp[i] == '+' || sp[i] == '-') {
                if (sp[i] == '-')
                    sign = -1;
                i++;
            }
            while (is_digit(sp[i])) {
                /* Clamp rather than overflow: an exponent past this is
                 * already infinity or zero in every format we have. */
                if (v < 1000000)
                    v = v * 10 + (sp[i] - '0');
                i++;
            }
            dec_exp = sign * v;
        }
        dec_exp -= (int32_t)frac_digits;
    }
    return sf_from_decimal(digits, ndigits, dec_exp, f, st);
}

/* The value of a float literal token, in the format its type calls for.
 * Diagnoses the range cases gcc diagnoses. */
Sf constexpr_float_literal(Sema *s, AstNode *e)
{
    SfStatus st;
    SfFormat f = constexpr_format_of(s, e->sem_type);
    Sf v = constexpr_parse_float(e->tok ? e->tok->spelling : NULL, f, &st);

    if (st.overflow && !e->fp_range_diagnosed) {
        warn_at(s->lang->warnings, WARN_OVERFLOW, e->span,
                "floating constant exceeds range of '%s'",
                type_to_str(s->arena, e->sem_type));
        e->fp_range_diagnosed = true;
    } else if (st.underflow && st.inexact && sf_is_zero(v) &&
               !e->fp_range_diagnosed) {
        /* TRUNCATED TO ZERO MEANS ZERO. A constant that lands on a SUBNORMAL
         * has underflowed and is inexact and is still a perfectly good
         * value: musl's <float.h> spells DBL_TRUE_MIN as
         * 4.94065645841246544177e-324, the smallest subnormal double, and we
         * warned on the very macro whose job is to name it. gcc is silent
         * there and warns for 1e-400, which really is zero -- both measured.
         *
         * Checking the RESULT rather than the status flags is the fix: the
         * flags say how the rounding went, not what came out. */
        warn_at(s->lang->warnings, WARN_OVERFLOW, e->span,
                "floating constant truncated to zero");
        e->fp_range_diagnosed = true;
    }
    return v;
}

/* --- the constant-expression evaluator ----------------------------------- */

/* All integer arithmetic happens on u64 BIT PATTERNS with the sign
 * interpreted from the type, never on host `int` values. A host int is
 * the wrong width for `long long` and the wrong signedness half the time,
 * and its overflow is undefined where ours must be diagnosable. */

static ConstValue cv_error(void)
{
    ConstValue v;

    memset(&v, 0, sizeof(v));
    v.kind = CV_ERROR;
    return v;
}

static ConstValue cv_int(Sema *s, Type *t, u64 bits)
{
    ConstValue v;

    memset(&v, 0, sizeof(v));
    v.kind = CV_INT;
    v.type = t ? t : type_basic(TY_INT);
    v.i = bits;
    (void)s;
    return v;
}

static ConstValue cv_special_float(Type *t, SfClass cls)
{
    ConstValue v;

    memset(&v, 0, sizeof(v));
    v.kind = CV_FLOAT;
    v.type = t;
    v.f.cls = cls;
    return v;
}

/* Truncates to the type's width and sign-extends if the type is signed —
 * the operation every fold ends with, so a value never carries bits its
 * type cannot hold. */
static u64 fit(Sema *s, Type *t, u64 bits)
{
    u32 w = conv_int_bits(s, t);

    if (w == 0 || w >= 64)
        return bits;
    bits &= (1ull << w) - 1;
    if (conv_is_signed(s, t) && (bits >> (w - 1)) & 1)
        bits |= ~((1ull << w) - 1);
    return bits;
}

static bool is_required(CeMode m)
{
    return m != CE_FOLD && m != CE_VLA;
}

static void ce_error(Sema *s, CeMode m, Span sp, const char *fmt, ...)
{
    va_list ap;
    char msg[512];

    if (!is_required(m))
        return; /* Opportunistic folds fail silently. */
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    s->nerrors++;
    diag_emit(s->dc, DIAG_ERROR, sp, "%s", msg);
}

static ConstValue eval(Sema *s, AstNode *e, CeMode m);

/* The traditional offsetof macro spells a member address from a null
 * pointer, rather than using __builtin_offsetof:
 *
 *   (size_t)((char *)&(((T *)0)->member) - (char *)0)
 *
 * Member expressions retain only the spelling, so recover the byte offset
 * here.  Anonymous aggregate members contribute their enclosing offsets on
 * the way down; returning only the innermost Member would lose them. */
static bool member_byte_offset(Sema *s, Type *rec, const char *name, i64 *out)
{
    Member *it;

    if (!rec || (rec->kind != TY_STRUCT && rec->kind != TY_UNION) || !rec->tag)
        return false;
    layout_record(s, rec);
    for (it = rec->tag->members; it; it = it->next) {
        if (it->name == name) {
            if (it->is_bitfield)
                return false;
            *out = (i64)it->offset;
            return true;
        }
        if (!it->name && it->type &&
            (it->type->kind == TY_STRUCT || it->type->kind == TY_UNION) &&
            find_member_named(it->type, name)) {
            i64 inner;

            if (!member_byte_offset(s, it->type, name, &inner))
                return false;
            *out = (i64)it->offset + inner;
            return true;
        }
    }
    return false;
}

static bool is_null_pointer_value(const ConstValue *v)
{
    return v && v->kind == CV_INT && v->i == 0 && v->type &&
           v->type->kind == TY_PTR;
}

/* Lowering pools ordinary string literals by encoding and bytes, while
 * function-name objects use their own pool.  Constant folding must use the
 * same identity rule: two AST nodes for `"x"` name one emitted object, but a
 * file-scope compound literal remains distinct unless it is the same node. */
static bool same_anonymous_object(const AstNode *a, const AstNode *b)
{
    if (a == b)
        return true;
    if (!a || !b || a->kind != AST_EXPR_STRING || b->kind != AST_EXPR_STRING ||
        a->is_func_name != b->is_func_name || !a->tok || !b->tok ||
        a->tok->str.enc != b->tok->str.enc ||
        a->tok->str.nbytes != b->tok->str.nbytes)
        return false;
    return a->tok->str.nbytes == 0 ||
           memcmp(a->tok->str.bytes, b->tok->str.bytes, a->tok->str.nbytes) ==
               0;
}

static bool same_address_origin(const ConstValue *a, const ConstValue *b)
{
    if (a->sym != b->sym)
        return false;
    if (a->sym)
        return true;
    return same_anonymous_object(a->anon, b->anon);
}

/* A null pointer used while folding the offsetof idiom is an address origin,
 * but it must never escape as CV_ADDR: sym == anon == NULL otherwise denotes
 * an anonymous static object to the initializer-image path. */
static ConstValue null_address(Type *type)
{
    ConstValue v;

    memset(&v, 0, sizeof(v));
    v.kind = CV_ADDR;
    v.type = type;
    return v;
}

/* Add a byte displacement to either kind of address the initializer folder
 * can carry.  CV_ADDR is a linker relocation; CV_INT is an absolute numeric
 * pointer produced by an integer-to-pointer cast such as `(T *)0x4000`.
 * Keeping the latter as a bit pattern is load-bearing: turning it into a
 * relocation with no symbol would instead name an anonymous object. */
static bool address_add_bytes(ConstValue *v, u64 bytes)
{
    if (v->kind == CV_ADDR) {
        v->addend += (i64)bytes;
        return true;
    }
    if (v->kind == CV_INT) {
        v->i += bytes;
        return true;
    }
    return false;
}

/* Fold the address of a member/subscript lvalue without loading it.  This is
 * deliberately separate from eval(): a decayed array member such as
 *
 *   &(((T *)0)->items[2])
 *
 * reaches us as INDEX(implicit-cast(MEMBER)), and evaluating MEMBER as a
 * value would try to load from the null base.  The traditional offsetof
 * idiom needs the address arithmetic instead.  `from_null` lets the caller
 * distinguish that GNU ICE extension from an ordinary address constant. */
static ConstValue eval_lvalue_address(Sema *s, AstNode *e, CeMode m,
                                      bool *from_null)
{
    ConstValue base;

    while (e && (e->kind == AST_EXPR_PAREN ||
                 (e->kind == AST_EXPR_CAST && e->implicit && e->lhs)))
        e = e->lhs;
    if (!e)
        return cv_error();

    if (e->kind == AST_EXPR_MEMBER) {
        Type *rec = e->lhs ? e->lhs->sem_type : NULL;
        i64 member_off;
        bool base_from_null = false;

        if (e->is_arrow) {
            base = eval(s, e->lhs, m);
            if (is_null_pointer_value(&base)) {
                base = null_address(base.type);
                base_from_null = true;
            }
            if (rec && rec->kind == TY_PTR)
                rec = rec->base;
        } else {
            base = eval_lvalue_address(s, e->lhs, m, &base_from_null);
        }
        if (base.kind == CV_ERROR)
            return base;
        if (!member_byte_offset(s, rec, e->name, &member_off) ||
            !address_add_bytes(&base, (u64)member_off)) {
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time");
            return cv_error();
        }
        base.type = e->sem_type;
        *from_null = *from_null || base_from_null;
        return base;
    }

    if (e->kind == AST_EXPR_INDEX) {
        AstNode *base_node = e->lhs;
        AstNode *idx_node = e->rhs;
        ConstValue idx;
        Type *ptr;
        u64 scale;

        if ((!base_node->sem_type || base_node->sem_type->kind != TY_PTR) &&
            idx_node->sem_type && idx_node->sem_type->kind == TY_PTR) {
            AstNode *tmp = base_node;

            base_node = idx_node;
            idx_node = tmp;
        }
        ptr = base_node->sem_type;
        base = eval_lvalue_address(s, base_node, m, from_null);
        idx = eval(s, idx_node, m == CE_FOLD ? CE_FOLD : CE_ICE);
        if (base.kind == CV_ERROR || idx.kind == CV_ERROR)
            return cv_error();
        if (idx.kind != CV_INT || !ptr ||
            (ptr->kind != TY_PTR && ptr->kind != TY_ARRAY) || !ptr->base) {
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time");
            return cv_error();
        }
        scale = layout_of(s, ptr->base).size;
        if (!address_add_bytes(&base, idx.i * scale)) {
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time");
            return cv_error();
        }
        base.type = e->sem_type;
        return base;
    }

    if (e->kind == AST_EXPR_IDENT && e->sym && e->sym->kind == SYM_VAR) {
        ConstValue v;

        if (!e->sym->static_storage) {
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time: "
                     "'%s' has automatic storage duration",
                     e->name);
            return cv_error();
        }
        if (m != CE_ADDR && m != CE_FOLD) {
            ce_error(s, m, e->span,
                     "an address is not an integer constant expression");
            return cv_error();
        }
        memset(&v, 0, sizeof(v));
        v.kind = CV_ADDR;
        v.type = e->sem_type;
        v.sym = e->sym;
        return v;
    }

    if (e->kind == AST_EXPR_COMPOUND_LIT) {
        ConstValue v;

        if (!e->is_static_storage) {
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time: "
                     "the compound literal has automatic storage duration");
            return cv_error();
        }
        if (m != CE_ADDR && m != CE_FOLD) {
            ce_error(s, m, e->span,
                     "an address is not an integer constant expression");
            return cv_error();
        }
        memset(&v, 0, sizeof(v));
        v.kind = CV_ADDR;
        v.type = e->sem_type;
        v.anon = e;
        return v;
    }

    base = eval(s, e, m);
    if (is_null_pointer_value(&base)) {
        base = null_address(base.type);
        *from_null = true;
    }
    return base;
}

/* Signed overflow is UNDEFINED at runtime but must be an ERROR at compile
 * time: a constant expression has one right answer, and silently wrapping
 * would bake a wrong number into the program. */
static bool add_overflows(Sema *s, Type *t, u64 a, u64 b, u64 r)
{
    u32 w = conv_int_bits(s, t);
    u64 sign_mask;

    if (!conv_is_signed(s, t) || w == 0 || w > 64)
        return false;
    sign_mask = 1ull << (w - 1);
    /* Overflow iff the operands share a sign that the result does not. */
    return ((a ^ r) & (b ^ r) & sign_mask) != 0;
}

static bool signed_minimum_value(Sema *s, Type *t, u64 bits)
{
    u32 w = conv_int_bits(s, t);

    return conv_is_signed(s, t) && w > 0 && w <= 64 &&
           bits == fit(s, t, 1ull << (w - 1));
}

static bool multiply_overflows(Sema *s, Type *t, u64 a, u64 b)
{
    u32 w = conv_int_bits(s, t);
    u64 sign_mask, a_mag, b_mag, limit;
    bool a_neg, b_neg;

    if (!conv_is_signed(s, t) || w == 0 || w > 64)
        return false;
    sign_mask = 1ull << (w - 1);
    a_neg = (a & sign_mask) != 0;
    b_neg = (b & sign_mask) != 0;
    a_mag = a_neg ? 0 - a : a;
    b_mag = b_neg ? 0 - b : b;
    limit = a_neg != b_neg ? sign_mask : sign_mask - 1;
    return b_mag != 0 && a_mag > limit / b_mag;
}

static ConstValue eval_binary(Sema *s, AstNode *e, CeMode m)
{
    bool pointer_difference =
        e->op == PUNCT_MINUS && e->lhs->sem_type && e->rhs->sem_type &&
        e->lhs->sem_type->kind == TY_PTR && e->rhs->sem_type->kind == TY_PTR;
    CeMode operand_mode = pointer_difference && m == CE_ARITH ? CE_ADDR : m;
    ConstValue l = eval(s, e->lhs, operand_mode);
    ConstValue r;
    Type *t = e->sem_type;
    u64 res;

    if (l.kind == CV_ERROR)
        return l;
    /* && and || SHORT-CIRCUIT even here: `0 && (1/0)` is a valid constant
     * expression precisely because the right side is never evaluated. */
    if (e->op == PUNCT_AMPAMP && l.kind == CV_INT && l.i == 0)
        return cv_int(s, type_basic(TY_INT), 0);
    if (e->op == PUNCT_PIPEPIPE && l.kind == CV_INT && l.i != 0)
        return cv_int(s, type_basic(TY_INT), 1);

    r = eval(s, e->rhs, operand_mode);
    if (r.kind == CV_ERROR)
        return r;

    /* Fold pointer subtraction when both operands name the same object.  The
     * null/null case is the traditional offsetof macro; ordinary same-symbol
     * differences such as `&a[3] - &a[1]` follow the same C rule. */
    if (pointer_difference) {
        ConstValue la = is_null_pointer_value(&l) ? null_address(l.type) : l;
        ConstValue ra = is_null_pointer_value(&r) ? null_address(r.type) : r;

        if (la.kind == CV_ADDR && ra.kind == CV_ADDR &&
            same_address_origin(&la, &ra)) {
            i64 bytes = la.addend - ra.addend;
            u64 scale = 1;

            if (e->lhs->sem_type->base &&
                layout_is_complete_for_size(e->lhs->sem_type->base))
                scale = layout_of(s, e->lhs->sem_type->base).size;
            if (scale == 0 || bytes % (i64)scale != 0) {
                ce_error(s, m, e->span,
                         "pointer difference is not an exact element count");
                return cv_error();
            }
            return cv_int(s, e->sem_type,
                          fit(s, e->sem_type, (u64)(bytes / (i64)scale)));
        }
    }

    /* Address constant arithmetic: `&g + 3` and `arr + 3`. */
    if (l.kind == CV_ADDR || r.kind == CV_ADDR) {
        ConstValue addr = l.kind == CV_ADDR ? l : r;
        ConstValue off = l.kind == CV_ADDR ? r : l;
        bool integer_reloc = addr.type && type_is_integer(addr.type);
        u64 scale = 1;

        /* GNU pointer-to-integer static initializers retain the relocation
         * through integer addends. Ordinary pointer arithmetic still needs
         * CE_ADDR/CE_FOLD and therefore cannot leak into an ICE. */
        if (m != CE_ADDR && m != CE_FOLD && !(m == CE_ARITH && integer_reloc)) {
            ce_error(s, m, e->span,
                     "an address is not an integer constant expression");
            return cv_error();
        }
        if (off.kind != CV_INT ||
            (e->op != PUNCT_PLUS && e->op != PUNCT_MINUS)) {
            ce_error(s, m, e->span,
                     "invalid operation on an address "
                     "constant");
            return cv_error();
        }
        /* Pointer arithmetic scales by the pointee's size. */
        if (addr.type && addr.type->kind == TY_PTR &&
            layout_is_complete_for_size(addr.type->base))
            scale = layout_of(s, addr.type->base).size;
        addr.addend +=
            (e->op == PUNCT_MINUS ? -(i64)off.i : (i64)off.i) * (i64)scale;
        addr.type = t ? t : addr.type;
        return addr;
    }

    if (l.kind == CV_FLOAT || r.kind == CV_FLOAT) {
        SfStatus st;
        SfFormat f = constexpr_format_of(s, t);
        ConstValue v;

        if (m == CE_ICE) {
            ce_error(s, m, e->span,
                     "a floating operand is not allowed in an integer "
                     "constant expression");
            return cv_error();
        }
        memset(&st, 0, sizeof(st));
        /* Promote an integer operand into the float format first, so the
         * arithmetic happens once in the result's own format. */
        if (l.kind == CV_INT)
            l.f = sf_from_int(l.i, false, f, &st);
        if (r.kind == CV_INT)
            r.f = sf_from_int(r.i, false, f, &st);
        memset(&v, 0, sizeof(v));
        v.kind = CV_FLOAT;
        v.type = t;
        switch (e->op) {
        case PUNCT_PLUS:
            v.f = sf_add(l.f, r.f, f, &st);
            break;
        case PUNCT_MINUS:
            v.f = sf_sub(l.f, r.f, f, &st);
            break;
        case PUNCT_STAR:
            v.f = sf_mul(l.f, r.f, f, &st);
            break;
        case PUNCT_SLASH:
            if (sf_is_zero(r.f)) {
                ce_error(s, m, e->span,
                         "division by zero in a constant "
                         "expression");
                return cv_error();
            }
            v.f = sf_div(l.f, r.f, f, &st);
            break;
        case PUNCT_LT:
        case PUNCT_GT:
        case PUNCT_LE:
        case PUNCT_GE:
        case PUNCT_EQEQ:
        case PUNCT_NOTEQ: {
            bool unordered;
            int c = sf_cmp(l.f, r.f, &unordered);
            int truth;

            switch (e->op) {
            case PUNCT_LT:
                truth = !unordered && c < 0;
                break;
            case PUNCT_GT:
                truth = !unordered && c > 0;
                break;
            case PUNCT_LE:
                truth = !unordered && c <= 0;
                break;
            case PUNCT_GE:
                truth = !unordered && c >= 0;
                break;
            case PUNCT_EQEQ:
                truth = !unordered && c == 0;
                break;
            default:
                truth = unordered || c != 0;
                break;
            }
            return cv_int(s, type_basic(TY_INT), (u64)truth);
        }
        default:
            ce_error(s, m, e->span,
                     "this operator is not allowed on floating constants");
            return cv_error();
        }
        return v;
    }

    if (l.kind != CV_INT || r.kind != CV_INT)
        return cv_error();

    switch (e->op) {
    case PUNCT_PLUS:
        res = l.i + r.i;
        if (add_overflows(s, t, l.i, r.i, res)) {
            ce_error(s, m, e->span, "overflow in constant expression");
            return cv_error();
        }
        break;
    case PUNCT_MINUS:
        res = l.i - r.i;
        if (add_overflows(s, t, l.i, (u64)(0 - r.i), res)) {
            ce_error(s, m, e->span, "overflow in constant expression");
            return cv_error();
        }
        break;
    case PUNCT_STAR: {
        res = l.i * r.i;
        /* Magnitudes keep the check target-width-correct and avoid making
         * the compiler itself evaluate INT64_MIN / -1 as a divide-back. */
        if (multiply_overflows(s, t, l.i, r.i)) {
            ce_error(s, m, e->span, "overflow in constant expression");
            return cv_error();
        }
        break;
    }
    case PUNCT_SLASH:
    case PUNCT_PERCENT:
        if (r.i == 0) {
            ce_error(s, m, e->span,
                     "division by zero in a constant expression");
            return cv_error();
        }
        /* SEMA-C-01: signed-minimum / -1 has no representable target value;
         * checking the target-width bit pattern before host / or % both
         * diagnoses the source UB and prevents compiler UB at 64 bits. */
        if (signed_minimum_value(s, t, l.i) && r.i == UINT64_MAX) {
            ce_error(s, m, e->span, "overflow in constant expression");
            return cv_error();
        }
        if (conv_is_signed(s, t))
            res = e->op == PUNCT_SLASH ? (u64)((i64)l.i / (i64)r.i)
                                       : (u64)((i64)l.i % (i64)r.i);
        else
            res = e->op == PUNCT_SLASH ? l.i / r.i : l.i % r.i;
        break;
    case PUNCT_SHL:
    case PUNCT_SHR: {
        u32 w = conv_int_bits(s, e->lhs->sem_type);

        /* A shift count at or past the promoted left operand's width has
         * no defined value, so a required-constant context must reject
         * rather than pick one. */
        if ((i64)r.i < 0 || (w && r.i >= w)) {
            ce_error(s, m, e->span,
                     "shift count %lld is out of range for a %u-bit type",
                     (long long)(i64)r.i, (unsigned)w);
            return cv_error();
        }
        if (e->op == PUNCT_SHL)
            res = l.i << r.i;
        else
            res = conv_is_signed(s, e->lhs->sem_type) ? (u64)((i64)l.i >> r.i)
                                                      : (l.i >> r.i);
        break;
    }
    case PUNCT_AMP:
        res = l.i & r.i;
        break;
    case PUNCT_PIPE:
        res = l.i | r.i;
        break;
    case PUNCT_CARET:
        res = l.i ^ r.i;
        break;
    case PUNCT_LT:
    case PUNCT_GT:
    case PUNCT_LE:
    case PUNCT_GE: {
        int c;

        if (conv_is_signed(s, e->lhs->sem_type))
            c = (i64)l.i < (i64)r.i ? -1 : ((i64)l.i > (i64)r.i ? 1 : 0);
        else
            c = l.i < r.i ? -1 : (l.i > r.i ? 1 : 0);
        res = (u64)(e->op == PUNCT_LT   ? c < 0
                    : e->op == PUNCT_GT ? c > 0
                    : e->op == PUNCT_LE ? c <= 0
                                        : c >= 0);
        return cv_int(s, type_basic(TY_INT), res);
    }
    case PUNCT_EQEQ:
        return cv_int(s, type_basic(TY_INT), l.i == r.i);
    case PUNCT_NOTEQ:
        return cv_int(s, type_basic(TY_INT), l.i != r.i);
    case PUNCT_AMPAMP:
        return cv_int(s, type_basic(TY_INT), (l.i != 0) && (r.i != 0));
    case PUNCT_PIPEPIPE:
        return cv_int(s, type_basic(TY_INT), (l.i != 0) || (r.i != 0));
    case PUNCT_COMMA:
        /* 6.6p3: the comma operator is forbidden in a constant
         * expression except inside an unevaluated operand. */
        ce_error(s, m, e->span,
                 "the comma operator is not allowed in a constant "
                 "expression");
        return cv_error();
    default:
        ce_error(s, m, e->span,
                 "this operator is not allowed in a constant "
                 "expression");
        return cv_error();
    }
    return cv_int(s, t, fit(s, t, res));
}

/* __builtin_offsetof's designator chain -> byte offset.
 *
 * Walks the same .member / [index] shape the parser built over the
 * placeholder base. Anonymous members are traversed with their offsets
 * ACCUMULATED (find_member alone returns the innermost member, whose
 * `offset` is relative to its own enclosing record). A bitfield has no
 * address, so offsetof of one is an error rather than a rounded-down
 * byte — gcc says the same. */
static bool offsetof_walk(Sema *s, CeMode m, AstNode *e, u64 *out)
{
    if (!e)
        return false;
    /* Sema materializes the array->pointer decay as an implicit cast
     * node, so `offsetof(S, arr[2])` reaches here as INDEX(CAST(MEMBER)).
     * Step through those: they change the TYPE, never the address. */
    while (e->kind == AST_EXPR_CAST && e->implicit && e->lhs)
        e = e->lhs;
    switch (e->kind) {
    case AST_EXPR_OFFSETOF_BASE:
        *out = 0;
        return true;
    case AST_EXPR_MEMBER: {
        const Type *rec = e->lhs ? e->lhs->sem_type : NULL;
        u64 base_off = 0;

        if (!offsetof_walk(s, m, e->lhs, &base_off))
            return false;
        if (!rec || !rec->tag) {
            ce_error(s, m, e->span, "this is not a constant expression");
            return false;
        }
        layout_record(s, (Type *)rec);
        /* Descend named-or-anonymous, accumulating as we go. */
        for (;;) {
            Member *it;
            bool stepped = false;

            for (it = rec->tag->members; it; it = it->next) {
                if (it->name == e->name) {
                    if (it->is_bitfield) {
                        ce_error(s, m, e->span,
                                 "'__builtin_offsetof' applied to a "
                                 "bit-field member");
                        return false;
                    }
                    *out = base_off + it->offset;
                    return true;
                }
                if (!it->name && it->type &&
                    (it->type->kind == TY_STRUCT ||
                     it->type->kind == TY_UNION) &&
                    find_member_named(it->type, e->name)) {
                    layout_record(s, it->type);
                    base_off += it->offset;
                    rec = it->type;
                    stepped = true;
                    break;
                }
            }
            if (!stepped) {
                ce_error(s, m, e->span, "this is not a constant expression");
                return false;
            }
        }
    }
    case AST_EXPR_INDEX: {
        u64 base_off = 0;
        ConstValue idx;
        const Type *arr = e->lhs ? e->lhs->sem_type : NULL;

        if (!offsetof_walk(s, m, e->lhs, &base_off))
            return false;
        idx = eval(s, e->rhs, m);
        /* `arr->base` is the element type whether arr is still an array
         * type or the decayed pointer — both spell the same element. */
        if (idx.kind != CV_INT || !arr || !arr->base) {
            ce_error(s, m, e->span,
                     "'__builtin_offsetof' array index must be an integer "
                     "constant expression");
            return false;
        }
        *out = base_off + (u64)(i64)idx.i * layout_of(s, arr->base).size;
        return true;
    }
    default:
        ce_error(s, m, e->span,
                 "'__builtin_offsetof' expects a member designator");
        return false;
    }
}

static ConstValue eval(Sema *s, AstNode *e, CeMode m)
{
    if (!e)
        return cv_error();
    /* Sprint 11's contract: never diagnose about an already-diagnosed
     * subtree. */
    if (e->poisoned || (e->sem_type && e->sem_type->kind == TY_ERROR))
        return cv_error();

    switch (e->kind) {
    case AST_EXPR_INT:
    case AST_EXPR_CHAR:
        return cv_int(s, e->sem_type, e->tok->int_val);
    case AST_EXPR_FLOAT: {
        ConstValue v;

        if (m == CE_ICE) {
            ce_error(s, m, e->span,
                     "a floating constant is not an integer constant "
                     "expression");
            return cv_error();
        }
        memset(&v, 0, sizeof(v));
        v.kind = CV_FLOAT;
        v.type = e->sem_type;
        v.f = constexpr_float_literal(s, e);
        return v;
    }
    case AST_EXPR_STRING: {
        /* A string literal IS an address constant: it names an anonymous
         * object in .rodata, which Sprint 19 materializes. */
        ConstValue v;

        if (m != CE_ADDR && m != CE_FOLD) {
            ce_error(s, m, e->span,
                     "a string literal is not an integer constant "
                     "expression");
            return cv_error();
        }
        memset(&v, 0, sizeof(v));
        v.kind = CV_ADDR;
        v.type = e->sem_type;
        v.sym = NULL;
        v.anon = e; /* lowering materializes the .rodata object */
        return v;
    }
    case AST_EXPR_COMPOUND_LIT: {
        /* AN ARRAY LITERAL AT FILE SCOPE IS AN ADDRESS CONSTANT WITHOUT AN
         * EXPLICIT `&`, exactly as an array designator is one line below and
         * a string literal is one case above -- the array decays, and what
         * it decays to is the address of an object with static storage
         * duration (6.5.2.5p5).
         *
         * `&(int[3]){1,2,3}` already worked, because the address-of case
         * further down handles a compound literal operand. `(int[3]){1,2,3}`
         * did not, because `eval` had NO compound-literal case at all, so
         * the implicit decay had nothing to reach. That is the whole bug:
         * NOT a general hole in implicit decay, which an array VARIABLE and
         * a string literal both come through correctly.
         *
         * ARRAY ONLY. A struct or union literal used as a VALUE
         * (`static struct S x = (struct S){1,2};`) is an aggregate image
         * rather than an address, and answering with an address here would
         * turn a clean error into a wrong initializer. It is still rejected,
         * and still deliberately, until the aggregate-image path learns the
         * same shape. */
        ConstValue v;

        if (!e->sem_type || e->sem_type->kind != TY_ARRAY) {
            ce_error(s, m, e->span, "this is not a constant expression");
            return cv_error();
        }
        if (!e->is_static_storage) {
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time: "
                     "the compound literal has automatic storage duration");
            return cv_error();
        }
        if (m != CE_ADDR && m != CE_FOLD) {
            ce_error(s, m, e->span,
                     "a compound literal is not an integer constant "
                     "expression");
            return cv_error();
        }
        memset(&v, 0, sizeof(v));
        v.kind = CV_ADDR;
        v.type = e->sem_type;
        v.sym = NULL;
        v.anon = e; /* lowering materializes the anonymous object */
        return v;
    }
    case AST_EXPR_TYPES_COMPATIBLE:
        /* A CONSTANT, which is the whole point of the builtin: it exists to
         * be used in an array bound, a `?:` selector or a _Static_assert.
         * Sema already computed the answer; without this row the value was
         * a perfectly good int that no constant context would accept, and
         * `int a[__builtin_types_compatible_p(int,int) ? 4 : 1];` failed at
         * file scope with "variably modified type". */
        return cv_int(s, type_basic(TY_INT), e->types_compatible ? 1 : 0);
    case AST_EXPR_CHOOSE_EXPR:
        /* The SELECTED arm, and only that one. Folding the other would
         * evaluate an expression the language says is not evaluated. */
        return eval(s, e->choose_taken ? e->mid : e->rhs, m);
    case AST_EXPR_GENERIC:
        /* SEMA-H-07: sema stores the selected association in `mid`.
         * Evaluate only that arm, in the caller's constant-expression mode;
         * the controlling expression and unselected associations are not
         * evaluated. */
        return eval(s, e->mid, m);
    case AST_EXPR_PAREN:
        return eval(s, e->lhs, m);
    case AST_EXPR_IDENT: {
        Symbol *sym = e->sym;
        ConstValue v;

        if (sym && sym->kind == SYM_ENUM_CONST)
            return cv_int(s, e->sem_type, (u64)sym->enum_value);
        /* GCC treats an already-initialized, top-level const scalar object
         * as foldable in later static initializers. It deliberately does
         * NOT promote the name to an integer constant expression, so keep
         * CE_ICE on the ordinary rejection path below. The declaration pass
         * records only initializers that have already folded silently; using
         * CE_FOLD again preserves that GNU extension's full value shape
         * (including relocatable const pointers) without emitting a second
         * diagnostic from the initializer's source location. */
        if (sym && sym->kind == SYM_VAR && sym->foldable_const_init &&
            m != CE_ICE && m != CE_VLA) {
            v = eval(s, sym->foldable_const_init, CE_FOLD);
            if (v.kind != CV_ERROR) {
                v.type = e->sem_type;
                return v;
            }
        }
        /* A FUNCTION or an array designator is an address constant even
         * without an explicit `&`. */
        if (sym &&
            (sym->kind == SYM_FUNC || (sym->static_storage && sym->type &&
                                       sym->type->kind == TY_ARRAY))) {
            if (m != CE_ADDR && m != CE_FOLD) {
                ce_error(s, m, e->span,
                         "'%s' is not an integer constant expression", e->name);
                return cv_error();
            }
            memset(&v, 0, sizeof(v));
            v.kind = CV_ADDR;
            v.type = e->sem_type;
            v.sym = sym;
            return v;
        }
        ce_error(s, m, e->span, "'%s' is not a constant expression", e->name);
        return cv_error();
    }
    case AST_EXPR_UNARY: {
        ConstValue o;

        if (e->op == PUNCT_AMP) {
            /* `&object` is an address constant; `&automatic` is not, and
             * that is the check that keeps a stack address out of .data. */
            AstNode *inner = e->lhs;
            ConstValue v;

            while (inner && inner->kind == AST_EXPR_PAREN)
                inner = inner->lhs;
            if (inner && (inner->kind == AST_EXPR_MEMBER ||
                          inner->kind == AST_EXPR_INDEX)) {
                /* The null-based offsetof spelling is accepted as an ICE by
                 * gcc and is used throughout musl.  Recognize that narrow
                 * shape, including indexed array members, before the general
                 * address-constant mode check;
                 * ordinary `&object.member` remains CE_ADDR/CE_FOLD only. */
                bool from_null = false;

                v = eval_lvalue_address(s, inner, m, &from_null);
                if (v.kind == CV_ERROR)
                    return v;
                if (!from_null && m != CE_ADDR && m != CE_FOLD) {
                    ce_error(s, m, e->span,
                             "an address is not an integer constant "
                             "expression");
                    return cv_error();
                }
                v.type = e->sem_type;
                return v;
            }
            if (m != CE_ADDR && m != CE_FOLD) {
                ce_error(s, m, e->span,
                         "an address is not an integer constant expression");
                return cv_error();
            }
            memset(&v, 0, sizeof(v));
            v.kind = CV_ADDR;
            v.type = e->sem_type;
            if (inner && inner->kind == AST_EXPR_IDENT && inner->sym) {
                if (inner->sym->kind == SYM_VAR &&
                    !inner->sym->static_storage) {
                    ce_error(s, m, e->span,
                             "initializer element is not computable at load "
                             "time: '%s' has automatic storage duration",
                             inner->name);
                    return cv_error();
                }
                v.sym = inner->sym;
                return v;
            }
            if (inner && inner->kind == AST_EXPR_COMPOUND_LIT) {
                /* A compound literal at FILE SCOPE has static storage
                 * duration (6.5.2.5p5), so its address is a perfectly
                 * good address constant — Sprint 19 materializes the
                 * anonymous object it names. Sprint 10 recorded which
                 * scope it was written in, because only the parser knew. */
                if (!inner->is_static_storage) {
                    ce_error(s, m, e->span,
                             "initializer element is not computable at load "
                             "time: the compound literal has automatic "
                             "storage duration");
                    return cv_error();
                }
                v.sym = NULL;
                v.anon = inner; /* lowering materializes the object */
                return v;
            }
            ce_error(s, m, e->span,
                     "initializer element is not computable at load time");
            return cv_error();
        }

        o = eval(s, e->lhs, m);
        if (o.kind == CV_ERROR)
            return o;
        if (e->is_postfix) {
            ce_error(s, m, e->span,
                     "'%s' is not allowed in a constant expression",
                     ast_punct_name(e->op));
            return cv_error();
        }
        if (o.kind == CV_FLOAT) {
            ConstValue v = o;

            switch (e->op) {
            case PUNCT_PLUS:
                return v;
            case PUNCT_MINUS:
                v.f = sf_neg(v.f);
                return v;
            case PUNCT_BANG:
                return cv_int(s, type_basic(TY_INT), sf_is_zero(v.f) ? 1 : 0);
            default:
                ce_error(s, m, e->span,
                         "this operator is not allowed on a floating "
                         "constant");
                return cv_error();
            }
        }
        if (o.kind != CV_INT)
            return cv_error();
        switch (e->op) {
        case PUNCT_PLUS:
            return cv_int(s, e->sem_type, fit(s, e->sem_type, o.i));
        case PUNCT_MINUS:
            /* SEMA-H-05: negating the target type's signed minimum has no
             * representable result. Unsigned `0 - bits` is safe compiler
             * arithmetic, but fitting that wrapped pattern would incorrectly
             * accept source-level signed overflow as a constant. */
            if (signed_minimum_value(s, e->sem_type, o.i)) {
                ce_error(s, m, e->span, "overflow in constant expression");
                return cv_error();
            }
            return cv_int(s, e->sem_type, fit(s, e->sem_type, 0 - o.i));
        case PUNCT_TILDE:
            return cv_int(s, e->sem_type, fit(s, e->sem_type, ~o.i));
        case PUNCT_BANG:
            return cv_int(s, type_basic(TY_INT), o.i == 0);
        default:
            ce_error(s, m, e->span,
                     "'%s' is not allowed in a constant expression",
                     ast_punct_name(e->op));
            return cv_error();
        }
    }
    case AST_EXPR_BINARY:
        return eval_binary(s, e, m);
    case AST_EXPR_COND: {
        ConstValue c = eval(s, e->lhs, m);

        if (c.kind == CV_ERROR)
            return c;
        /* Only the TAKEN branch is evaluated, so `1 ? 0 : 1/0` is a valid
         * constant expression.
         *
         * GNU `a ?: b` has no middle to evaluate: the condition's own value
         * IS the result, and it is already in hand. Re-evaluating e->lhs
         * would be correct here (a constant expression has no side effects
         * to duplicate) but returning `c` says what the form MEANS. */
        if (c.kind == CV_FLOAT)
            return sf_is_zero(c.f)
                       ? eval(s, e->rhs, m)
                       : (e->cond_omits_mid ? c : eval(s, e->mid, m));
        if (!c.i)
            return eval(s, e->rhs, m);
        return e->cond_omits_mid ? c : eval(s, e->mid, m);
    }
    case AST_EXPR_CAST: {
        Type *from = e->lhs ? e->lhs->sem_type : NULL;
        Type *to = e->sem_type;
        CeMode cast_mode = m == CE_ICE ? CE_ARITH : m;
        ConstValue o;

        /* A pointer-width integer initializer may carry a linker relocation.
         * Evaluate the pointer operand in address mode so the cast below can
         * retain that relocation; CE_ICE deliberately keeps rejecting it. */
        if (m == CE_ARITH && from && from->kind == TY_PTR && to &&
            type_is_integer(to))
            cast_mode = CE_ADDR;

        if (e->implicit && from && from->kind == TY_ARRAY && to &&
            to->kind == TY_PTR) {
            bool from_null = false;

            o = eval_lvalue_address(s, e->lhs, cast_mode, &from_null);
        } else {
            o = eval(s, e->lhs, cast_mode);
        }
        if (o.kind == CV_ERROR)
            return o;
        if (o.kind == CV_ADDR) {
            /* A pointer-to-integer cast in a static initializer is
             * accepted only at exactly pointer width; a truncating one
             * cannot be relocated at load time. */
            if (type_is_integer(to) &&
                conv_int_bits(s, to) <
                    cgf_target_layout(s->target).ptr_size * 8) {
                ce_error(s, m, e->span,
                         "initializer element is not computable at load "
                         "time: the cast to '%s' truncates an address",
                         type_to_str(s->arena, to));
                return cv_error();
            }
            o.type = to;
            return o;
        }
        if (type_is_integer(to)) {
            if (o.kind == CV_FLOAT) {
                SfStatus st;
                u64 iv;

                /* 6.6p6 allows a float only as the IMMEDIATE operand of an
                 * integer cast in an ICE. gcc folds the general case as an
                 * extension; matching that keeps real headers compiling. */
                if (m == CE_ICE && e->lhs->kind != AST_EXPR_FLOAT &&
                    s->lang->pedantic)
                    warn_at(
                        s->lang->warnings, WARN_PEDANTIC, e->span,
                        "a folded floating expression is not an ISO integer "
                        "constant expression");
                memset(&st, 0, sizeof(st));
                iv = sf_to_int(o.f, (int)conv_int_bits(s, to),
                               !conv_is_signed(s, to), &st);
                if (st.invalid) {
                    ce_error(s, m, e->span,
                             "the value is out of range for '%s'",
                             type_to_str(s->arena, to));
                    return cv_error();
                }
                return cv_int(s, to, fit(s, to, iv));
            }
            return cv_int(s, to, fit(s, to, o.i));
        }
        if (type_is_arithmetic(to)) {
            SfStatus st;
            ConstValue v;
            SfFormat f = constexpr_format_of(s, to);

            memset(&st, 0, sizeof(st));
            memset(&v, 0, sizeof(v));
            v.kind = CV_FLOAT;
            v.type = to;
            if (o.kind == CV_INT) {
                bool neg = conv_is_signed(s, o.type) && (i64)o.i < 0;
                u64 magnitude = neg ? 0 - o.i : o.i;

                v.f = sf_from_int(magnitude, neg, f, &st);
            } else {
                v.f = sf_convert(o.f, constexpr_format_of(s, o.type), f, &st);
            }
            return v;
        }
        if (to->kind == TY_PTR) {
            if (o.kind == CV_INT)
                return cv_int(s, to, o.i); /* an integer null pointer */
            o.type = to;
            return o;
        }
        ce_error(s, m, e->span, "invalid cast in a constant expression");
        return cv_error();
    }
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ALIGNOF: {
        Type *t = e->sem_operand_type;

        if (!t ||
            (!layout_is_complete_for_size(t) && !type_is_runtime_sized(t) &&
             !(e->kind == AST_EXPR_SIZEOF &&
               (t->kind == TY_VOID || t->kind == TY_FUNC)) &&
             !(e->kind == AST_EXPR_ALIGNOF &&
               (t->kind == TY_VOID || t->kind == TY_FUNC)))) {
            ce_error(s, m, e->span,
                     "invalid application of '%s' to an incomplete type",
                     e->kind == AST_EXPR_SIZEOF ? "sizeof" : "_Alignof");
            return cv_error();
        }
        /* A variably modified type's size is a RUNTIME value, so it is not
         * an ICE however it is spelled. */
        if (e->kind == AST_EXPR_SIZEOF && type_is_runtime_sized(t)) {
            ce_error(s, m, e->span,
                     "'sizeof' applied to a variably modified type is not "
                     "constant");
            return cv_error();
        }
        if (e->kind == AST_EXPR_SIZEOF)
            return cv_int(s, e->sem_type, layout_of(s, t).size);
        else {
            u64 align = layout_of(s, t).align;

            if (e->lhs && e->lhs->sem_lvalue_align)
                align = e->lhs->sem_lvalue_align;
            return cv_int(s, e->sem_type, align);
        }
    }
    case AST_EXPR_OFFSETOF: {
        u64 off = 0;

        if (!offsetof_walk(s, m, e->lhs, &off))
            return cv_error();
        return cv_int(s, e->sem_type, off);
    }
    case AST_EXPR_CALL: {
        /* These compiler-owned builtins have compile-time semantics.  Keep
         * constant_p's answer identical to lowering: addresses do not count
         * as known constants at this optimization-independent stage. */
        unsigned bytes = sema_builtin_bswap_bytes((u16)e->op);

        if (bytes && e->nargs == 1) {
            ConstValue a = eval(s, e->args[0], m);

            if (a.kind != CV_INT)
                return cv_error();
            return cv_int(s, e->sem_type, cgf_bswap(a.i, bytes));
        }
        if (e->op == SEMA_BUILTIN_CONSTANT_P && e->nargs == 1) {
            ConstValue a = eval(s, e->args[0], CE_FOLD);

            return cv_int(s, e->sem_type,
                          a.kind == CV_INT || a.kind == CV_FLOAT);
        }
        if (e->op == SEMA_BUILTIN_HUGE_VAL || e->op == SEMA_BUILTIN_HUGE_VALF ||
            e->op == SEMA_BUILTIN_INF || e->op == SEMA_BUILTIN_INFF ||
            e->op == SEMA_BUILTIN_NAN || e->op == SEMA_BUILTIN_NANF) {
            if (m == CE_ICE) {
                ce_error(s, m, e->span,
                         "a floating constant is not an integer constant "
                         "expression");
                return cv_error();
            }
            return cv_special_float(e->sem_type, (e->op == SEMA_BUILTIN_NAN ||
                                                  e->op == SEMA_BUILTIN_NANF)
                                                     ? SF_NAN
                                                     : SF_INF);
        }
        ce_error(s, m, e->span, "this is not a constant expression");
        return cv_error();
    }
    case AST_EXPR_INDEX:
    case AST_EXPR_MEMBER:
        ce_error(s, m, e->span, "this is not a constant expression");
        return cv_error();
    case AST_ERROR:
        return cv_error();
    default:
        ce_error(s, m, e->span, "this is not a constant expression");
        return cv_error();
    }
}

ConstValue constexpr_eval(Sema *s, AstNode *e, CeMode mode)
{
    return eval(s, e, mode);
}

bool sema_require_ice(Sema *s, AstNode *e, i64 *out, const char *what)
{
    ConstValue v;

    if (!e)
        return false;
    /* The expression must be TYPED before it can be folded: constant
     * contexts are reached from declarations, which may run before
     * expression sema has visited them. */
    if (!e->sem_type)
        (void)sema_expr(s, e);
    v = eval(s, e, CE_ICE);
    if (v.kind != CV_INT) {
        if (v.kind != CV_ERROR)
            ce_error(s, CE_ICE, e->span,
                     "%s must be an integer constant "
                     "expression",
                     what);
        return false;
    }
    *out = (i64)v.i;
    return true;
}

/* --- static initializer images ------------------------------------------- */

/* Evaluates an initializer into the exact bytes Sprint 19 emits.
 *
 * PADDING IS ZERO, always. gcc does the same, and it is the only choice
 * compatible with a byte-identical bootstrap: leaving padding
 * uninitialized would put whatever the arena happened to hold into the
 * object file. */

typedef struct {
    Type *type;
    u64 off;
    u64 member;
} InitUnionSelection;

typedef struct {
    Sema *s;
    InitImage *img;
    u64 semantic_size;
    bool ok;
    InitUnionSelection *unions;
    u32 nunions;
    u32 cap_unions;
} InitCtx;

static void img_zero(InitCtx *c, u64 off, u64 len)
{
    if (off + len <= c->img->size)
        memset(c->img->bytes + off, 0, (size_t)len);
}

static void img_put_int(InitCtx *c, u64 off, u64 value, u64 width)
{
    u64 i;

    if (width > sizeof(value)) {
        c->ok = false;
        return;
    }
    /* Little-endian: all five targets are. */
    for (i = 0; i < width && off + i < c->img->size; i++)
        c->img->bytes[off + i] = (u8)(value >> (i * 8));
}

static void img_clear_relocs(InitCtx *c, u64 off, u64 width)
{
    u32 from;
    u32 to = 0;

    for (from = 0; from < c->img->nrelocs; from++) {
        InitReloc r = c->img->relocs[from];

        if (r.offset < off + width && off < r.offset + 8)
            continue;
        c->img->relocs[to++] = r;
    }
    c->img->nrelocs = to;
}

static void img_reloc(InitCtx *c, u64 off, Symbol *sym, i64 addend,
                      const AstNode *anon)
{
    InitReloc *grown;
    u32 at = 0;

    /* Emitters walk relocations alongside bytes, so keep this list in image
     * order even when source designators move backwards.  A later
     * initializer of the same subobject replaces the earlier relocation. */
    img_clear_relocs(c, off, 8);
    while (at < c->img->nrelocs && c->img->relocs[at].offset < off)
        at++;

    grown = arena_alloc(c->s->arena, (c->img->nrelocs + 1) * sizeof(InitReloc),
                        _Alignof(InitReloc));
    if (at)
        memcpy(grown, c->img->relocs, at * sizeof(InitReloc));
    grown[at].offset = off;
    grown[at].sym = sym;
    grown[at].addend = addend;
    grown[at].anon = anon;
    if (at < c->img->nrelocs)
        memcpy(grown + at + 1, c->img->relocs + at,
               (c->img->nrelocs - at) * sizeof(InitReloc));
    c->img->relocs = grown;
    c->img->nrelocs++;
}

static void fill(InitCtx *c, Type *t, AstNode *init, u64 off);

/* Writes one scalar. A bitfield member is written by fill_record, which
 * knows the bit position; everything else lands on a byte boundary. */
static void fill_scalar(InitCtx *c, Type *t, AstNode *init, u64 off)
{
    Sema *s = c->s;
    ConstValue v;
    TypeLayout l;

    if (!init)
        return; /* already zeroed */
    if (!t || (!type_is_arithmetic(t) && t->kind != TY_PTR)) {
        c->ok = false;
        return;
    }
    l = layout_of(s, t);
    img_clear_relocs(c, off, l.size);
    v = constexpr_eval(s, init, t->kind == TY_PTR ? CE_ADDR : CE_ARITH);
    switch (v.kind) {
    case CV_INT:
        if (type_is_arithmetic(t) && !type_is_integer(t)) {
            uint8_t b[16];
            SfStatus st;
            SfFormat f = constexpr_format_of(s, t);
            bool neg = conv_is_signed(s, v.type) && (i64)v.i < 0;
            u64 magnitude = neg ? 0 - v.i : v.i;
            Sf converted;
            u64 i;

            memset(&st, 0, sizeof(st));
            converted = sf_from_int(magnitude, neg, f, &st);
            sf_to_bits(converted, f, b);
            for (i = 0; i < l.size && off + i < c->img->size; i++)
                c->img->bytes[off + i] = b[i];
            return;
        }
        img_put_int(c, off, v.i, l.size);
        return;
    case CV_FLOAT: {
        uint8_t b[16];
        SfStatus st;
        SfFormat f;
        Sf converted;
        u64 i;

        memset(&st, 0, sizeof(st));
        if (type_is_integer(t)) {
            u64 iv = sf_to_int(v.f, (int)conv_int_bits(s, t),
                               !conv_is_signed(s, t), &st);

            if (st.invalid) {
                c->ok = false;
                return;
            }
            img_put_int(c, off, iv, l.size);
            return;
        }
        f = constexpr_format_of(s, t);
        converted = sf_convert(v.f, constexpr_format_of(s, v.type), f, &st);
        sf_to_bits(converted, f, b);
        for (i = 0; i < l.size && off + i < c->img->size; i++)
            c->img->bytes[off + i] = b[i];
        return;
    }
    case CV_ADDR:
        /* The bytes stay zero; the relocation carries the address, which
         * only the linker can supply. */
        img_zero(c, off, l.size);
        img_reloc(c, off, v.sym, v.addend, v.anon);
        return;
    default:
        c->ok = false;
        return;
    }
}

/* `char s[4] = "abc"` copies the bytes and the terminator. `char s[3] =
 * "abc"` is LEGAL C — the terminator is simply dropped — while a longer
 * string than that is a warning in gcc rather than an error. */
static void fill_string(InitCtx *c, Type *t, AstNode *init, u64 off)
{
    const Token *tok = init->tok;
    u64 cap = t->has_size ? t->size : 0;
    u64 n;
    u64 i;

    if (!tok)
        return;
    /* A GNU static FAM initializer enlarges the emitted image without
     * completing the semantic array type. Its capacity is precisely the
     * payload appended beyond sizeof(record), not all bytes remaining after
     * the member offset (which would incorrectly count tail padding). */
    if (!t->has_size && t->base && c->img->size >= c->semantic_size) {
        TypeLayout elem = layout_of(c->s, t->base);

        if (elem.size)
            cap = (c->img->size - c->semantic_size) / elem.size;
    }
    /* A later designated initializer replaces the whole selected array
     * subobject, including any relocation left by an overlapping union
     * member.  The image starts zeroed, but source-order overrides do not. */
    img_clear_relocs(c, off, cap);
    img_zero(c, off, cap);
    n = tok->str.nbytes;
    if (n > cap) {
        warn_at(c->s->lang->warnings, WARN_INITIALIZER_STRING_TOO_LONG,
                init->span,
                "initializer-string for array of chars is too long");
        n = cap;
    }
    for (i = 0; i < n && off + i < c->img->size; i++)
        c->img->bytes[off + i] = tok->str.bytes[i];
    /* Everything past the copied bytes stays zero, which supplies the
     * terminator when there is room for one. */
}

#define FILL_CURSOR_MAX 256u

typedef struct {
    Type *aggregate;
    u64 off;
    u64 pos;
} FillCursorFrame;

typedef struct {
    FillCursorFrame frames[FILL_CURSOR_MAX];
    u32 depth;
    Type *current;
    u64 off;
    Member *member;
} FillCursor;

static bool fill_is_aggregate(const Type *t)
{
    return t &&
           (t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION);
}

static bool fill_cursor_select(InitCtx *c, FillCursor *cursor)
{
    FillCursorFrame *f;
    Member *m;
    u64 at = 0;

    cursor->current = NULL;
    cursor->member = NULL;
    if (cursor->depth == 0)
        return false;
    f = &cursor->frames[cursor->depth - 1];
    if (f->aggregate->kind == TY_ARRAY) {
        TypeLayout el;

        if (!f->aggregate->base ||
            (f->aggregate->has_size && f->pos >= f->aggregate->size))
            return false;
        el = layout_of(c->s, f->aggregate->base);
        cursor->current = f->aggregate->base;
        cursor->off = f->off + f->pos * el.size;
        return true;
    }
    if ((f->aggregate->kind != TY_STRUCT && f->aggregate->kind != TY_UNION) ||
        !f->aggregate->tag)
        return false;
    layout_record(c->s, f->aggregate);
    for (m = f->aggregate->tag->members; m; m = m->next) {
        if (m->is_bitfield && !m->name)
            continue;
        if (at++ != f->pos)
            continue;
        cursor->current = m->type;
        cursor->member = m;
        cursor->off = m->is_bitfield ? f->off : f->off + m->offset;
        return true;
    }
    return false;
}

static void fill_cursor_start(InitCtx *c, FillCursor *cursor, Type *root,
                              u64 off)
{
    memset(cursor, 0, sizeof(*cursor));
    cursor->frames[0].aggregate = root;
    cursor->frames[0].off = off;
    cursor->depth = 1;
    (void)fill_cursor_select(c, cursor);
}

static bool fill_cursor_descend(InitCtx *c, FillCursor *cursor)
{
    Type *aggregate = cursor->current;
    u64 off = cursor->off;

    if (!fill_is_aggregate(aggregate) || cursor->depth >= FILL_CURSOR_MAX)
        return false;
    cursor->frames[cursor->depth].aggregate = aggregate;
    cursor->frames[cursor->depth].off = off;
    cursor->frames[cursor->depth].pos = 0;
    cursor->depth++;
    return fill_cursor_select(c, cursor);
}

static void fill_cursor_advance(InitCtx *c, FillCursor *cursor)
{
    while (cursor->depth) {
        FillCursorFrame *f = &cursor->frames[cursor->depth - 1];

        if (f->aggregate->kind != TY_UNION) {
            f->pos++;
            if (fill_cursor_select(c, cursor))
                return;
        }
        cursor->depth--;
    }
    cursor->current = NULL;
    cursor->member = NULL;
}

static bool fill_member_position(Type *aggregate, const char *name, u64 *out)
{
    Member *m;
    u64 at = 0;

    if (!aggregate || !aggregate->tag || !name)
        return false;
    for (m = aggregate->tag->members; m; m = m->next) {
        if (m->is_bitfield && !m->name)
            continue;
        if (m->name == name) {
            *out = at;
            return true;
        }
        at++;
    }
    return false;
}

static bool fill_cursor_designate(InitCtx *c, FillCursor *cursor, Type *root,
                                  u64 off, const AstNode *item)
{
    u32 i;

    fill_cursor_start(c, cursor, root, off);
    for (i = 0; item && i < item->ndesignators; i++) {
        const AstNode *desig = item->designators[i];
        FillCursorFrame *f;
        u64 pos;

        if (!desig || cursor->depth == 0)
            return false;
        f = &cursor->frames[cursor->depth - 1];
        if (desig->desig_is_field) {
            if ((f->aggregate->kind != TY_STRUCT &&
                 f->aggregate->kind != TY_UNION) ||
                !fill_member_position(f->aggregate, desig->desig_field, &pos))
                return false;
        } else {
            i64 idx;

            if (f->aggregate->kind != TY_ARRAY || !desig->desig_index)
                return false;
            if (desig->desig_bounds_checked) {
                if (!desig->desig_bounds_valid)
                    return false;
                idx = desig->desig_index_value;
            } else if (!sema_require_ice(c->s, desig->desig_index, &idx,
                                         "an array designator") ||
                       idx < 0) {
                return false;
            }
            pos = (u64)idx;
        }
        f->pos = pos;
        if (!fill_cursor_select(c, cursor))
            return false;
        if (i + 1 < item->ndesignators) {
            if (!fill_is_aggregate(cursor->current) ||
                cursor->depth >= FILL_CURSOR_MAX)
                return false;
            cursor->frames[cursor->depth].aggregate = cursor->current;
            cursor->frames[cursor->depth].off = cursor->off;
            cursor->frames[cursor->depth].pos = 0;
            cursor->depth++;
        }
    }
    return true;
}

static bool fill_expr_initializes_whole(Type *target, const AstNode *init)
{
    if (!target || !init)
        return false;
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_STRING)
        return true;
    return init->sem_type && type_compatible(target, init->sem_type);
}

static void fill_activate_union(InitCtx *c, Type *type, u64 off, u64 member)
{
    u32 i;

    if (!type || type->kind != TY_UNION)
        return;
    for (i = 0; i < c->nunions; i++) {
        InitUnionSelection *sel = &c->unions[i];

        if (sel->type != type || sel->off != off)
            continue;
        if (sel->member != member) {
            TypeLayout l = layout_of(c->s, type);

            img_clear_relocs(c, off, l.size);
            img_zero(c, off, l.size);
            sel->member = member;
        }
        return;
    }
    if (c->nunions == c->cap_unions) {
        u32 cap = c->cap_unions ? c->cap_unions * 2 : 8;
        InitUnionSelection *grown = arena_alloc(
            c->s->arena, cap * sizeof(*grown), _Alignof(InitUnionSelection));

        if (c->nunions)
            memcpy(grown, c->unions, c->nunions * sizeof(*grown));
        c->unions = grown;
        c->cap_unions = cap;
    }
    c->unions[c->nunions].type = type;
    c->unions[c->nunions].off = off;
    c->unions[c->nunions].member = member;
    c->nunions++;
}

static void fill_activate_cursor_unions(InitCtx *c, const FillCursor *cursor)
{
    u32 i;

    for (i = 0; i < cursor->depth; i++) {
        const FillCursorFrame *f = &cursor->frames[i];

        if (f->aggregate->kind == TY_UNION)
            fill_activate_union(c, f->aggregate, f->off, f->pos);
    }
}

static void fill_bitfield(InitCtx *c, const FillCursor *cursor, AstNode *item)
{
    Member *m = cursor->member;
    i64 value;
    u32 b;

    if (!m || !m->is_bitfield ||
        !sema_require_ice(c->s, item, &value, "a bit-field initializer"))
        return;
    /* Clear exactly the selected bit-field before setting its new value.
     * This preserves neighboring fields in the same container while making
     * a later union/member designator replace, rather than OR with, the
     * earlier representation.  Any relocation overlapping those bytes can
     * no longer survive the scalar write. */
    if (m->bit_width) {
        u64 first_byte = (cursor->off * 8 + m->bit_offset) / 8;
        u64 last_bit = cursor->off * 8 + m->bit_offset + m->bit_width - 1;

        img_clear_relocs(c, first_byte, last_bit / 8 - first_byte + 1);
    }
    for (b = 0; b < m->bit_width; b++) {
        u64 abs_bit = cursor->off * 8 + m->bit_offset + b;
        u64 byte = abs_bit / 8;
        u8 mask = (u8)(1u << (abs_bit % 8));

        if (byte >= c->img->size)
            break;
        c->img->bytes[byte] &= (u8)~mask;
        if (((u64)value >> b) & 1)
            c->img->bytes[byte] |= mask;
    }
}

static void fill_cursor_value(InitCtx *c, const FillCursor *cursor,
                              AstNode *item)
{
    if (!cursor->current) {
        c->ok = false;
        return;
    }
    fill_activate_cursor_unions(c, cursor);
    if (fill_is_aggregate(cursor->current) &&
        (item->kind == AST_INIT_LIST ||
         fill_expr_initializes_whole(cursor->current, item))) {
        TypeLayout l = layout_of(c->s, cursor->current);

        img_clear_relocs(c, cursor->off, l.size);
        img_zero(c, cursor->off, l.size);
    }
    if (cursor->member && cursor->member->is_bitfield)
        fill_bitfield(c, cursor, item);
    else
        fill(c, cursor->current, item, cursor->off);
}

static void fill_aggregate_list(InitCtx *c, Type *t, AstNode *init, u64 off)
{
    FillCursor cursor;
    u32 k;

    fill_cursor_start(c, &cursor, t, off);
    for (k = 0; k < init->nitems; k++) {
        AstNode *item = init->items[k];

        if (!item)
            continue;
        if (item->ndesignators &&
            !fill_cursor_designate(c, &cursor, t, off, item)) {
            c->ok = false;
            continue;
        }
        if (!cursor.current) {
            c->ok = false;
            continue;
        }
        if (item->kind == AST_INIT_LIST) {
            fill_cursor_value(c, &cursor, item);
            fill_cursor_advance(c, &cursor);
            continue;
        }
        while (fill_is_aggregate(cursor.current) &&
               !fill_expr_initializes_whole(cursor.current, item)) {
            if (!fill_cursor_descend(c, &cursor)) {
                c->ok = false;
                break;
            }
        }
        fill_cursor_value(c, &cursor, item);
        fill_cursor_advance(c, &cursor);
    }
}

static void fill_array(InitCtx *c, Type *t, AstNode *init, u64 off)
{
    if (!t->base)
        return;
    if (init->kind == AST_EXPR_STRING) {
        fill_string(c, t, init, off);
        return;
    }
    if (init->kind != AST_INIT_LIST) {
        c->ok = false;
        return;
    }
    fill_aggregate_list(c, t, init, off);
}

static void fill_record(InitCtx *c, Type *t, AstNode *init, u64 off)
{
    if (!t->tag)
        return;
    layout_record(c->s, t);
    if (init->kind != AST_INIT_LIST) {
        /* `struct S a = b;` copies a whole object, which is not a
         * constant unless b is — and b being an object makes it not one. */
        c->ok = false;
        return;
    }
    fill_aggregate_list(c, t, init, off);
}

static void fill(InitCtx *c, Type *t, AstNode *init, u64 off)
{
    if (!t || !init)
        return;
    /* A GNU cast from a scalar union member type is the static-image
     * equivalent of a member-designated compound literal. Reuse the
     * initializer cursor so relocatable pointer members, union selection,
     * and deterministic zeroed padding follow the ordinary initializer
     * path. Aggregate-valued casts remain nonconstant, as in GCC. */
    if (t->kind == TY_UNION && init->kind == AST_EXPR_CAST && !init->implicit &&
        init->lhs && init->sem_type && type_compatible(t, init->sem_type)) {
        Member *selected = type_union_cast_member(t, init->lhs->sem_type);

        if (selected && (type_is_arithmetic(selected->type) ||
                         selected->type->kind == TY_PTR)) {
            FillCursor cursor;
            Member *m;
            u64 pos = 0;

            for (m = t->tag->members; m && m != selected; m = m->next) {
                if (m->is_bitfield && !m->name)
                    continue;
                pos++;
            }
            fill_cursor_start(c, &cursor, t, off);
            cursor.frames[0].pos = pos;
            if (!fill_cursor_select(c, &cursor)) {
                c->ok = false;
                return;
            }
            fill_cursor_value(c, &cursor, init->lhs);
            return;
        }
    }
    /* INITIALIZING AN AGGREGATE FROM A COMPOUND LITERAL OF ITS OWN TYPE is
     * initializing it from that literal's braces:
     *
     *     static struct S x = (struct S){ 1, 2 };
     *
     * gcc accepts it and emits the same .data bytes as `= { 1, 2 }`. Only
     * for an AGGREGATE target -- a POINTER target takes the literal's
     * ADDRESS instead, which is the decay case in eval() and reaches here
     * through fill_scalar, so unwrapping there would silently store the
     * pointee where a pointer belongs. */
    if (init->kind == AST_EXPR_COMPOUND_LIT && init->init && init->sem_type &&
        (t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION)) {
        bool compatible =
            t->kind == TY_ARRAY
                ? type_array_initializer_compatible(t, init->sem_type)
                : type_compatible(init->sem_type, t);

        if (compatible)
            init = init->init;
    }
    switch (t->kind) {
    case TY_ARRAY:
        fill_array(c, t, init, off);
        return;
    case TY_STRUCT:
    case TY_UNION:
        fill_record(c, t, init, off);
        return;
    default:
        /* A braced scalar initializer (`int x = {1}`) is legal. */
        if (init->kind == AST_INIT_LIST) {
            if (init->nitems > 0)
                fill_scalar(c, t, init->items[0], off);
            return;
        }
        fill_scalar(c, t, init, off);
        return;
    }
}

bool constexpr_eval_initializer_sized(Sema *s, Type *type, AstNode *init,
                                      u64 storage_size, InitImage *out)
{
    InitCtx c;
    TypeLayout l;

    memset(&c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    if (!type || !layout_is_complete_for_size(type))
        return false;
    l = layout_of(s, type);
    if (storage_size == 0)
        storage_size = l.size;
    if (storage_size < l.size || storage_size > SIZE_MAX)
        return false;
    out->size = storage_size;
    out->bytes =
        arena_alloc(s->arena, storage_size ? (size_t)storage_size : 1, 16);
    /* Zero FIRST, so every byte the initializer does not mention — every
     * padding byte, every trailing element — is deterministic. */
    memset(out->bytes, 0, storage_size ? (size_t)storage_size : 1);

    c.s = s;
    c.img = out;
    c.semantic_size = l.size;
    c.ok = true;
    if (init)
        fill(&c, type, init, 0);
    (void)img_zero;
    return c.ok;
}

bool constexpr_eval_initializer(Sema *s, Type *type, AstNode *init,
                                InitImage *out)
{
    return constexpr_eval_initializer_sized(s, type, init, 0, out);
}

/* -fdump-init. The bytes are printed in memory order so a fixture can be
 * compared byte-for-byte against what gcc puts in .data. */
void constexpr_dump_initializers(Sema *s, AstNode *tu, FILE *f)
{
    u32 i;

    if (!tu)
        return;
    for (i = 0; i < tu->ndecls; i++) {
        AstNode *d = tu->decls[i];
        Symbol *sym;
        InitImage img;
        u64 k;

        if (!d || d->kind != AST_DECL || !d->name || !d->init)
            continue;
        if (d->storage & AST_SC_TYPEDEF)
            continue;
        sym = scope_lookup(s->file_scope, d->name, NS_ORDINARY);
        if (!sym || !sym->type)
            continue;
        if (!constexpr_eval_initializer_sized(s, sym->type, d->init,
                                              sym->init_storage_size, &img)) {
            fprintf(f, "%s: <not a constant initializer>\n", d->name);
            continue;
        }
        fprintf(f, "%s: size=%llu bytes=", d->name,
                (unsigned long long)img.size);
        for (k = 0; k < img.size; k++)
            fprintf(f, "%02X", img.bytes[k]);
        for (k = 0; k < img.nrelocs; k++)
            fprintf(f, " reloc@%llu=%s%+lld",
                    (unsigned long long)img.relocs[k].offset,
                    img.relocs[k].sym && img.relocs[k].sym->name
                        ? img.relocs[k].sym->name
                        : "<string>",
                    (long long)img.relocs[k].addend);
        fprintf(f, "\n");
    }
}
