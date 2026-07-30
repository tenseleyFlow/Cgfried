#include <string.h>

#include "sema/sema.h"
#include "util/dlev.h"

/* Expression typing. Each node gets a sem_type and an is_lvalue bit, and
 * every operand is rewritten with the conversions its operator demands.
 *
 * Values are NOT folded here — Sprint 15 owns that — so this pass answers
 * "what type" and never "what value". The one exception is the null
 * pointer constant, where the syntactic forms `0` and `(void *)0` are
 * recognized because assignment compatibility cannot be decided without
 * them; anything else defers rather than guessing. */

static AstNode *expr(Sema *s, AstNode *e);

static AstNode *poison(Sema *s, AstNode *e)
{
    if (e) {
        e->sem_type = type_basic(TY_ERROR);
        e->is_lvalue = false;
        e->poisoned = true;
    }
    (void)s;
    return e;
}

/* Sprint 11's contract: never emit a diagnostic about an already
 * -diagnosed subtree. Checking here rather than at every call site is
 * what makes the contract hold by construction. */
static bool quiet(const AstNode *a, const AstNode *b)
{
    if (a && (a->poisoned || (a->sem_type && a->sem_type->kind == TY_ERROR)))
        return true;
    if (b && (b->poisoned || (b->sem_type && b->sem_type->kind == TY_ERROR)))
        return true;
    return false;
}

static void err(Sema *s, Span sp, const char *fmt, ...)
{
    va_list ap;
    char msg[512];

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    s->nerrors++;
    diag_emit(s->dc, DIAG_ERROR, sp, "%s", msg);
}

/* --- identifiers, with "did you mean" ------------------------------------ */

/* Scans every VISIBLE ordinary identifier for a close spelling. Sprint 12
 * put the same machinery on unknown TYPE names in the parser; this is the
 * case the sprint file actually asked for, and it only becomes possible
 * now that identifiers resolve. Restricted to NS_ORDINARY: suggesting a
 * struct tag where a value belongs would point the reader somewhere the
 * name cannot go. */
static const char *suggest_ident(Sema *s, const char *typo)
{
    size_t tlen = strlen(typo);
    const char *best = NULL;
    unsigned best_d = 0;
    Scope *sc;

    for (sc = s->scope; sc; sc = sc->parent) {
        Symbol *sym;

        for (sym = sc->ordinary; sym; sym = sym->next) {
            unsigned d;

            if (!sym->name)
                continue;
            if (!dlev_is_suggestion(typo, tlen, sym->name, strlen(sym->name),
                                    &d))
                continue;
            /* `<=` breaks ties toward the EARLIEST declaration: the
             * chain runs newest-first, so the last match at a given
             * distance is the oldest. Two candidates one edit away is
             * common (`gj` reaches both `gi` and `ga`), and picking by
             * declaration order is both deterministic and the answer a
             * reader expects. */
            if (!best || d <= best_d) {
                best = sym->name;
                best_d = d;
            }
        }
    }
    return best;
}

static AstNode *expr_ident(Sema *s, AstNode *e)
{
    Symbol *sym = scope_lookup(s->scope, e->name, NS_ORDINARY);

    if (!sym) {
        const char *guess = suggest_ident(s, e->name);

        if (guess)
            err(s, e->span, "'%s' undeclared; did you mean '%s'?", e->name,
                guess);
        else
            err(s, e->span, "'%s' undeclared", e->name);
        return poison(s, e);
    }
    e->sym = sym;
    e->sem_type = sym->type;
    /* 6.7.4p3: an inline definition may not reference an identifier with
     * internal linkage. gcc warns; matched by observation. */
    if (s->cur_inline_candidate && sym->linkage == LINK_INTERNAL)
        diag_emit(s->dc, DIAG_WARNING, e->span,
                  "'%s' is static but used in inline function '%s' which "
                  "is not static",
                  e->name, s->cur_fname ? s->cur_fname : "?");
    /* An enum CONSTANT is a value, not an object — assigning to one is a
     * different error than assigning to a const object, so the lvalue bit
     * has to distinguish them here. Functions ARE lvalues (of function
     * type), which is what lets `&f` and a bare `f` both work. */
    e->is_lvalue = sym->kind != SYM_ENUM_CONST;
    return e;
}

/* --- literals ------------------------------------------------------------ */

static Type *int_literal_type(Sema *s, const Token *t)
{
    (void)s;
    switch ((IntConstType)t->int_type) {
    case ITY_INT:
        return type_basic(TY_INT);
    case ITY_UINT:
        return type_basic(TY_UINT);
    case ITY_LONG:
        return type_basic(TY_LONG);
    case ITY_ULONG:
        return type_basic(TY_ULONG);
    case ITY_LLONG:
        return type_basic(TY_LLONG);
    case ITY_ULLONG:
        return type_basic(TY_ULLONG);
    }
    return type_basic(TY_INT);
}

/* --- operators ----------------------------------------------------------- */

static bool is_ptr(const Type *t)
{
    return t && t->kind == TY_PTR;
}

static AstNode *expr_unary(Sema *s, AstNode *e)
{
    AstNode *op;

    /* `&x` does NOT decay its operand, and neither does sizeof — those are
     * the two contexts where an array stays an array (6.3.2.1p3). */
    if (e->op == PUNCT_AMP && !e->is_postfix) {
        op = expr(s, e->lhs);
        e->lhs = op;
        if (quiet(op, NULL))
            return poison(s, e);
        if (!op->is_lvalue && op->sem_type->kind != TY_FUNC) {
            err(s, e->span,
                "cannot take the address of an rvalue of type "
                "'%s'",
                type_to_str(s->arena, op->sem_type));
            return poison(s, e);
        }
        /* `&arr` is `T (*)[N]`, NOT `T **` — the single most common
         * confusion in this area, and the reason decay is suppressed. */
        e->sem_type = type_ptr(s->arena, op->sem_type);
        e->is_lvalue = false;
        return e;
    }

    op = expr(s, e->lhs);
    if (quiet(op, NULL)) {
        e->lhs = op;
        return poison(s, e);
    }

    switch (e->op) {
    case PUNCT_STAR:
        e->lhs = op = conv_decay(s, op);
        if (!is_ptr(op->sem_type)) {
            err(s, e->span, "cannot dereference a value of type '%s'",
                type_to_str(s->arena, op->sem_type));
            return poison(s, e);
        }
        if (op->sem_type->base->kind == TY_VOID) {
            err(s, e->span, "cannot dereference a 'void *'");
            return poison(s, e);
        }
        e->sem_type = op->sem_type->base;
        e->is_lvalue = true; /* *p is an object */
        return e;
    case PUNCT_BANG:
        e->lhs = conv_decay(s, op);
        /* `!` yields int, not _Bool — this is C, not C++. */
        e->sem_type = type_basic(TY_INT);
        e->is_lvalue = false;
        return e;
    case PUNCT_PLUS:
    case PUNCT_MINUS:
    case PUNCT_TILDE:
        e->lhs = op = conv_promote(s, op);
        if (!type_is_arithmetic(op->sem_type) ||
            (e->op == PUNCT_TILDE && !type_is_integer(op->sem_type))) {
            err(s, e->span, "invalid operand of type '%s' to unary '%s'",
                type_to_str(s->arena, op->sem_type), ast_punct_name(e->op));
            return poison(s, e);
        }
        e->sem_type = op->sem_type;
        e->is_lvalue = false;
        return e;
    case PUNCT_PLUSPLUS:
    case PUNCT_MINUSMINUS:
        /* ++ and -- read AND write, so the operand stays an lvalue and is
         * never converted — the result type is the operand's own type. */
        if (!op->is_lvalue) {
            err(s, e->span, "'%s' requires a modifiable lvalue",
                ast_punct_name(e->op));
            return poison(s, e);
        }
        if (op->sem_type->quals & CGF_QUAL_CONST) {
            err(s, e->span,
                "cannot modify a const-qualified value of type "
                "'%s'",
                type_to_str(s->arena, op->sem_type));
            return poison(s, e);
        }
        if (!type_is_arithmetic(op->sem_type) && !is_ptr(op->sem_type)) {
            err(s, e->span, "invalid operand of type '%s' to '%s'",
                type_to_str(s->arena, op->sem_type), ast_punct_name(e->op));
            return poison(s, e);
        }
        e->lhs = op;
        e->sem_type = conv_strip_quals(s, op->sem_type);
        e->is_lvalue = false;
        return e;
    default:
        e->lhs = op;
        e->sem_type = op->sem_type;
        e->is_lvalue = false;
        return e;
    }
}

static bool is_assign_op(u16 op)
{
    switch (op) {
    case PUNCT_ASSIGN:
    case PUNCT_STAR_ASSIGN:
    case PUNCT_SLASH_ASSIGN:
    case PUNCT_PERCENT_ASSIGN:
    case PUNCT_PLUS_ASSIGN:
    case PUNCT_MINUS_ASSIGN:
    case PUNCT_SHL_ASSIGN:
    case PUNCT_SHR_ASSIGN:
    case PUNCT_AMP_ASSIGN:
    case PUNCT_CARET_ASSIGN:
    case PUNCT_PIPE_ASSIGN:
        return true;
    default:
        return false;
    }
}

static AstNode *expr_assign(Sema *s, AstNode *e)
{
    AstNode *lhs = expr(s, e->lhs);
    AstNode *rhs = expr(s, e->rhs);
    AssignCtx ctx;

    e->lhs = lhs;
    e->rhs = rhs;
    if (quiet(lhs, rhs))
        return poison(s, e);

    /* The lhs of an assignment is NOT lvalue-converted: it names the
     * object being written. Everything that makes it unwritable is an
     * error, not a warning — gcc agrees. */
    if (!lhs->is_lvalue) {
        err(s, e->span, "assignment to an rvalue of type '%s'",
            type_to_str(s->arena, lhs->sem_type));
        return poison(s, e);
    }
    if (lhs->sem_type->kind == TY_ARRAY) {
        err(s, e->span, "assignment to an array type '%s'",
            type_to_str(s->arena, lhs->sem_type));
        return poison(s, e);
    }
    if (lhs->sem_type->quals & CGF_QUAL_CONST) {
        err(s, e->span, "assignment to a const-qualified type '%s'",
            type_to_str(s->arena, lhs->sem_type));
        return poison(s, e);
    }
    if (!type_is_complete(lhs->sem_type)) {
        err(s, e->span, "assignment to an incomplete type '%s'",
            type_to_str(s->arena, lhs->sem_type));
        return poison(s, e);
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = ACTX_ASSIGN;
    if (e->op == PUNCT_ASSIGN) {
        conv_assignable(s, lhs->sem_type, &e->rhs, ctx);
    } else {
        /* A compound assignment converts as if by the operator then back;
         * the checks that matter here are the same ones. */
        e->rhs = conv_decay(s, e->rhs);
        if (!type_is_arithmetic(lhs->sem_type) && !is_ptr(lhs->sem_type)) {
            err(s, e->span, "invalid operands to '%s'", ast_punct_name(e->op));
            return poison(s, e);
        }
    }
    /* The VALUE of an assignment has the lhs type with qualifiers dropped,
     * and is not an lvalue — `(a = b) = c` is an error in C. */
    e->sem_type = conv_strip_quals(s, lhs->sem_type);
    e->is_lvalue = false;
    return e;
}

static AstNode *expr_binary(Sema *s, AstNode *e)
{
    AstNode *lhs;
    AstNode *rhs;
    Type *lt;
    Type *rt;

    if (is_assign_op(e->op))
        return expr_assign(s, e);

    lhs = expr(s, e->lhs);
    rhs = expr(s, e->rhs);
    e->lhs = lhs;
    e->rhs = rhs;
    if (quiet(lhs, rhs))
        return poison(s, e);

    if (e->op == PUNCT_COMMA) {
        /* The comma operator's value is the RIGHT operand, lvalue-
         * converted; the result is never an lvalue in C. */
        e->rhs = rhs = conv_decay(s, rhs);
        e->sem_type = rhs->sem_type;
        e->is_lvalue = false;
        return e;
    }

    if (e->op == PUNCT_AMPAMP || e->op == PUNCT_PIPEPIPE) {
        e->lhs = conv_decay(s, lhs);
        e->rhs = conv_decay(s, rhs);
        e->sem_type = type_basic(TY_INT); /* int, not _Bool */
        e->is_lvalue = false;
        return e;
    }

    if (e->op == PUNCT_SHL || e->op == PUNCT_SHR) {
        /* THE shift rule: the result type is the PROMOTED LEFT OPERAND
         * alone. The right operand is promoted independently and takes no
         * part in the UAC, so `1 << 40L` has type int — not long. Getting
         * this wrong makes Sprint 15 fold at the wrong width. */
        e->lhs = lhs = conv_promote(s, lhs);
        e->rhs = rhs = conv_promote(s, rhs);
        if (!type_is_integer(lhs->sem_type) ||
            !type_is_integer(rhs->sem_type)) {
            err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
                ast_punct_name(e->op), type_to_str(s->arena, lhs->sem_type),
                type_to_str(s->arena, rhs->sem_type));
            return poison(s, e);
        }
        e->sem_type = lhs->sem_type;
        e->is_lvalue = false;
        return e;
    }

    e->lhs = lhs = conv_decay(s, lhs);
    e->rhs = rhs = conv_decay(s, rhs);
    lt = lhs->sem_type;
    rt = rhs->sem_type;

    /* Pointer arithmetic and comparison, before the UAC gets a look. */
    if (is_ptr(lt) || is_ptr(rt)) {
        bool is_cmp = e->op == PUNCT_EQEQ || e->op == PUNCT_NOTEQ ||
                      e->op == PUNCT_LT || e->op == PUNCT_GT ||
                      e->op == PUNCT_LE || e->op == PUNCT_GE;

        if (is_cmp) {
            if (is_ptr(lt) && is_ptr(rt)) {
                Type *lb = conv_strip_quals(s, lt->base);
                Type *rb = conv_strip_quals(s, rt->base);

                if (!type_compatible(lb, rb) && lt->base->kind != TY_VOID &&
                    rt->base->kind != TY_VOID)
                    diag_emit(s->dc, DIAG_WARNING, e->span,
                              "warning [-Wcompare-distinct-pointer-types]: "
                              "comparison of distinct pointer types ('%s' "
                              "and '%s')",
                              type_to_str(s->arena, lt),
                              type_to_str(s->arena, rt));
            } else if (!conv_is_npc(s, is_ptr(lt) ? rhs : lhs)) {
                diag_emit(s->dc, DIAG_WARNING, e->span,
                          "warning [-Wint-conversion]: comparison between "
                          "pointer and integer ('%s' and '%s')",
                          type_to_str(s->arena, lt), type_to_str(s->arena, rt));
            }
            e->sem_type = type_basic(TY_INT);
            e->is_lvalue = false;
            return e;
        }
        if (e->op == PUNCT_PLUS || e->op == PUNCT_MINUS) {
            if (is_ptr(lt) && is_ptr(rt)) {
                if (e->op != PUNCT_MINUS) {
                    err(s, e->span,
                        "invalid operands to '+' ('%s' and "
                        "'%s')",
                        type_to_str(s->arena, lt), type_to_str(s->arena, rt));
                    return poison(s, e);
                }
                /* Pointer difference has type ptrdiff_t; naming it needs
                 * the target's size type, which is Sprint 14's. */
                e->sem_type = type_basic(TY_LONG);
                e->is_lvalue = false;
                return e;
            }
            if (!type_is_integer(is_ptr(lt) ? rt : lt)) {
                err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
                    ast_punct_name(e->op), type_to_str(s->arena, lt),
                    type_to_str(s->arena, rt));
                return poison(s, e);
            }
            e->sem_type = is_ptr(lt) ? lt : rt;
            e->is_lvalue = false;
            return e;
        }
        err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
            ast_punct_name(e->op), type_to_str(s->arena, lt),
            type_to_str(s->arena, rt));
        return poison(s, e);
    }

    if (!type_is_arithmetic(lt) || !type_is_arithmetic(rt)) {
        err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
            ast_punct_name(e->op), type_to_str(s->arena, lt),
            type_to_str(s->arena, rt));
        return poison(s, e);
    }
    if ((e->op == PUNCT_PERCENT || e->op == PUNCT_AMP || e->op == PUNCT_PIPE ||
         e->op == PUNCT_CARET) &&
        (!type_is_integer(lt) || !type_is_integer(rt))) {
        err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
            ast_punct_name(e->op), type_to_str(s->arena, lt),
            type_to_str(s->arena, rt));
        return poison(s, e);
    }

    {
        Type *common = conv_uac(s, &e->lhs, &e->rhs);

        switch (e->op) {
        case PUNCT_LT:
        case PUNCT_GT:
        case PUNCT_LE:
        case PUNCT_GE:
        case PUNCT_EQEQ:
        case PUNCT_NOTEQ:
            /* Comparisons still perform the UAC on their operands, but
             * the RESULT is int. */
            e->sem_type = type_basic(TY_INT);
            break;
        default:
            e->sem_type = common;
            break;
        }
    }
    e->is_lvalue = false;
    return e;
}

/* 6.5.15. The pointer rules are where gcc's RECOVERY matters: a mismatch
 * is a warning with a `void *` result, not an error, and real code
 * depends on that. */
static AstNode *expr_cond(Sema *s, AstNode *e)
{
    AstNode *c = expr(s, e->lhs);
    AstNode *a;
    AstNode *b;
    Type *at;
    Type *bt;

    e->lhs = conv_decay(s, c);
    a = expr(s, e->mid);
    b = expr(s, e->rhs);
    e->mid = a = conv_decay(s, a);
    e->rhs = b = conv_decay(s, b);
    if (quiet(a, b) || quiet(c, NULL))
        return poison(s, e);

    at = a->sem_type;
    bt = b->sem_type;
    e->is_lvalue = false;

    if (type_is_arithmetic(at) && type_is_arithmetic(bt)) {
        e->sem_type = conv_uac(s, &e->mid, &e->rhs);
        return e;
    }
    if (at->kind == TY_VOID && bt->kind == TY_VOID) {
        e->sem_type = type_basic(TY_VOID);
        return e;
    }
    if ((at->kind == TY_STRUCT || at->kind == TY_UNION) &&
        type_compatible(conv_strip_quals(s, at), conv_strip_quals(s, bt))) {
        e->sem_type = at;
        return e;
    }
    if (is_ptr(at) && is_ptr(bt)) {
        Type *ab = conv_strip_quals(s, at->base);
        Type *bb = conv_strip_quals(s, bt->base);
        unsigned quals = at->base->quals | bt->base->quals;

        if (type_compatible(ab, bb)) {
            Type *composite = type_composite(s->arena, ab, bb);

            e->sem_type =
                type_ptr(s->arena, type_qualify(s->arena, composite, quals));
            return e;
        }
        if (at->base->kind == TY_VOID || bt->base->kind == TY_VOID) {
            e->sem_type = type_ptr(
                s->arena, type_qualify(s->arena, type_basic(TY_VOID), quals));
            return e;
        }
        /* gcc's recovery: warn and yield `void *` rather than erroring.
         * Matching this is the deliverable — erroring here would reject
         * code that builds everywhere else. */
        diag_emit(s->dc, DIAG_WARNING, e->span,
                  "warning [-Wincompatible-pointer-types]: pointer type "
                  "mismatch in conditional expression ('%s' and '%s')",
                  type_to_str(s->arena, at), type_to_str(s->arena, bt));
        e->sem_type = type_ptr(s->arena, type_basic(TY_VOID));
        return e;
    }
    if (is_ptr(at) || is_ptr(bt)) {
        AstNode *intside = is_ptr(at) ? b : a;
        Type *ptype = is_ptr(at) ? at : bt;

        if (conv_is_npc(s, intside)) {
            /* One side a null pointer constant: the result is the other
             * side's type, and the NPC is CONVERTED to it — lowering must
             * not have to rediscover that the 0 is a pointer. */
            if (is_ptr(at))
                e->rhs = conv_cast(s, e->rhs, ptype);
            else
                e->mid = conv_cast(s, e->mid, ptype);
            e->sem_type = ptype;
            return e;
        }
        diag_emit(s->dc, DIAG_WARNING, e->span,
                  "warning [-Wint-conversion]: pointer/integer type mismatch "
                  "in conditional expression ('%s' and '%s')",
                  type_to_str(s->arena, at), type_to_str(s->arena, bt));
        e->sem_type = ptype;
        return e;
    }

    err(s, e->span,
        "incompatible operand types in a conditional expression "
        "('%s' and '%s')",
        type_to_str(s->arena, at), type_to_str(s->arena, bt));
    return poison(s, e);
}

/* C11 6.7.2.1p13: an ANONYMOUS struct or union member's own members are
 * considered members of the containing structure, so `v.c` reaches into
 * an unnamed `union { int c; int d; };`. The search therefore recurses
 * through unnamed aggregate members — a named nested struct is NOT
 * transparent, which is why the recursion is gated on `!m->name`. */
static Member *find_member(const Type *t, const char *name)
{
    Member *m;

    if (!t || !t->tag)
        return NULL;
    for (m = t->tag->members; m; m = m->next) {
        if (m->name == name)
            return m;
        if (!m->name && m->type &&
            (m->type->kind == TY_STRUCT || m->type->kind == TY_UNION)) {
            Member *inner = find_member(m->type, name);

            if (inner)
                return inner;
        }
    }
    return NULL;
}

static AstNode *expr_member(Sema *s, AstNode *e)
{
    AstNode *obj = expr(s, e->lhs);
    Type *ot;
    Member *m;

    e->lhs = obj;
    if (quiet(obj, NULL))
        return poison(s, e);

    if (e->is_arrow) {
        e->lhs = obj = conv_decay(s, obj);
        if (!is_ptr(obj->sem_type)) {
            err(s, e->span, "'->' applied to a non-pointer of type '%s'",
                type_to_str(s->arena, obj->sem_type));
            return poison(s, e);
        }
        ot = obj->sem_type->base;
    } else {
        ot = obj->sem_type;
    }

    if (ot->kind != TY_STRUCT && ot->kind != TY_UNION) {
        err(s, e->span, "member access into a non-struct type '%s'",
            type_to_str(s->arena, ot));
        return poison(s, e);
    }
    if (!type_is_complete(ot)) {
        err(s, e->span, "member access into an incomplete type '%s'",
            type_to_str(s->arena, ot));
        return poison(s, e);
    }
    m = find_member(ot, e->name);
    if (!m) {
        err(s, e->span, "'%s' has no member named '%s'",
            type_to_str(s->arena, ot), e->name ? e->name : "?");
        return poison(s, e);
    }
    /* The member inherits the OBJECT's qualifiers: a member of a const
     * struct is const, which is what stops `cs.m = 1`. */
    e->sem_type =
        ot->quals ? type_qualify(s->arena, m->type, ot->quals) : m->type;
    /* `f().m` is not an lvalue even though it has struct type — the
     * lvalue bit has to come from the object, not from the member. */
    e->is_lvalue = e->is_arrow ? true : obj->is_lvalue;
    return e;
}

static AstNode *expr_index(Sema *s, AstNode *e)
{
    AstNode *base = conv_decay(s, expr(s, e->lhs));
    AstNode *idx = conv_decay(s, expr(s, e->rhs));

    e->lhs = base;
    e->rhs = idx;
    if (quiet(base, idx))
        return poison(s, e);

    /* `a[i]` is `*(a + i)`, so either operand may be the pointer. */
    if (is_ptr(base->sem_type) && type_is_integer(idx->sem_type)) {
        e->sem_type = base->sem_type->base;
    } else if (type_is_integer(base->sem_type) && is_ptr(idx->sem_type)) {
        e->sem_type = idx->sem_type->base;
    } else {
        err(s, e->span, "invalid subscript of '%s' by '%s'",
            type_to_str(s->arena, base->sem_type),
            type_to_str(s->arena, idx->sem_type));
        return poison(s, e);
    }
    e->is_lvalue = true;
    return e;
}

static AstNode *expr_call(Sema *s, AstNode *e)
{
    AstNode *callee = conv_decay(s, expr(s, e->lhs));
    Type *ft;
    u32 i;
    const char *callee_name = NULL;

    e->lhs = callee;
    if (quiet(callee, NULL)) {
        for (i = 0; i < e->nargs; i++)
            e->args[i] = expr(s, e->args[i]);
        return poison(s, e);
    }
    if (e->lhs->kind == AST_EXPR_CAST && e->lhs->lhs &&
        e->lhs->lhs->kind == AST_EXPR_IDENT)
        callee_name = e->lhs->lhs->name;

    ft = callee->sem_type;
    if (!is_ptr(ft) || ft->base->kind != TY_FUNC) {
        err(s, e->span, "called object of type '%s' is not a function",
            type_to_str(s->arena, ft));
        for (i = 0; i < e->nargs; i++)
            e->args[i] = expr(s, e->args[i]);
        return poison(s, e);
    }
    ft = ft->base;

    for (i = 0; i < e->nargs; i++) {
        AstNode *arg = conv_decay(s, expr(s, e->args[i]));

        e->args[i] = arg;
        if (ft->has_proto && i < ft->nparams) {
            AssignCtx ctx;

            memset(&ctx, 0, sizeof(ctx));
            ctx.kind = ACTX_ARG;
            ctx.arg_index = i + 1; /* gcc numbers arguments from 1 */
            ctx.callee = callee_name;
            conv_assignable(s, ft->params[i], &e->args[i], ctx);
        } else {
            /* Unprototyped or variadic: the DEFAULT ARGUMENT PROMOTIONS
             * apply — integers promote and float becomes double. */
            e->args[i] = conv_promote(s, e->args[i]);
            if (e->args[i]->sem_type->kind == TY_FLOAT)
                e->args[i] = conv_cast(s, e->args[i], type_basic(TY_DOUBLE));
        }
    }
    if (ft->has_proto) {
        if (e->nargs < ft->nparams)
            err(s, e->span,
                "too few arguments to function%s%s%s: expected "
                "%u, have %u",
                callee_name ? " '" : "", callee_name ? callee_name : "",
                callee_name ? "'" : "", (unsigned)ft->nparams,
                (unsigned)e->nargs);
        else if (e->nargs > ft->nparams && !ft->variadic)
            err(s, e->span,
                "too many arguments to function%s%s%s: expected "
                "%u, have %u",
                callee_name ? " '" : "", callee_name ? callee_name : "",
                callee_name ? "'" : "", (unsigned)ft->nparams,
                (unsigned)e->nargs);
    }
    e->sem_type = ft->base;
    /* A function call is never an lvalue, even returning a struct. */
    e->is_lvalue = false;
    return e;
}

static AstNode *expr_sizeof(Sema *s, AstNode *e)
{
    /* sizeof does NOT decay its operand and does not evaluate it, so the
     * operand is typed only to surface errors inside it.
     *
     * The TYPE of the result is size_t and needs no layout at all — only
     * the VALUE does, and that is Sprint 15's folding over Sprint 14's
     * sizes. Typing it here rather than deferring the whole construct is
     * what keeps every program that merely MENTIONS sizeof compiling.
     * size_t is unsigned long on all five (LP64) targets; Sprint 14 gives
     * it its proper name. */
    if (e->lhs) {
        AstNode *saved = e->lhs;

        e->lhs = expr(s, saved);
    }
    {
        Type *operand = e->type ? sema_type_from_ast(s, e->type, e->span)
                                : (e->lhs ? e->lhs->sem_type : NULL);

        /* A VLA operand is COMPLETE — its size just is not a constant.
         * sizeof evaluates it at runtime, and 6.5.3.4p2 makes the operand
         * itself EVALUATED (sizeof(int[f()]) calls f), so the unevaluated
         * flag the parser set comes OFF; Sprint 18 lowers the side
         * effects, Sprint 15's folder already refuses to fold it. */
        if (operand && operand->kind == TY_ARRAY && operand->is_vla) {
            if (e->lhs)
                e->lhs->unevaluated = false;
            e->unevaluated = false;
            e->sem_type = type_basic(TY_ULONG);
            e->is_lvalue = false;
            return e;
        }
        /* An incomplete operand has no size — an error at the point that
         * demanded it, never a silent zero. */
        if (operand && !layout_is_complete_for_size(operand)) {
            err(s, e->span,
                "invalid application of '%s' to incomplete type '%s'",
                e->kind == AST_EXPR_SIZEOF ? "sizeof" : "_Alignof",
                type_to_str(s->arena, operand));
            return poison(s, e);
        }
    }
    e->sem_type = type_basic(TY_ULONG);
    e->is_lvalue = false;
    return e;
}

/* 6.5.1.1: the controlling expression is NOT evaluated; only its type
 * matters, and that type is taken AFTER lvalue conversion — qualifiers
 * dropped, array to pointer, function to pointer. The C17 wording made
 * this explicit; C11 as published was ambiguous and both gcc and clang
 * already behaved this way. */
static AstNode *expr_generic(Sema *s, AstNode *e)
{
    AstNode *ctrl = conv_decay(s, expr(s, e->lhs));
    Type *ct;
    AstNode *chosen = NULL;
    AstNode *fallback = NULL;
    u32 i;

    e->lhs = ctrl;
    if (quiet(ctrl, NULL))
        return poison(s, e);
    ct = conv_strip_quals(s, ctrl->sem_type);

    for (i = 0; i < e->nitems; i++) {
        AstNode *assoc = e->items[i];
        Type *at;
        u32 j;

        if (!assoc)
            continue;
        if (!assoc->type) {
            fallback = assoc; /* the `default:` association */
            continue;
        }
        at = sema_type_from_ast(s, assoc->type, assoc->span);
        assoc->sem_type = at;
        /* No two associations may name compatible types — otherwise the
         * selection would be ambiguous rather than merely unmatched. */
        for (j = 0; j < i; j++)
            if (e->items[j] && e->items[j]->sem_type && e->items[j]->type &&
                type_compatible(conv_strip_quals(s, e->items[j]->sem_type),
                                conv_strip_quals(s, at))) {
                err(s, assoc->span,
                    "_Generic association for '%s' duplicates an earlier one",
                    type_to_str(s->arena, at));
                break;
            }
        if (!chosen && type_compatible(conv_strip_quals(s, at), ct))
            chosen = assoc;
    }

    if (!chosen)
        chosen = fallback;
    if (!chosen) {
        err(s, e->span,
            "no _Generic association matches the controlling type '%s'",
            type_to_str(s->arena, ct));
        return poison(s, e);
    }
    /* ONLY the selected association is typed as a value: the others are
     * not evaluated and must not produce diagnostics (6.5.1.1p3). */
    chosen->lhs = conv_decay(s, expr(s, chosen->lhs));
    e->sem_type = chosen->lhs->sem_type;
    e->is_lvalue = false;
    e->mid = chosen->lhs; /* what lowering will emit */
    return e;
}

static AstNode *expr(Sema *s, AstNode *e)
{
    if (!e)
        return e;
    if (e->sem_type)
        return e; /* already typed (a conversion wrapper) */

    switch (e->kind) {
    case AST_EXPR_INT:
        e->sem_type = int_literal_type(s, e->tok);
        return e;
    case AST_EXPR_CHAR:
        /* A character constant has type int in C (6.4.4.4p10) — not char,
         * which is the C++ rule and a classic source of sizeof surprises. */
        e->sem_type = type_basic(TY_INT);
        return e;
    case AST_EXPR_FLOAT:
        e->sem_type = e->tok->float_type == 0   ? type_basic(TY_FLOAT)
                      : e->tok->float_type == 1 ? type_basic(TY_DOUBLE)
                                                : type_basic(TY_LDOUBLE);
        return e;
    case AST_EXPR_STRING: {
        /* A string literal is an ARRAY of char, and an lvalue — which is
         * why `sizeof "abc"` is 4 rather than a pointer's width. */
        Type *arr = type_array(s->arena, type_basic(TY_CHAR));

        arr->has_size = true;
        arr->size = (u64)e->tok->str.nbytes + 1;
        e->sem_type = arr;
        e->is_lvalue = true;
        return e;
    }
    case AST_EXPR_IDENT:
        return expr_ident(s, e);
    case AST_EXPR_PAREN:
        e->lhs = expr(s, e->lhs);
        e->sem_type = e->lhs->sem_type;
        e->is_lvalue = e->lhs->is_lvalue;
        e->poisoned = e->poisoned || e->lhs->poisoned;
        return e;
    case AST_EXPR_UNARY:
        if (e->is_postfix) {
            AstNode *op = expr(s, e->lhs);

            e->lhs = op;
            if (quiet(op, NULL))
                return poison(s, e);
            if (!op->is_lvalue) {
                err(s, e->span, "'%s' requires a modifiable lvalue",
                    ast_punct_name(e->op));
                return poison(s, e);
            }
            if (op->sem_type->quals & CGF_QUAL_CONST) {
                err(s, e->span,
                    "cannot modify a const-qualified value of type '%s'",
                    type_to_str(s->arena, op->sem_type));
                return poison(s, e);
            }
            e->sem_type = conv_strip_quals(s, op->sem_type);
            e->is_lvalue = false;
            return e;
        }
        return expr_unary(s, e);
    case AST_EXPR_BINARY:
        return expr_binary(s, e);
    case AST_EXPR_COND:
        return expr_cond(s, e);
    case AST_EXPR_MEMBER:
        return expr_member(s, e);
    case AST_EXPR_INDEX:
        return expr_index(s, e);
    case AST_EXPR_CALL:
        return expr_call(s, e);
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ALIGNOF:
        return expr_sizeof(s, e);
    case AST_EXPR_CAST: {
        Type *to = sema_type_from_ast(s, e->type, e->span);
        AstNode *op;

        e->lhs = op = conv_decay(s, expr(s, e->lhs));
        e->sem_type = to;
        e->is_lvalue = false;
        if (quiet(op, NULL))
            return poison(s, e);
        if (to->kind != TY_VOID && !type_is_arithmetic(to) &&
            to->kind != TY_PTR) {
            err(s, e->span, "cannot cast to non-scalar type '%s'",
                type_to_str(s->arena, to));
            return poison(s, e);
        }
        return e;
    }
    case AST_EXPR_GENERIC:
        return expr_generic(s, e);
    case AST_EXPR_COMPOUND_LIT: {
        Type *t = sema_type_from_ast(s, e->type, e->span);

        /* A compound literal IS an lvalue — `(int[]){1,2}[0]` and
         * `&(struct S){0}` both depend on that. Its storage duration was
         * decided by the parser from the scope it appeared in (Sprint 10);
         * Sprint 19 lowers it. */
        e->sem_type = t;
        e->is_lvalue = true;
        return e;
    }
    case AST_ERROR:
        return poison(s, e);
    default:
        e->sem_type = type_basic(TY_ERROR);
        return e;
    }
}

AstNode *sema_expr(Sema *s, AstNode *e)
{
    return expr(s, e);
}
