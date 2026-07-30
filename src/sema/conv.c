#include <string.h>

#include "sema/sema.h"

/* C11 6.3: every implicit conversion, plus the operator-level rules that
 * consume them.
 *
 * Two decisions shape this file.
 *
 * First, conversions are MATERIALIZED as AST_EXPR_CAST nodes carrying
 * `implicit`. Nothing downstream re-derives them: IR lowering reads the
 * tree, and a conversion that exists only as a rule is one that gets
 * applied twice or not at all depending on who remembers.
 *
 * Second, every width and signedness question goes through TargetSpec.
 * Plain `char` is signed on x86_64 and UNSIGNED on arm64-linux, so a
 * `#ifdef` here would silently miscompile every cross-compilation — which
 * is why the CI gate rejects one in src/sema/. */

Type *conv_strip_quals(Sema *s, const Type *t)
{
    Type *out;

    if (!t || t->quals == 0)
        return (Type *)t;
    out = arena_alloc(s->arena, sizeof(Type), _Alignof(Type));
    *out = *t;
    out->quals = 0;
    return out;
}

/* Wraps `e` in an implicit cast to `to`. The span is the operand's, so a
 * later diagnostic about the conversion points at the code that caused
 * it rather than at nothing. */
AstNode *conv_cast(Sema *s, AstNode *e, Type *to)
{
    AstNode *c;

    if (!e || !to)
        return e;
    /* Skip a cast that would convert a type to ITSELF. Comparing by
     * pointer is not enough: derived types are allocated per use, so two
     * structurally identical `int *` nodes are different pointers and a
     * pointer test emitted `(icast<int *> (icast<int *> a))` — visible in
     * the -fdump-sema goldens, and pure noise for every later pass. */
    if (e->sem_type == to || type_compatible(e->sem_type, to))
        return e;
    c = ast_new(s->arena, AST_EXPR_CAST, e->span);
    c->lhs = e;
    c->sem_type = to;
    c->implicit = true;
    c->is_lvalue = false; /* the result of a conversion is never an lvalue */
    c->poisoned = e->poisoned;
    return c;
}

/* --- integer rank (6.3.1.1p1) -------------------------------------------- */

/* Rank orders the integer types for the UAC. `char`, `signed char` and
 * `unsigned char` share a rank; so do each signed type and its unsigned
 * counterpart. _Bool ranks below all of them. */
int conv_rank(const Type *t)
{
    if (!t)
        return 0;
    switch (t->kind) {
    case TY_BOOL:
        return 1;
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
        return 2;
    case TY_SHORT:
    case TY_USHORT:
        return 3;
    case TY_INT:
    case TY_UINT:
        return 4;
    case TY_LONG:
    case TY_ULONG:
        return 5;
    case TY_LLONG:
    case TY_ULLONG:
        return 6;
    case TY_ENUM:
        return 4; /* ranks with its compatible type */
    default:
        return 0;
    }
}

bool conv_is_signed(Sema *s, const Type *t)
{
    IntWidths w = cgf_target_int_widths(s->target);

    if (!t)
        return false;
    switch (t->kind) {
    case TY_BOOL:
        return false;
    /* THE per-target question: plain char is a distinct type from both
     * signed and unsigned char, and its signedness is the target's
     * choice. Asking the host here would miscompile every cross build. */
    case TY_CHAR:
        return w.char_signed;
    case TY_SCHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_LLONG:
        return true;
    case TY_ENUM:
        return t->tag && t->tag->enum_underlying
                   ? conv_is_signed(s, t->tag->enum_underlying)
                   : true;
    default:
        return false;
    }
}

u32 conv_int_bits(Sema *s, const Type *t)
{
    IntWidths w = cgf_target_int_widths(s->target);

    if (!t)
        return 0;
    switch (t->kind) {
    case TY_BOOL:
        return 1;
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
        return w.char_bits;
    case TY_SHORT:
    case TY_USHORT:
        return w.short_bits;
    case TY_INT:
    case TY_UINT:
        return w.int_bits;
    case TY_LONG:
    case TY_ULONG:
        return w.long_bits;
    case TY_LLONG:
    case TY_ULLONG:
        return w.llong_bits;
    case TY_ENUM:
        return t->tag && t->tag->enum_underlying
                   ? conv_int_bits(s, t->tag->enum_underlying)
                   : w.int_bits;
    default:
        return 0;
    }
}

/* The unsigned type corresponding to a signed one (UAC rule e). */
static Type *unsigned_counterpart(const Type *t)
{
    switch (t->kind) {
    case TY_SCHAR:
    case TY_CHAR:
        return type_basic(TY_UCHAR);
    case TY_SHORT:
        return type_basic(TY_USHORT);
    case TY_INT:
        return type_basic(TY_UINT);
    case TY_LONG:
        return type_basic(TY_ULONG);
    case TY_LLONG:
        return type_basic(TY_ULLONG);
    default:
        return (Type *)t;
    }
}

/* --- lvalue, decay, promotion -------------------------------------------- */

AstNode *conv_lvalue(Sema *s, AstNode *e)
{
    Type *t;

    if (!e || !e->sem_type)
        return e;
    if (!e->is_lvalue)
        return e;
    t = e->sem_type;
    /* Lvalue conversion drops TOP-LEVEL qualifiers (6.3.2.1p2): the VALUE
     * of a const int is just an int. Qualifiers below the top level are
     * part of the type and must survive — `const char *` stays that. */
    if (t->quals) {
        AstNode *c = conv_cast(s, e, conv_strip_quals(s, t));

        return c;
    }
    e->is_lvalue = false;
    return e;
}

AstNode *conv_decay(Sema *s, AstNode *e)
{
    Type *t;

    if (!e || !e->sem_type)
        return e;
    t = e->sem_type;
    if (t->kind == TY_ARRAY) {
        /* An array becomes a pointer to its first element, and the
         * ELEMENT's qualifiers ride along: `const char a[4]` decays to
         * `const char *`. */
        Type *p = type_ptr(s->arena, t->base);
        AstNode *c = conv_cast(s, e, p);

        c->is_lvalue = false;
        return c;
    }
    if (t->kind == TY_FUNC) {
        AstNode *c = conv_cast(s, e, type_ptr(s->arena, t));

        c->is_lvalue = false;
        return c;
    }
    return conv_lvalue(s, e);
}

Type *conv_promote_type(Sema *s, Type *t)
{
    IntWidths w = cgf_target_int_widths(s->target);

    if (!t)
        return t;
    if (t->kind == TY_ENUM)
        return t->tag && t->tag->enum_underlying ? t->tag->enum_underlying
                                                 : type_basic(TY_INT);
    if (!type_is_integer(t) || conv_rank(t) >= conv_rank(type_basic(TY_INT)))
        return conv_strip_quals(s, t);
    /* 6.3.1.1p2: a type of lesser rank promotes to int if int represents
     * every one of its values, else to unsigned int. On all five targets
     * short is 16 bits and int is 32, so `unsigned short` promotes to
     * SIGNED int — the sign surprise behind `(us1 - us2) < 0` being a
     * meaningful test rather than dead code. */
    if (conv_int_bits(s, t) < w.int_bits ||
        (conv_int_bits(s, t) == w.int_bits && conv_is_signed(s, t)))
        return type_basic(TY_INT);
    return type_basic(TY_UINT);
}

AstNode *conv_promote(Sema *s, AstNode *e)
{
    Type *promoted;

    if (!e || !e->sem_type)
        return e;
    e = conv_decay(s, e);
    promoted = conv_promote_type(s, e->sem_type);
    if (promoted == e->sem_type)
        return e;
    return conv_cast(s, e, promoted);
}

/* --- usual arithmetic conversions (6.3.1.8) ------------------------------ */

/* The ordered algorithm, written out rather than shortcut. "Unsigned
 * wins" is FALSE: on LP64, `long` vs `unsigned int` yields `long`,
 * because a 64-bit long represents every unsigned int (rule d). The
 * shortcut only looks right because rank and width usually agree. */
Type *conv_uac_type(Sema *s, Type *a, Type *b)
{
    int ra, rb;
    bool sa, sb;

    if (!a || !b)
        return a ? a : b;
    if (a->kind == TY_ERROR || b->kind == TY_ERROR)
        return type_basic(TY_ERROR);

    /* 1-3: any floating operand pulls the other up, widest first. */
    if (a->kind == TY_LDOUBLE || b->kind == TY_LDOUBLE)
        return type_basic(TY_LDOUBLE);
    if (a->kind == TY_DOUBLE || b->kind == TY_DOUBLE)
        return type_basic(TY_DOUBLE);
    if (a->kind == TY_FLOAT || b->kind == TY_FLOAT)
        return type_basic(TY_FLOAT);

    /* 4: promote both, then compare rank and signedness. */
    a = conv_promote_type(s, a);
    b = conv_promote_type(s, b);
    if (a->kind == b->kind)
        return a; /* (a) same type */

    ra = conv_rank(a);
    rb = conv_rank(b);
    sa = conv_is_signed(s, a);
    sb = conv_is_signed(s, b);

    if (sa == sb) /* (b) same signedness: the higher rank */
        return ra >= rb ? a : b;

    {
        Type *uns = sa ? b : a; /* the unsigned one */
        Type *sig = sa ? a : b; /* the signed one */
        int runs = sa ? rb : ra;
        int rsig = sa ? ra : rb;

        /* (c) the unsigned type's rank is >= the signed one's. */
        if (runs >= rsig)
            return uns;
        /* (d) the signed type represents every value of the unsigned one —
         * strictly more bits, since equal width with a sign bit cannot. */
        if (conv_int_bits(s, sig) > conv_int_bits(s, uns))
            return sig;
        /* (e) otherwise the unsigned counterpart of the signed type. */
        return unsigned_counterpart(sig);
    }
}

Type *conv_uac(Sema *s, AstNode **a, AstNode **b)
{
    Type *result;

    *a = conv_decay(s, *a);
    *b = conv_decay(s, *b);
    result = conv_uac_type(s, (*a)->sem_type, (*b)->sem_type);
    *a = conv_cast(s, *a, result);
    *b = conv_cast(s, *b, result);
    return result;
}

AstNode *conv_to_bool(Sema *s, AstNode *e)
{
    if (!e || !e->sem_type)
        return e;
    e = conv_decay(s, e);
    /* 6.3.1.2: conversion to _Bool is `!= 0`, NOT truncation. `(_Bool)0.5`
     * is 1; a naive float-to-int truncation would give 0. The cast node
     * records the intent and lowering emits the comparison. */
    return conv_cast(s, e, type_basic(TY_BOOL));
}

/* --- null pointer constants (6.3.2.3p3) ---------------------------------- */

/* An NPC is an integer constant expression with value 0, or such an
 * expression cast to `void *`. `(char *)0` is a null POINTER but not a
 * null pointer CONSTANT — the distinction decides `?:` result types and
 * varargs sentinels, so it is worth keeping straight.
 *
 * Full ICE evaluation is Sprint 15's. Until then literal `0` and
 * `(void *)0` are recognized syntactically, and anything else that looks
 * like a candidate reports rather than being silently accepted. */
bool conv_is_npc(Sema *s, const AstNode *e)
{
    if (!e)
        return false;
    while (e->kind == AST_EXPR_PAREN ||
           (e->kind == AST_EXPR_CAST && e->implicit))
        e = e->lhs;
    if (e->kind == AST_EXPR_INT)
        return e->tok && e->tok->int_val == 0;
    if (e->kind == AST_EXPR_CAST && e->sem_type &&
        e->sem_type->kind == TY_PTR && e->sem_type->base &&
        e->sem_type->base->kind == TY_VOID)
        return conv_is_npc(s, e->lhs);
    return false;
}

/* --- pointer assignment compatibility (6.5.16.1) ------------------------- */

static bool pointee_quals_ok(const Type *lhs_pointee, const Type *rhs_pointee)
{
    /* The lhs pointee must have ALL the rhs pointee's qualifiers: `const
     * char *` may take a `char *`, never the reverse. */
    return (rhs_pointee->quals & ~lhs_pointee->quals) == 0;
}

static bool is_void_ptr(const Type *t)
{
    return t && t->kind == TY_PTR && t->base && t->base->kind == TY_VOID;
}

static bool is_object_ptr(const Type *t)
{
    return t && t->kind == TY_PTR && t->base && t->base->kind != TY_FUNC;
}

static const char *ctx_phrase(AssignCtx ctx)
{
    switch (ctx.kind) {
    case ACTX_ASSIGN:
        return "assignment";
    case ACTX_INIT:
        return "initialization";
    case ACTX_ARG:
        return "passing argument";
    case ACTX_RETURN:
        return "returning";
    }
    return "assignment";
}

/* gcc's wording, so a user moving between compilers reads the same
 * sentence: "assignment to X from Y", "passing argument N of 'f'", and so
 * on. The ctx parameter exists precisely so one function serves all four. */
static void assign_diag(Sema *s, DiagLevel lvl, Span sp, AssignCtx ctx,
                        const char *what, const Type *lhs, const Type *rhs)
{
    char *ls = type_to_str(s->arena, lhs);
    char *rs = type_to_str(s->arena, rhs);

    if (lvl == DIAG_ERROR)
        s->nerrors++;
    switch (ctx.kind) {
    case ACTX_ARG:
        diag_emit(s->dc, lvl, sp,
                  "%s: passing argument %u of '%s' from incompatible type "
                  "'%s' (expected '%s')",
                  what, (unsigned)ctx.arg_index,
                  ctx.callee ? ctx.callee : "<expression>", rs, ls);
        return;
    case ACTX_RETURN:
        diag_emit(s->dc, lvl, sp,
                  "%s: returning '%s' from a function with return type '%s'",
                  what, rs, ls);
        return;
    default:
        diag_emit(s->dc, lvl, sp, "%s: %s to '%s' from '%s'", what,
                  ctx_phrase(ctx), ls, rs);
        return;
    }
}

bool conv_assignable(Sema *s, Type *lhs, AstNode **rhs_slot, AssignCtx ctx)
{
    AstNode *rhs = *rhs_slot;
    Type *rt;

    if (!lhs || !rhs || !rhs->sem_type)
        return true;
    if (lhs->kind == TY_ERROR)
        return true;

    *rhs_slot = rhs = conv_decay(s, rhs);
    rt = rhs->sem_type;
    if (!rt || rt->kind == TY_ERROR)
        return true;

    /* _Bool takes any scalar (6.3.1.2), via != 0 rather than truncation. */
    if (lhs->kind == TY_BOOL &&
        (type_is_arithmetic(rt) || rt->kind == TY_PTR)) {
        *rhs_slot = conv_to_bool(s, rhs);
        return true;
    }

    if (type_is_arithmetic(lhs) && type_is_arithmetic(rt)) {
        *rhs_slot = conv_cast(s, rhs, conv_strip_quals(s, lhs));
        return true;
    }

    if (lhs->kind == TY_PTR && rt->kind == TY_PTR) {
        Type *lp = lhs->base;
        Type *rp = rt->base;
        Type *lp_bare = conv_strip_quals(s, lp);
        Type *rp_bare = conv_strip_quals(s, rp);

        /* `void *` converts to and from any OBJECT pointer with no cast,
         * in BOTH directions. C++ requires a cast here and refugees
         * routinely "fix" this; the fixtures exist to stop that. */
        if ((is_void_ptr(lhs) && is_object_ptr(rt)) ||
            (is_void_ptr(rt) && is_object_ptr(lhs))) {
            if (!pointee_quals_ok(lp, rp)) {
                assign_diag(s, DIAG_WARNING, rhs->span, ctx,
                            "warning [-Wdiscarded-qualifiers]", lhs, rt);
            }
            *rhs_slot = conv_cast(s, rhs, lhs);
            return true;
        }
        if (type_compatible(lp_bare, rp_bare)) {
            /* Compatible pointees: only the QUALIFIERS can still be
             * wrong, and dropping one is a warning in gcc 8 — real code
             * depends on that staying a warning. */
            if (!pointee_quals_ok(lp, rp))
                assign_diag(s, DIAG_WARNING, rhs->span, ctx,
                            "warning [-Wdiscarded-qualifiers]", lhs, rt);
            *rhs_slot = conv_cast(s, rhs, lhs);
            return true;
        }
        assign_diag(s, DIAG_WARNING, rhs->span, ctx,
                    "warning [-Wincompatible-pointer-types]", lhs, rt);
        *rhs_slot = conv_cast(s, rhs, lhs);
        return true;
    }

    if (lhs->kind == TY_PTR && type_is_integer(rt)) {
        if (conv_is_npc(s, rhs)) {
            *rhs_slot = conv_cast(s, rhs, lhs); /* NPC -> any pointer: OK */
            return true;
        }
        assign_diag(s, DIAG_WARNING, rhs->span, ctx,
                    "warning [-Wint-conversion]", lhs, rt);
        *rhs_slot = conv_cast(s, rhs, lhs);
        return true;
    }
    if (type_is_integer(lhs) && rt->kind == TY_PTR) {
        assign_diag(s, DIAG_WARNING, rhs->span, ctx,
                    "warning [-Wint-conversion]", lhs, rt);
        *rhs_slot = conv_cast(s, rhs, lhs);
        return true;
    }

    /* Structs and unions: compatible or nothing, and this one IS an error
     * in gcc — there is no plausible reinterpretation to warn about. */
    if (lhs->kind == TY_STRUCT || lhs->kind == TY_UNION ||
        rt->kind == TY_STRUCT || rt->kind == TY_UNION) {
        if (type_compatible(conv_strip_quals(s, lhs), conv_strip_quals(s, rt)))
            return true;
        assign_diag(s, DIAG_ERROR, rhs->span, ctx, "error", lhs, rt);
        return false;
    }

    assign_diag(s, DIAG_ERROR, rhs->span, ctx, "error", lhs, rt);
    return false;
}
