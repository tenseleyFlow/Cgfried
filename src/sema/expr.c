#include <string.h>

#include "sema/sema.h"
#include "util/dlev.h"
#include "warn/format.h"
#include "warn/warn.h"

/* Expression typing. Each node gets a sem_type and an is_lvalue bit, and
 * every operand is rewritten with the conversions its operator demands.
 *
 * Values are NOT folded here — Sprint 15 owns that — so this pass answers
 * "what type" and never "what value". The one exception is the null
 * pointer constant, where the syntactic forms `0` and `(void *)0` are
 * recognized because assignment compatibility cannot be decided without
 * them; anything else defers rather than guessing. */

static AstNode *expr(Sema *s, AstNode *e);

static void expr_init_list(Sema *s, AstNode *list)
{
    u32 i;

    if (!list || list->kind != AST_INIT_LIST)
        return;
    for (i = 0; i < list->nitems; i++) {
        AstNode *item = list->items[i];

        if (!item)
            continue;
        if (item->kind == AST_INIT_LIST)
            expr_init_list(s, item);
        else
            list->items[i] = expr(s, item);
    }
}

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
    sema_warn_deprecated(s, sym->name, sym->gnu.deprecated,
                         sym->gnu.deprecated_msg, e->span);
    if (sym->kind == SYM_VAR ||
        (sym->kind == SYM_FUNC && sym->name != s->cur_fname))
        sym->reads++;
    /* 6.7.4p3: an inline definition may not reference an identifier with
     * internal linkage. gcc warns; matched by observation. */
    if (s->cur_inline_candidate && sym->linkage == LINK_INTERNAL)
        warn_at(s->lang->warnings, WARN_STATIC_IN_INLINE, e->span,
                "'%s' is static but used in inline function '%s' "
                "which is not static",
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

static Type *literal_element_type(Sema *s, EncPrefix enc)
{
    switch (enc) {
    case ENC_NONE:
    case ENC_U8:
        return type_basic(TY_CHAR);
    case ENC_WIDE:
        /* __WCHAR_TYPE__ is target data.  arm64-linux is the sole current
         * unsigned-wchar target; the other four use int. */
        return type_basic(s->target.kind == CGF_TARGET_ARM64_LINUX ? TY_UINT
                                                                   : TY_INT);
    case ENC_U16:
        return type_basic(TY_USHORT);
    case ENC_U32:
        return type_basic(TY_UINT);
    }
    return type_basic(TY_CHAR);
}

/* --- operators ----------------------------------------------------------- */

static bool is_ptr(const Type *t)
{
    return t && t->kind == TY_PTR;
}

/* Every supported va_list expression decays to a pointer to the canonical
 * synthesized record.  Accepting arbitrary pointers (or integers) lets
 * malformed source reach lowering as verifier-invalid memory operations. */
/* What the va_* builtins accept as a cursor.
 *
 * On targets whose va_list is a one-element ARRAY, the argument arrives
 * already decayed to a pointer at the record, so the test is "pointer to the
 * element type". Apple's va_list is a plain `char *` OBJECT, so the argument
 * arrives as that pointer itself and the test is "compatible with va_list".
 * Lowering makes the two uniform by taking the ADDRESS in the second case --
 * decay does it for free in the first. */
static bool is_va_list_cursor(Sema *s, const Type *t)
{
    Type *va = sema_va_list_type(s);

    if (!va)
        return false;
    if (va->kind != TY_ARRAY)
        return type_compatible(conv_strip_quals(s, (Type *)t),
                               conv_strip_quals(s, va));
    return is_ptr(t) && va->base &&
           type_compatible(conv_strip_quals(s, t->base),
                           conv_strip_quals(s, va->base));
}

AstNode *sema_lvalue_root_ident(AstNode *e)
{
    AstNode *root;

    while (e && e->kind == AST_EXPR_PAREN)
        e = e->lhs;
    if (!e)
        return NULL;
    if (e->kind == AST_EXPR_IDENT)
        return e;
    if (e->kind == AST_EXPR_MEMBER)
        return e->is_arrow ? NULL : sema_lvalue_root_ident(e->lhs);
    if (e->kind != AST_EXPR_INDEX)
        return NULL;

    /* A subscript writes the declared array object only when its pointer
     * operand came from array-to-pointer decay.  `p[i]` reads `p` and writes
     * the pointed-to object, never the pointer variable itself. */
    root = e->lhs;
    while (root && root->kind == AST_EXPR_PAREN)
        root = root->lhs;
    if (root && root->kind == AST_EXPR_CAST && root->implicit && root->lhs &&
        root->lhs->sem_type && root->lhs->sem_type->kind == TY_ARRAY)
        return sema_lvalue_root_ident(root->lhs);
    root = e->rhs;
    while (root && root->kind == AST_EXPR_PAREN)
        root = root->lhs;
    if (root && root->kind == AST_EXPR_CAST && root->implicit && root->lhs &&
        root->lhs->sem_type && root->lhs->sem_type->kind == TY_ARRAY)
        return sema_lvalue_root_ident(root->lhs);
    return NULL;
}

static void mark_lvalue_write(AstNode *e, bool plain_assignment)
{
    AstNode *ident = sema_lvalue_root_ident(e);

    if (!ident || !ident->sym || ident->sym->kind != SYM_VAR)
        return;
    ident->sym->writes++;
    if (plain_assignment && ident->sym->reads)
        ident->sym->reads--;
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
        if (!sema_require_scalar(s, e->lhs))
            return poison(s, e);
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
        mark_lvalue_write(op, false);
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

    /* Typing the lvalue resolves its root declaration and records a read.
     * A plain assignment only writes that object; compound assignment also
     * reads its old value.  Member/array lvalues keep the same root object,
     * while `p[i]` correctly leaves the pointer variable as a read. */
    mark_lvalue_write(lhs, e->op == PUNCT_ASSIGN);

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
         * reject invalid operand pairs here so lowering may remain a pure
         * translation of a valid, typed tree. */
        Type *lt = lhs->sem_type;
        Type *rt;
        bool valid;

        e->rhs = conv_decay(s, e->rhs);
        rt = e->rhs->sem_type;
        switch (e->op) {
        case PUNCT_PLUS_ASSIGN:
        case PUNCT_MINUS_ASSIGN:
            valid = is_ptr(lt)
                        ? type_is_integer(rt)
                        : type_is_arithmetic(lt) && type_is_arithmetic(rt);
            break;
        case PUNCT_STAR_ASSIGN:
        case PUNCT_SLASH_ASSIGN:
            valid = type_is_arithmetic(lt) && type_is_arithmetic(rt);
            break;
        case PUNCT_PERCENT_ASSIGN:
        case PUNCT_SHL_ASSIGN:
        case PUNCT_SHR_ASSIGN:
        case PUNCT_AMP_ASSIGN:
        case PUNCT_CARET_ASSIGN:
        case PUNCT_PIPE_ASSIGN:
            valid = type_is_integer(lt) && type_is_integer(rt);
            break;
        default:
            valid = false;
            break;
        }
        if (!valid) {
            err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
                ast_punct_name(e->op), type_to_str(s->arena, lt),
                type_to_str(s->arena, rt));
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
        bool lhs_ok;
        bool rhs_ok;

        e->lhs = conv_decay(s, lhs);
        e->rhs = conv_decay(s, rhs);
        /* BOTH operands are checked and BOTH are reported: a second mistake
         * on the right must not be hidden by one on the left. Separate
         * calls rather than `&&`, which would short-circuit the second
         * check away. */
        lhs_ok = sema_require_scalar(s, e->lhs);
        rhs_ok = sema_require_scalar(s, e->rhs);
        if (!lhs_ok || !rhs_ok)
            return poison(s, e);
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
                    warn_at(s->lang->warnings,
                            WARN_COMPARE_DISTINCT_POINTER_TYPES, e->span,
                            "comparison of distinct pointer types ('%s' and "
                            "'%s')",
                            type_to_str(s->arena, lt),
                            type_to_str(s->arena, rt));
            } else {
                AstNode *other = is_ptr(lt) ? rhs : lhs;

                if (!type_is_integer(other->sem_type)) {
                    err(s, e->span, "invalid operands to '%s' ('%s' and '%s')",
                        ast_punct_name(e->op), type_to_str(s->arena, lt),
                        type_to_str(s->arena, rt));
                    return poison(s, e);
                }
                if (!conv_is_npc(s, other))
                    warn_at(s->lang->warnings, WARN_INT_CONVERSION, e->span,
                            "comparison between pointer and integer ('%s' "
                            "and '%s')",
                            type_to_str(s->arena, lt),
                            type_to_str(s->arena, rt));
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
/* C11 requires SCALAR type -- arithmetic or pointer, 6.2.5p21 -- for the
 * operand of `!` (6.5.3.3p1), both operands of `&&` and `||` (6.5.13p2,
 * 6.5.14p2), the first operand of `?:` (6.5.15p2), and every controlling
 * expression (6.8.4.1p1 for `if`, 6.8.5p2 for the loops).
 *
 * None of it was checked, and the two ways it went wrong were both SILENT.
 * A void operand became `icmp ne i32 undef, 0`, so the branch was taken on an
 * undef and an optimizer could resolve it either way. An aggregate operand
 * became `icmp ne ptr @s, 0` -- the address of the object, never null -- so
 * `if (s)` compiled to `if (1)`. Neither produced a diagnostic.
 *
 * Call it AFTER conv_decay: an array or function operand is a pointer by
 * then, and both are legal conditions.
 *
 * Returns true when the operand is acceptable. An already-poisoned or
 * untyped operand returns true so the original error is the only one. */
bool sema_require_scalar(Sema *s, const AstNode *e)
{
    const Type *t = e ? e->sem_type : NULL;

    if (!t || e->poisoned)
        return true;
    if (type_is_arithmetic(t) || t->kind == TY_PTR)
        return true;
    /* An ARRAY or FUNCTION operand converts to a pointer (6.3.2.1p3/p4) and
     * is therefore a legal condition. The expression paths below decay before
     * asking, but a STATEMENT condition is typed without decaying, so `if
     * (arr)` arrives here still an array -- and rejecting it would refuse
     * correct C. Lowering is already right about these: it compares the
     * object's ADDRESS against null, which for an array or a function is
     * never null, so the condition is always true, exactly as C requires. */
    if (t->kind == TY_ARRAY || t->kind == TY_FUNC)
        return true;
    if (t->kind == TY_VOID)
        /* gcc's wording, which is the one people recognize. */
        err(s, e->span, "void value not ignored as it ought to be");
    else
        err(s, e->span, "used value of type '%s' where scalar is required",
            type_to_str(s->arena, t));
    return false;
}

/* 6.8.4.2p1: a `switch` controlling expression shall have INTEGER type --
 * stricter than the scalar rule above, since a pointer and a float are both
 * scalars and neither may be switched on. gcc's wording. */
bool sema_require_switch_integer(Sema *s, const AstNode *e)
{
    const Type *t = e ? e->sem_type : NULL;

    if (!t || e->poisoned || type_is_integer(t))
        return true;
    err(s, e->span, "switch quantity not an integer");
    return false;
}

static AstNode *expr_cond(Sema *s, AstNode *e)
{
    AstNode *c = expr(s, e->lhs);
    AstNode *a;
    AstNode *b;
    AstNode **midp;
    Type *at;
    Type *bt;

    e->lhs = conv_decay(s, c);
    if (!sema_require_scalar(s, e->lhs))
        return poison(s, e);
    /* GNU `a ?: b`: the condition IS the middle operand, so every place
     * below that would convert the middle converts the CONDITION instead.
     * One pointer rather than a branch per site -- there are three, and a
     * fourth would be added without noticing.
     *
     * Converting the condition is safe because the result type is a
     * conversion of both arms: it can only widen `a`, and widening maps
     * zero to zero and nonzero to nonzero, so testing the converted value
     * asks the same question the source did. */
    midp = e->cond_omits_mid ? &e->lhs : &e->mid;
    if (!e->cond_omits_mid)
        *midp = expr(s, e->mid);
    b = expr(s, e->rhs);
    *midp = a = conv_decay(s, *midp);
    e->rhs = b = conv_decay(s, b);
    if (quiet(a, b) || quiet(c, NULL))
        return poison(s, e);

    at = a->sem_type;
    bt = b->sem_type;
    e->is_lvalue = false;

    if (type_is_arithmetic(at) && type_is_arithmetic(bt)) {
        e->sem_type = conv_uac(s, midp, &e->rhs);
        return e;
    }
    /* 6.5.15p3 allows a void conditional only when BOTH arms are void, but
     * gcc and clang both accept ONE void arm and give the whole expression
     * void type -- `g ? a : b()` compiles, and using its value is what earns
     * "void value not ignored as it ought to be". Matching that is the
     * deliverable: erroring here rejects code that builds everywhere else.
     *
     * Handling only the both-void case left one-sided void falling through to
     * the pointer/integer arm below, which treated `void` as the integer side
     * and gave the CONDITIONAL a pointer type while one arm produced no value
     * at all. Lowering then asked for the IR type of void: "lower_irtype on
     * non-scalar type kind 0". Frontend fuzzer, seed 76632. The arithmetic
     * case had the milder version of the same bug -- a hard error where gcc
     * is silent. */
    if (at->kind == TY_VOID || bt->kind == TY_VOID) {
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
        warn_at(s->lang->warnings, WARN_INCOMPATIBLE_POINTER_TYPES, e->span,
                "pointer type mismatch in conditional expression ('%s' and "
                "'%s')",
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
                *midp = conv_cast(s, *midp, ptype);
            e->sem_type = ptype;
            return e;
        }
        warn_at(s->lang->warnings, WARN_INT_CONVERSION, e->span,
                "pointer/integer type mismatch in conditional expression ('%s' "
                "and '%s')",
                type_to_str(s->arena, at), type_to_str(s->arena, bt));
        /* Keep gcc's warning-only recovery, but materialize the recovery
         * conversion just like the NPC case above.  Otherwise the two CFG
         * edges reach lowering with different IR types even though the
         * conditional expression has already been assigned ptype. */
        if (is_ptr(at))
            e->rhs = conv_cast(s, e->rhs, ptype);
        else
            *midp = conv_cast(s, *midp, ptype);
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
static Member *find_member(const Type *t, const char *name,
                           bool inherited_may_alias, bool *through_may_alias)
{
    Member *m;
    bool here_may_alias;

    if (!t || !t->tag)
        return NULL;
    here_may_alias = inherited_may_alias || t->may_alias;
    for (m = t->tag->members; m; m = m->next) {
        if (m->name == name) {
            if (through_may_alias)
                *through_may_alias = here_may_alias;
            return m;
        }
        if (!m->name && m->type &&
            (m->type->kind == TY_STRUCT || m->type->kind == TY_UNION)) {
            Member *inner =
                find_member(m->type, name, here_may_alias, through_may_alias);

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
    Type *member_type;
    Member *m;
    bool through_may_alias = false;

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
    m = find_member(ot, e->name, false, &through_may_alias);
    if (!m) {
        err(s, e->span, "'%s' has no member named '%s'",
            type_to_str(s->arena, ot), e->name ? e->name : "?");
        return poison(s, e);
    }
    sema_warn_deprecated(s, m->name, m->deprecated, m->deprecated_msg, e->span);
    /* The member inherits the OBJECT's qualifiers: a member of a const
     * struct is const, which is what stops `cs.m = 1`. */
    member_type =
        through_may_alias ? type_may_alias(s->arena, m->type) : m->type;
    e->sem_type = ot->quals ? type_qualify(s->arena, member_type, ot->quals)
                            : member_type;
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

/* GNU's forwarding pack is an inliner operand, not a va_list and not an
 * expression value. It is meaningful only while typing the body of a
 * prototyped variadic inline definition; lowering later specializes that
 * body at each direct call and supplies the caller's anonymous arguments. */
static bool require_va_arg_pack_wrapper(Sema *s, AstNode *e,
                                        const char *builtin)
{
    Symbol *fn = s->cur_func;
    Type *ft = fn ? fn->type : NULL;

    if (!fn || fn->kind != SYM_FUNC || !ft || ft->kind != TY_FUNC) {
        err(s, e->span, "'%s' may only appear in a function definition",
            builtin);
        return false;
    }
    if (!ft->has_proto || !ft->variadic) {
        err(s, e->span,
            "'%s' may only appear in a prototyped variadic function", builtin);
        return false;
    }
    if ((fn->func_specs & AST_FS_INLINE) == 0) {
        err(s, e->span, "'%s' requires an inline function definition", builtin);
        return false;
    }
    fn->uses_va_arg_pack = true;
    return true;
}

static AstNode *expr_call(Sema *s, AstNode *e)
{
    AstNode *callee;
    Type *ft;
    u32 i;
    u32 explicit_nargs = e->nargs;
    bool has_va_pack = false;
    const char *callee_name = NULL;

    /* C89 implicit-function recovery: introduce one file-scope `int f()`
     * symbol before ordinary identifier typing. Later calls find it, so
     * the diagnostic is first-use-only and a later declaration follows the
     * normal redeclaration path. */
    if (e->lhs && e->lhs->kind == AST_EXPR_IDENT && e->lhs->name &&
        strncmp(e->lhs->name, "__builtin_", 10) != 0 &&
        !scope_lookup(s->scope, e->lhs->name, NS_ORDINARY)) {
        Type *fn = type_func(s->arena, type_basic(TY_INT));
        Symbol *implicit =
            sym_new(s, e->lhs->name, SYM_FUNC, NS_ORDINARY, fn, e->lhs->span);

        fn->has_proto = false;
        implicit->linkage = LINK_EXTERNAL;
        implicit->next = s->file_scope->ordinary;
        s->file_scope->ordinary = implicit;
        if (std_is_c99_or_later(s->lang->std))
            warn_pedwarn_at(s->lang->warnings,
                            WARN_IMPLICIT_FUNCTION_DECLARATION, e->lhs->span,
                            "implicit declaration of function '%s'",
                            e->lhs->name);
        else if (warn_explicitly_enabled(s->lang->warnings,
                                         WARN_IMPLICIT_FUNCTION_DECLARATION,
                                         e->lhs->span))
            warn_at(s->lang->warnings, WARN_IMPLICIT_FUNCTION_DECLARATION,
                    e->lhs->span, "implicit declaration of function '%s'",
                    e->lhs->name);
    }

    /* The va_* builtins are not declared functions — recognize the names
     * BEFORE ordinary resolution would call them undeclared. Arguments
     * are typed and decayed (a va_list is an ARRAY and decays to the
     * record pointer, which is exactly what lowering wants); the marker
     * on `op` is what lowering dispatches on. */
    if (e->lhs && e->lhs->kind == AST_EXPR_IDENT && e->lhs->name &&
        strncmp(e->lhs->name, "__builtin_", 10) == 0) {
        int want = 0, kind = 0;
        u16 b = sema_builtin_lookup(e->lhs->name + 10, &want, &kind);

        if (b) {
            if (want >= 0 && (int)e->nargs != want) {
                err(s, e->span, "'%s' takes exactly %d argument%s",
                    e->lhs->name, want, want == 1 ? "" : "s");
                return poison(s, e);
            }
            for (i = 0; i < e->nargs; i++)
                e->args[i] = conv_decay(s, expr(s, e->args[i]));
            if (b == SEMA_BUILTIN_VA_START || b == SEMA_BUILTIN_VA_END ||
                b == SEMA_BUILTIN_VA_COPY) {
                u32 cursors = b == SEMA_BUILTIN_VA_COPY ? 2u : 1u;

                for (i = 0; i < cursors; i++) {
                    if (quiet(e->args[i], NULL))
                        return poison(s, e);
                    if (!is_va_list_cursor(s, e->args[i]->sem_type)) {
                        err(s, e->args[i]->span,
                            "argument %u to '%s' is not a va_list",
                            (unsigned)i + 1, e->lhs->name);
                        return poison(s, e);
                    }
                }
            }
            /* The mem/str builtins take the LIBC signatures: sizes are
             * size_t, so promote the counted argument rather than
             * passing whatever width the user wrote (v0.1.0 lowers
             * these to real libc calls — inlining is Phase 7/11). */
            {
                AssignCtx bctx;
                int size_arg = -1;

                memset(&bctx, 0, sizeof(bctx));
                bctx.kind = ACTX_ARG;
                bctx.callee = e->lhs->name;
                switch (b) {
                case SEMA_BUILTIN_MEMCPY:
                case SEMA_BUILTIN_MEMMOVE:
                case SEMA_BUILTIN_MEMSET:
                case SEMA_BUILTIN_MEMCMP:
                    size_arg = 2;
                    break;
                case SEMA_BUILTIN_ALLOCA:
                    size_arg = 0;
                    break;
                }
                if (size_arg >= 0 && (int)e->nargs > size_arg) {
                    bctx.arg_index = (u32)size_arg + 1;
                    conv_assignable(s, type_basic(TY_ULONG), &e->args[size_arg],
                                    bctx);
                }
                /* A BK_U* builtin has a real prototype, so its argument
                 * converts as if by assignment -- and that is OBSERVABLE:
                 * __builtin_bswap16(0x11223344) truncates to 0x3344 and
                 * swaps THAT, with gcc's -Woverflow on the way. Promoting
                 * it instead would swap the wrong bytes. */
                {
                    Type *ut = sema_builtin_uint_type(s, kind);

                    if (ut && e->nargs > 0) {
                        bctx.arg_index = 1;
                        conv_assignable(s, ut, &e->args[0], bctx);
                    }
                }
            }
            e->op = b;
            e->is_lvalue = false;
            switch ((BuiltinKind)kind) {
            case BK_VOID:
                e->sem_type = type_basic(TY_VOID);
                break;
            case BK_INT:
                e->sem_type = type_basic(TY_INT);
                break;
            case BK_LONG:
                e->sem_type = type_basic(TY_LONG);
                break;
            case BK_SIZE:
                e->sem_type = type_basic(TY_ULONG);
                break;
            case BK_VOIDP:
                e->sem_type = type_ptr(s->arena, type_basic(TY_VOID));
                break;
            case BK_DOUBLE:
                e->sem_type = type_basic(TY_DOUBLE);
                break;
            case BK_FLOAT:
                e->sem_type = type_basic(TY_FLOAT);
                break;
            case BK_ARG0:
                /* __builtin_expect(e, c): the result IS the first
                 * argument, promoted (gcc types it long). */
                e->sem_type =
                    e->nargs ? e->args[0]->sem_type : type_basic(TY_LONG);
                break;
            case BK_U16:
            case BK_U32:
            case BK_U64:
                e->sem_type = sema_builtin_uint_type(s, kind);
                break;
            case BK_SPECIAL:
                e->sem_type = type_basic(TY_INT);
                break;
            }
            return e;
        }
        /* A __builtin_ name with no table row: the parser already
         * deferred it loudly, so reaching here means a new row was
         * added without teaching sema. */
    }

    callee = conv_decay(s, expr(s, e->lhs));

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
        AstNode *arg = e->args[i];

        if (!arg || arg->kind != AST_EXPR_VA_ARG_PACK)
            continue;
        if (has_va_pack || i + 1 != e->nargs) {
            err(s, arg->span,
                "'__builtin_va_arg_pack()' must be the final argument of "
                "a call");
        }
        has_va_pack = true;
        explicit_nargs--;
        arg->sem_type = type_basic(TY_VOID);
        arg->is_lvalue = false;
        (void)require_va_arg_pack_wrapper(s, arg, "__builtin_va_arg_pack");
    }
    if (has_va_pack) {
        if (!ft->has_proto || !ft->variadic)
            err(s, e->span,
                "'__builtin_va_arg_pack()' may only forward to a "
                "prototyped variadic function");
        else if (explicit_nargs < ft->nparams)
            err(s, e->span,
                "'__builtin_va_arg_pack()' cannot supply named parameters: "
                "expected at least %u explicit argument%s, have %u",
                (unsigned)ft->nparams, ft->nparams == 1 ? "" : "s",
                (unsigned)explicit_nargs);
    }

    for (i = 0; i < e->nargs; i++) {
        AstNode *arg;

        if (e->args[i] && e->args[i]->kind == AST_EXPR_VA_ARG_PACK)
            continue;
        arg = conv_decay(s, expr(s, e->args[i]));

        e->args[i] = arg;
        /* An argument is a VALUE, and void has none. The prototyped path
         * below catches this as an assignment mismatch, but an unprototyped
         * or variadic call has nothing to assign to -- `r(f())` where f
         * returns void reached lowering and ICEd on "lower_irtype on
         * non-scalar type kind 0". Found by the frontend fuzzer, seed 27852.
         * gcc's wording. */
        if (!quiet(arg, NULL) && arg->sem_type &&
            arg->sem_type->kind == TY_VOID) {
            err(s, arg->span, "invalid use of void expression");
            e->args[i] = poison(s, arg);
            continue;
        }
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
        if (explicit_nargs < ft->nparams)
            err(s, e->span,
                "too few arguments to function%s%s%s: expected "
                "%u, have %u",
                callee_name ? " '" : "", callee_name ? callee_name : "",
                callee_name ? "'" : "", (unsigned)ft->nparams,
                (unsigned)explicit_nargs);
        else if (explicit_nargs > ft->nparams && !ft->variadic)
            err(s, e->span,
                "too many arguments to function%s%s%s: expected "
                "%u, have %u",
                callee_name ? " '" : "", callee_name ? callee_name : "",
                callee_name ? "'" : "", (unsigned)ft->nparams,
                (unsigned)explicit_nargs);
    }
    if (!has_va_pack)
        warn_format_check_call(s->lang->warnings, s, e);
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
        if (e->kind == AST_EXPR_SIZEOF && operand &&
            (operand->kind == TY_VOID || operand->kind == TY_FUNC)) {
            warn_pedwarn_at(s->lang->warnings, WARN_POINTER_ARITH, e->span,
                            "invalid application of 'sizeof' to type '%s'",
                            type_to_str(s->arena, operand));
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
        /* Ordinary character constants have type int.  Prefixed constants
         * instead have wchar_t/char16_t/char32_t (6.4.4.4p10); format %lc
         * depends on retaining that target-aware distinction. */
        e->sem_type = e->tok->enc == ENC_NONE
                          ? type_basic(TY_INT)
                          : literal_element_type(s, (EncPrefix)e->tok->enc);
        return e;
    case AST_EXPR_FLOAT:
        switch ((FloatConstType)e->tok->float_type) {
        case FTY_FLOAT:
            e->sem_type = type_basic(TY_FLOAT);
            break;
        case FTY_DOUBLE:
            e->sem_type = type_basic(TY_DOUBLE);
            break;
        case FTY_LDOUBLE:
            e->sem_type = type_basic(TY_LDOUBLE);
            break;
        case FTY_FLOAT32:
            e->sem_type = type_basic(TY_FLOAT32);
            break;
        case FTY_FLOAT64:
            e->sem_type = type_basic(TY_FLOAT64);
            break;
        case FTY_FLOAT32X:
            e->sem_type = type_basic(TY_FLOAT32X);
            break;
        case FTY_FLOAT64X:
            e->sem_type = type_basic(TY_FLOAT64X);
            break;
        case FTY_FLOAT128:
            e->sem_type = type_basic(TY_FLOAT128);
            break;
        }
        return e;
    case AST_EXPR_STRING: {
        Type *arr = type_array(
            s->arena, literal_element_type(s, (EncPrefix)e->tok->str.enc));

        arr->has_size = true;
        arr->size = (u64)e->tok->str.nelems + 1;
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
            mark_lvalue_write(op, false);
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
        /* 6.5.4p2 constrains BOTH sides: the OPERAND must be scalar too.
         * Only the target was checked, so `(int)r()` on a struct-returning
         * `r` typed cleanly and then ICEd in lowering on a non-scalar IR
         * type — found by the frontend fuzzer, seed 9241. A cast to void is
         * exempt: it discards the value whatever its type, and `(void)s` on
         * a struct is ordinary C. */
        if (to->kind != TY_VOID && op->sem_type &&
            !type_is_arithmetic(op->sem_type) && op->sem_type->kind != TY_PTR) {
            err(s, e->span, "cannot cast from non-scalar type '%s'",
                type_to_str(s->arena, op->sem_type));
            return poison(s, e);
        }
        return e;
    }
    case AST_EXPR_GENERIC:
        return expr_generic(s, e);
    case AST_EXPR_TYPES_COMPATIBLE: {
        /* An int 0/1 constant. TWO measured rules decide it:
         *
         *  - TOP-LEVEL QUALIFIERS ARE IGNORED. `(const int, int)` is 1.
         *  - ARRAYS DO NOT DECAY. `(char *, char[3])` is 0, unlike every
         *    other context where an array in a value position becomes a
         *    pointer -- this builtin asks about the TYPES as written.
         *
         * Both from gcc directly. It folds here rather than in constexpr
         * because the answer is a property of two types and nothing later
         * needs the operands. */
        Type *a = sema_type_from_ast(s, e->type, e->span);
        Type *b = sema_type_from_ast(s, e->type2, e->span);
        /* conv_strip_quals, not type_qualify(...,0): the latter RETURNS THE
         * TYPE UNCHANGED when asked for zero qualifiers, so `const int` and
         * `int` stayed distinct and the answer was 0 where gcc says 1. */
        Type *ua = a ? conv_strip_quals(s, a) : NULL;
        Type *ub = b ? conv_strip_quals(s, b) : NULL;

        e->sem_type = type_basic(TY_INT);
        e->is_lvalue = false;
        e->types_compatible = ua && ub && type_compatible(ua, ub);
        return e;
    }
    case AST_EXPR_CHOOSE_EXPR: {
        /* BOTH ARMS ARE TYPED; only the selected one is evaluated.
         *
         * The sprint file says the unselected arm is "untype-checked beyond
         * parse" and that is WRONG -- gcc diagnoses a bad member access, an
         * undeclared identifier and a wrong-arity call there, all measured.
         * Typing both is therefore the parity-correct behaviour AND the
         * safer one: a macro whose dead arm is nonsense is a bug the author
         * wants told about.
         *
         * The RESULT is the selected arm entirely -- its type, its lvalue-
         * ness and its value. */
        i64 cond = 0;

        e->lhs = expr(s, e->lhs);
        e->mid = expr(s, e->mid);
        e->rhs = expr(s, e->rhs);
        if (!sema_require_ice(s, e->lhs, &cond,
                              "the first argument to "
                              "'__builtin_choose_expr'")) {
            e->sem_type = type_basic(TY_ERROR);
            return e;
        }
        e->choose_taken = cond != 0;
        {
            AstNode *sel = e->choose_taken ? e->mid : e->rhs;

            e->sem_type =
                sel && sel->sem_type ? sel->sem_type : type_basic(TY_ERROR);
            e->is_lvalue = sel ? sel->is_lvalue : false;
        }
        return e;
    }
    case AST_EXPR_VA_ARG_PACK:
        /* The call path consumes this placeholder without treating it as a
         * value. Reaching ordinary expression typing means it was written in
         * every other (invalid) position. */
        (void)require_va_arg_pack_wrapper(s, e, "__builtin_va_arg_pack");
        err(s, e->span,
            "'__builtin_va_arg_pack()' must be the final argument of a "
            "call");
        return poison(s, e);
    case AST_EXPR_VA_ARG_PACK_LEN:
        (void)require_va_arg_pack_wrapper(s, e, "__builtin_va_arg_pack_len");
        e->sem_type = type_basic(TY_INT);
        e->is_lvalue = false;
        return e;
    case AST_EXPR_VA_ARG: {
        /* __builtin_va_arg(ap, T): the value is a T rvalue; ap types and
         * decays (array va_list -> record pointer). Checking that ap
         * really is a va_list is deliberately loose — glibc's macros
         * pass both the array and its decayed pointer, and the SysV
         * record address is the same either way. */
        e->lhs = conv_decay(s, expr(s, e->lhs));
        e->sem_type = sema_type_from_ast(s, e->type, e->span);
        e->is_lvalue = false;
        if (quiet(e->lhs, NULL))
            return poison(s, e);
        if (!is_va_list_cursor(s, e->lhs->sem_type)) {
            err(s, e->lhs->span,
                "first argument to '__builtin_va_arg' is not a va_list");
            return poison(s, e);
        }
        return e;
    }
    case AST_EXPR_OFFSETOF: {
        /* __builtin_offsetof(T, designator): the designator chain is
         * typed against a synthetic lvalue of T, so every ordinary
         * member/index rule (anonymous members, array bounds, the
         * not-a-member diagnostic) applies unchanged. The result is a
         * size_t ICE that Sprint 15's engine folds. */
        Type *rec = sema_type_from_ast(s, e->type, e->span);
        AstNode *base = e->lhs;

        while (base && base->kind != AST_EXPR_OFFSETOF_BASE)
            base = base->lhs;
        if (base) {
            base->sem_type = rec;
            base->is_lvalue = true;
        }
        if (rec->kind != TY_STRUCT && rec->kind != TY_UNION) {
            err(s, e->span,
                "'__builtin_offsetof' requires a struct or union type, "
                "not '%s'",
                type_to_str(s->arena, rec));
            return poison(s, e);
        }
        e->lhs = expr(s, e->lhs);
        e->sem_type = type_basic(TY_ULONG);
        e->is_lvalue = false;
        if (quiet(e->lhs, NULL))
            return poison(s, e);
        /* Fold HERE, not at lowering: offsetof is an ICE by definition,
         * so a bad designator must be a sema error at its own location
         * rather than an ICE from a fold that "cannot fail". */
        {
            ConstValue cv = constexpr_eval(s, e, CE_ICE);

            if (cv.kind != CV_INT)
                return poison(s, e);
        }
        return e;
    }
    case AST_EXPR_OFFSETOF_BASE:
        /* Already typed by the OFFSETOF case above; never evaluated. */
        return e;
    case AST_EXPR_COMPOUND_LIT: {
        Type *t = sema_type_from_ast(s, e->type, e->span);

        /* Initializer expressions are part of the compound literal's
         * evaluation.  Type them before leaving the enclosing scope so
         * identifier reads count for the unused-* family. */
        if (e->init && e->init->kind == AST_INIT_LIST)
            expr_init_list(s, e->init);
        else if (e->init)
            e->init = expr(s, e->init);
        /* AN UNSIZED ARRAY LITERAL TAKES ITS BOUND FROM THE INITIALIZER, the
         * same 6.7.9p22 completion a declaration gets -- 6.5.2.5p4 says the
         * literal's type is the type-name's, and for an array of unknown size
         * that is what the initializer list determines.
         *
         * Missing for years, because the two shapes fail differently and
         * neither is loud: `sizeof((int[]){1,2})` reported "incomplete type",
         * while a NESTED literal lowered an alloca too small and stored only
         * its first element -- reading uninitialized stack for the rest, with
         * no diagnostic. musl's res_msend.c has exactly that nesting and only
         * began reaching lowering when extended asm made its includes parse.
         *
         * Done AFTER the initializer is typed, since counting a designated
         * item folds its index. */
        t = sema_array_complete_from_init(s, t, e->init);
        /* A compound literal IS an lvalue — `(int[]){1,2}[0]` and
         * `&(struct S){0}` both depend on that. Its storage duration was
         * decided by the parser from the scope it appeared in (Sprint 10);
         * Sprint 19 lowers it. */
        e->sem_type = t;
        e->is_lvalue = true;
        return e;
    }
    case AST_EXPR_STMT: {
        /* THE VALUE IS THE LAST ITEM, AND ONLY IF THAT ITEM IS AN EXPRESSION
         * STATEMENT. A trailing declaration or a trailing `if` both make the
         * whole thing `void` -- gcc reports either as "void value not
         * ignored as it ought to be", measured on all three shapes before
         * this was written. An empty `({ })` is legal and void.
         *
         * NOT an lvalue: gcc rejects `({ int a; a; }) = 1`, so the result is
         * a value even when the last expression was an lvalue. */
        AstNode *last = NULL;

        sema_stmt_in_expr(s, e->lhs);
        if (e->lhs && e->lhs->kind == AST_STMT_COMPOUND && e->lhs->nitems)
            last = e->lhs->items[e->lhs->nitems - 1];
        if (last && last->kind == AST_STMT_EXPR && last->lhs &&
            last->lhs->sem_type)
            e->sem_type = last->lhs->sem_type;
        else
            e->sem_type = type_basic(TY_VOID);
        e->is_lvalue = false;
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
