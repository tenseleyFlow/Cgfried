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
        return SF_BINARY32;
    case TY_DOUBLE:
        return SF_BINARY64;
    case TY_LDOUBLE:
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

    if (st.overflow)
        warn_at(s->lang->warnings, WARN_OVERFLOW, e->span,
                "floating constant exceeds range of '%s'",
                type_to_str(s->arena, e->sem_type));
    else if (st.underflow && st.inexact)
        warn_at(s->lang->warnings, WARN_OVERFLOW, e->span,
                "floating constant truncated to zero");
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
    return m != CE_FOLD;
}

static void ce_error(Sema *s, CeMode m, Span sp, const char *fmt, ...)
{
    va_list ap;
    char msg[512];

    if (!is_required(m))
        return; /* CE_FOLD fails silently: it is opportunistic */
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    s->nerrors++;
    diag_emit(s->dc, DIAG_ERROR, sp, "%s", msg);
}

static ConstValue eval(Sema *s, AstNode *e, CeMode m);

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

static ConstValue eval_binary(Sema *s, AstNode *e, CeMode m)
{
    ConstValue l = eval(s, e->lhs, m);
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

    r = eval(s, e->rhs, m);
    if (r.kind == CV_ERROR)
        return r;

    /* Address constant arithmetic: `&g + 3` and `arr + 3`. */
    if (l.kind == CV_ADDR || r.kind == CV_ADDR) {
        ConstValue addr = l.kind == CV_ADDR ? l : r;
        ConstValue off = l.kind == CV_ADDR ? r : l;
        u64 scale = 1;

        if (m != CE_ADDR && m != CE_FOLD) {
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
        /* Detect signed multiply overflow by dividing back out. */
        if (conv_is_signed(s, t) && l.i != 0) {
            i64 li = (i64)l.i, ri = (i64)r.i;

            if (li != 0 && ((i64)res) / li != ri) {
                ce_error(s, m, e->span, "overflow in constant expression");
                return cv_error();
            }
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
    case AST_EXPR_PAREN:
        return eval(s, e->lhs, m);
    case AST_EXPR_IDENT: {
        Symbol *sym = e->sym;
        ConstValue v;

        if (sym && sym->kind == SYM_ENUM_CONST)
            return cv_int(s, e->sem_type, (u64)sym->enum_value);
        /* A FUNCTION or an array designator is an address constant even
         * without an explicit `&`. */
        if (sym && (sym->kind == SYM_FUNC ||
                    (sym->type && sym->type->kind == TY_ARRAY))) {
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
            if (m != CE_ADDR && m != CE_FOLD) {
                ce_error(s, m, e->span,
                         "an address is not an integer constant expression");
                return cv_error();
            }
            memset(&v, 0, sizeof(v));
            v.kind = CV_ADDR;
            v.type = e->sem_type;
            if (inner && inner->kind == AST_EXPR_IDENT && inner->sym) {
                if (inner->sym->linkage == LINK_NONE && !inner->sym->is_param &&
                    s->scope != s->file_scope) {
                    ce_error(s, m, e->span,
                             "initializer element is not computable at load "
                             "time: '%s' has automatic storage duration",
                             inner->name);
                    return cv_error();
                }
                v.sym = inner->sym;
                return v;
            }
            if (inner && inner->kind == AST_EXPR_MEMBER) {
                /* `&g.member`: the same symbol at a constant offset. */
                ConstValue base = eval(s, inner->lhs, CE_ADDR);
                Member *mem;

                if (base.kind != CV_ADDR)
                    return cv_error();
                mem = NULL;
                if (inner->lhs->sem_type && inner->lhs->sem_type->tag) {
                    Member *it;

                    layout_record(s, inner->lhs->sem_type);
                    for (it = inner->lhs->sem_type->tag->members; it;
                         it = it->next)
                        if (it->name == inner->name) {
                            mem = it;
                            break;
                        }
                }
                base.addend += mem ? (i64)mem->offset : 0;
                base.type = e->sem_type;
                return base;
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
            if (inner && inner->kind == AST_EXPR_INDEX) {
                /* `&arr[k]` folds through the standard identity. */
                ConstValue base = eval(s, inner->lhs, CE_ADDR);
                ConstValue idx = eval(s, inner->rhs, CE_ICE);
                u64 scale = 1;

                if (base.kind != CV_ADDR || idx.kind != CV_INT)
                    return cv_error();
                if (inner->lhs->sem_type &&
                    inner->lhs->sem_type->kind == TY_PTR)
                    scale = layout_of(s, inner->lhs->sem_type->base).size;
                base.addend += (i64)idx.i * (i64)scale;
                base.type = e->sem_type;
                return base;
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
         * constant expression. */
        if (c.kind == CV_FLOAT)
            return eval(s, sf_is_zero(c.f) ? e->rhs : e->mid, m);
        return eval(s, c.i ? e->mid : e->rhs, m);
    }
    case AST_EXPR_CAST: {
        ConstValue o = eval(s, e->lhs, m == CE_ICE ? CE_ARITH : m);
        Type *to = e->sem_type;

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
        Type *t = e->type ? sema_type_from_ast(s, e->type, e->span)
                          : (e->lhs ? e->lhs->sem_type : NULL);

        if (!t || (!layout_is_complete_for_size(t) &&
                   !(e->kind == AST_EXPR_SIZEOF &&
                     (t->kind == TY_VOID || t->kind == TY_FUNC)))) {
            ce_error(s, m, e->span,
                     "invalid application of '%s' to an incomplete type",
                     e->kind == AST_EXPR_SIZEOF ? "sizeof" : "_Alignof");
            return cv_error();
        }
        /* A VLA's size is a RUNTIME value, so it is not an ICE however it
         * is spelled. */
        if (t->kind == TY_ARRAY && t->is_vla) {
            ce_error(s, m, e->span,
                     "'sizeof' applied to a variable-length array is not "
                     "constant");
            return cv_error();
        }
        return cv_int(s, e->sem_type,
                      e->kind == AST_EXPR_SIZEOF ? layout_of(s, t).size
                                                 : layout_of(s, t).align);
    }
    case AST_EXPR_OFFSETOF: {
        u64 off = 0;

        if (!offsetof_walk(s, m, e->lhs, &off))
            return cv_error();
        return cv_int(s, e->sem_type, off);
    }
    case AST_EXPR_INDEX:
    case AST_EXPR_MEMBER:
    case AST_EXPR_CALL:
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
    Sema *s;
    InitImage *img;
    bool ok;
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

static void img_reloc(InitCtx *c, u64 off, Symbol *sym, i64 addend,
                      const AstNode *anon)
{
    InitReloc *grown;

    grown = arena_alloc(c->s->arena, (c->img->nrelocs + 1) * sizeof(InitReloc),
                        _Alignof(InitReloc));
    if (c->img->nrelocs)
        memcpy(grown, c->img->relocs, c->img->nrelocs * sizeof(InitReloc));
    grown[c->img->nrelocs].offset = off;
    grown[c->img->nrelocs].sym = sym;
    grown[c->img->nrelocs].addend = addend;
    grown[c->img->nrelocs].anon = anon;
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

            if (f->aggregate->kind != TY_ARRAY || !desig->desig_index ||
                !sema_require_ice(c->s, desig->desig_index, &idx,
                                  "an array designator") ||
                idx < 0)
                return false;
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

static void fill_bitfield(InitCtx *c, const FillCursor *cursor, AstNode *item)
{
    Member *m = cursor->member;
    i64 value;
    u32 b;

    if (!m || !m->is_bitfield ||
        !sema_require_ice(c->s, item, &value, "a bit-field initializer"))
        return;
    for (b = 0; b < m->bit_width; b++) {
        u64 abs_bit = cursor->off * 8 + m->bit_offset + b;
        u64 byte = abs_bit / 8;

        if (byte >= c->img->size)
            break;
        if (((u64)value >> b) & 1)
            c->img->bytes[byte] |= (u8)(1u << (abs_bit % 8));
    }
}

static void fill_cursor_value(InitCtx *c, const FillCursor *cursor,
                              AstNode *item)
{
    if (!cursor->current) {
        c->ok = false;
        return;
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

bool constexpr_eval_initializer(Sema *s, Type *type, AstNode *init,
                                InitImage *out)
{
    InitCtx c;
    TypeLayout l;

    memset(out, 0, sizeof(*out));
    if (!type || !layout_is_complete_for_size(type))
        return false;
    l = layout_of(s, type);
    out->size = l.size;
    out->bytes = arena_alloc(s->arena, l.size ? (size_t)l.size : 1, 16);
    /* Zero FIRST, so every byte the initializer does not mention — every
     * padding byte, every trailing element — is deterministic. */
    memset(out->bytes, 0, l.size ? (size_t)l.size : 1);

    c.s = s;
    c.img = out;
    c.ok = true;
    if (init)
        fill(&c, type, init, 0);
    (void)img_zero;
    return c.ok;
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
        if (!constexpr_eval_initializer(s, sym->type, d->init, &img)) {
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
