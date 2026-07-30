#include <string.h>

#include "parse/parse.h"

/* The C11 6.5 expression grammar: one function per precedence level, named
 * after its subclause. Nothing here type-checks or folds — `_Generic`
 * selection and lvalue rules are Sprints 12-13, constant evaluation is
 * Sprint 15. The parser's job is to get the BINDING right and to record
 * enough (unevaluated flags, compound-literal storage) that later passes
 * never have to re-derive syntax.
 *
 * On assignment: the grammar demands a unary-expression on the left, but
 * we parse a conditional-expression and check in sema — gcc's approach.
 * That makes `a ? b : c = d` parse as `(a?b:c) = d` and then fail the
 * lvalue check, which is the diagnostic users expect, rather than a bare
 * "syntax error" pointing at the '='. */

VEC_DECL(ExprVec, AstNode *);

static AstNode *parse_cast_expr(Parser *p);
static AstNode *parse_unary_expr(Parser *p);

static AstNode *expr_new(Parser *p, AstKind k, Span sp)
{
    AstNode *n = ast_new(p->arena, k, sp);

    n->unevaluated = p->unevaluated > 0;
    return n;
}

static AstNode *expr_error(Parser *p, const Token *t, const char *what)
{
    AstNode *n;

    parse_error(p, t, "expected %s but found '%s'", what,
                t->kind == TOK_EOF ? "end of file" : t->spelling);
    n = expr_new(p, AST_ERROR, t->span);
    n->poisoned = true;
    return n;
}

/* --- 6.5.1.1 _Generic ---------------------------------------------------- */

static AstNode *parse_generic(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *g;
    ExprVec assocs = {NULL, 0, 0};
    u32 ndefault = 0;

    p->pos++; /* _Generic */
    g = expr_new(p, AST_EXPR_GENERIC, kw->span);
    parse_expect_punct(p, PUNCT_LPAREN, "after '_Generic'");

    /* The controlling expression is NEVER evaluated (6.5.1.1p2) — only its
     * type after lvalue conversion matters. Flagging it here means sema and
     * lowering cannot accidentally emit its side effects. */
    p->unevaluated++;
    g->lhs = parse_assign_expr(p);
    p->unevaluated--;

    while (parse_eat_punct(p, PUNCT_COMMA)) {
        const Token *at = parse_peek(p);
        AstNode *a = expr_new(p, AST_GENERIC_ASSOC, at->span);

        if (parse_eat_kw(p, KW_DEFAULT)) {
            /* `default` may appear ANYWHERE in the list, not only last —
             * so this is a running count, checked after the loop. */
            ndefault++;
        } else {
            a->type = parse_type_name(p);
        }
        parse_expect_punct(p, PUNCT_COLON, "in a _Generic association");
        a->lhs = parse_assign_expr(p);
        ExprVec_push(&assocs, a);
    }
    parse_expect_punct(p, PUNCT_RPAREN, "after _Generic association list");

    if (ndefault > 1)
        parse_error(p, kw,
                    "_Generic has %u 'default' associations; at most one is "
                    "allowed",
                    (unsigned)ndefault);
    if (assocs.len == 0)
        parse_error(p, kw, "_Generic requires at least one association");

    g->nitems = (u32)assocs.len;
    if (assocs.len) {
        g->items = arena_alloc(p->arena, assocs.len * sizeof(AstNode *),
                               _Alignof(AstNode *));
        memcpy(g->items, assocs.data, assocs.len * sizeof(AstNode *));
    }
    ExprVec_free(&assocs);
    return g;
}

/* --- 6.5.1 primary ------------------------------------------------------- */

static AstNode *parse_primary_expr(Parser *p)
{
    const Token *t = parse_peek(p);
    AstNode *n;

    switch ((TokenKind)t->kind) {
    case TOK_INT_CONST:
    case TOK_FLOAT_CONST:
    case TOK_CHAR_CONST:
    case TOK_STRING:
        n = expr_new(p,
                     t->kind == TOK_INT_CONST     ? AST_EXPR_INT
                     : t->kind == TOK_FLOAT_CONST ? AST_EXPR_FLOAT
                     : t->kind == TOK_CHAR_CONST  ? AST_EXPR_CHAR
                                                  : AST_EXPR_STRING,
                     t->span);
        n->tok = t;
        p->pos++;
        return n;
    case TOK_IDENT:
        /* A `__builtin_*` name in expression position is compiler magic
         * we have not implemented; letting it become an ordinary
         * identifier would turn a missing feature into a link error much
         * later. Defer loudly, naming the sprint. */
        if (parse_is_builtin_name(t->spelling)) {
            parse_error(p, t,
                        "'%s' is not yet supported (the compiler builtins "
                        "land in Sprint 28)",
                        t->spelling);
            p->pos++;
            return expr_new(p, AST_ERROR, t->span);
        }
        n = expr_new(p, AST_EXPR_IDENT, t->span);
        n->tok = t;
        n->name = t->spelling;
        p->pos++;
        return n;
    default:
        break;
    }

    if (parse_at_kw(p, KW_GENERIC))
        return parse_generic(p);

    /* GNU address-of-label: `&&lab`. It reaches primary rather than the
     * unary path because `&&` lexes as one token, so without this it
     * would report a bare "expected an expression". */
    if (parse_at_punct(p, PUNCT_AMPAMP) &&
        parse_peek_n(p, 1)->kind == TOK_IDENT) {
        parse_error(p, t,
                    "the GNU address-of-label operator '&&' is not yet "
                    "supported (lands in Sprint 55)");
        p->pos += 2;
        return expr_new(p, AST_ERROR, t->span);
    }

    if (parse_at_punct(p, PUNCT_LPAREN)) {
        /* A GNU statement expression `({ ... })` is a paren immediately
         * followed by a brace. Diagnose it here rather than letting the
         * expression parser produce a confusing cascade. */
        if (parse_peek_n(p, 1)->kind == TOK_PUNCT &&
            parse_peek_n(p, 1)->punct == PUNCT_LBRACE) {
            parse_error(p, t,
                        "GNU statement expressions are not yet supported "
                        "(lands in Sprint 55)");
            p->pos++;
            return expr_new(p, AST_ERROR, t->span);
        }
        p->pos++;
        if (!parse_depth_enter(p, t))
            return parse_error_node(p, t->span);
        n = expr_new(p, AST_EXPR_PAREN, t->span);
        n->lhs = parse_expr(p);
        parse_depth_leave(p);
        parse_expect_close(p, PUNCT_RPAREN, t->span,
                           "after parenthesized expression");
        return parse_poison_from(n, n->lhs);
    }
    return expr_error(p, t, "an expression");
}

/* --- 6.5.2 postfix ------------------------------------------------------- */

/* `(type-name){ init }` — a compound literal, NOT a cast. The caller has
 * already consumed the parenthesized type-name and seen the '{'. */
static AstNode *finish_compound_literal(Parser *p, AstType *ty, Span sp)
{
    AstNode *n = expr_new(p, AST_EXPR_COMPOUND_LIT, sp);

    n->type = ty;
    /* Storage duration follows the SCOPE it was written in (6.5.2.5p5),
     * with no keyword to consult: static at file scope, automatic at block
     * scope. Only the parser still knows. */
    n->is_static_storage = p->scope_depth == 0;
    n->init = parse_braced_initializer(p);
    return n;
}

static AstNode *parse_postfix_ops(Parser *p, AstNode *base)
{
    for (;;) {
        const Token *t = parse_peek(p);

        if (parse_eat_punct(p, PUNCT_LBRACKET)) {
            AstNode *n = expr_new(p, AST_EXPR_INDEX, t->span);

            if (!parse_depth_enter(p, t))
                return parse_error_node(p, t->span);
            n->lhs = base;
            n->rhs = parse_expr(p);
            parse_depth_leave(p);
            parse_expect_close(p, PUNCT_RBRACKET, t->span,
                               "after array subscript");
            parse_poison_from(n, base);
            base = parse_poison_from(n, n->rhs);
        } else if (parse_eat_punct(p, PUNCT_LPAREN)) {
            AstNode *n = expr_new(p, AST_EXPR_CALL, t->span);
            ExprVec args = {NULL, 0, 0};

            n->lhs = base;
            if (!parse_at_punct(p, PUNCT_RPAREN)) {
                for (;;) {
                    /* parse_assign_expr, never parse_expr: the commas
                     * SEPARATING arguments are not comma operators, so
                     * `f(a, (b, c), d)` has three arguments. */
                    ExprVec_push(&args, parse_assign_expr(p));
                    if (!parse_eat_punct(p, PUNCT_COMMA))
                        break;
                }
            }
            parse_expect_close(p, PUNCT_RPAREN, t->span,
                               "after the argument list");
            n->nargs = (u32)args.len;
            if (args.len) {
                n->args = arena_alloc(p->arena, args.len * sizeof(AstNode *),
                                      _Alignof(AstNode *));
                memcpy(n->args, args.data, args.len * sizeof(AstNode *));
            }
            ExprVec_free(&args);
            base = n;
        } else if (parse_at_punct(p, PUNCT_DOT) ||
                   parse_at_punct(p, PUNCT_ARROW)) {
            AstNode *n = expr_new(p, AST_EXPR_MEMBER, t->span);
            const Token *id;

            n->is_arrow = t->punct == PUNCT_ARROW;
            n->lhs = base;
            p->pos++;
            id = parse_peek(p);
            /* A member name may be spelled with a KEYWORD in the member
             * namespace only if the program used one — reject, but name
             * the token so the message is useful. */
            if (id->kind != TOK_IDENT) {
                parse_error(p, id, "expected a member name after '%s'",
                            n->is_arrow ? "->" : ".");
            } else {
                n->name = id->spelling;
                p->pos++;
            }
            base = n;
        } else if (parse_at_punct(p, PUNCT_PLUSPLUS) ||
                   parse_at_punct(p, PUNCT_MINUSMINUS)) {
            AstNode *n = expr_new(p, AST_EXPR_UNARY, t->span);
            n->op = t->punct;
            n->is_postfix = true;
            n->lhs = base;
            p->pos++;
            base = n;
        } else {
            return base;
        }
    }
}

static AstNode *parse_postfix_expr(Parser *p)
{
    return parse_postfix_ops(p, parse_primary_expr(p));
}

/* --- 6.5.3 unary --------------------------------------------------------- */

static bool is_unary_op(const Token *t)
{
    if (t->kind != TOK_PUNCT)
        return false;
    switch (t->punct) {
    case PUNCT_AMP:
    case PUNCT_STAR:
    case PUNCT_PLUS:
    case PUNCT_MINUS:
    case PUNCT_TILDE:
    case PUNCT_BANG:
        return true;
    default:
        return false;
    }
}

static AstNode *parse_sizeof(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n;
    u32 save;

    p->pos++;
    n = expr_new(p, AST_EXPR_SIZEOF, kw->span);

    save = p->pos;
    if (parse_at_punct(p, PUNCT_LPAREN)) {
        p->pos++;
        if (parse_at_type_name(p)) {
            AstType *ty = parse_type_name(p);

            parse_expect_punct(p, PUNCT_RPAREN, "after the type name");
            /* `sizeof (int){1}` is sizeof applied to a COMPOUND LITERAL,
             * not to the type: the '{' says the parenthesized type-name
             * started a postfix expression. Without this check the
             * initializer would be stranded as a syntax error. */
            if (parse_at_punct(p, PUNCT_LBRACE)) {
                AstNode *lit = finish_compound_literal(p, ty, kw->span);
                p->unevaluated++;
                n->lhs = parse_postfix_ops(p, lit);
                p->unevaluated--;
                n->unevaluated = true;
                return n;
            }
            /* Otherwise sizeof STOPS here. `sizeof (T)(x)` is therefore
             * sizeof(T) followed by a stray `(x)`, a syntax error — which
             * is what gcc and clang report, because a cast-expression is
             * not a unary-expression and so cannot be sizeof's operand. */
            n->type = ty;
            n->unevaluated = true;
            return n;
        }
        p->pos = save;
    }

    /* Not a type-name: `sizeof x`, `sizeof *p`, `sizeof (expr)`. The
     * operand is NOT evaluated (6.5.3.4p2) except for a VLA, which sema
     * sorts out; flag it so nothing lowers its side effects by default. */
    p->unevaluated++;
    n->lhs = parse_unary_expr(p);
    p->unevaluated--;
    n->unevaluated = true;
    return n;
}

static AstNode *parse_alignof(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n;

    p->pos++;
    n = expr_new(p, AST_EXPR_ALIGNOF, kw->span);
    n->unevaluated = true;
    if (!parse_at_punct(p, PUNCT_LPAREN)) {
        /* `_Alignof expr` is a GNU extension; ISO takes only a type-name. */
        parse_error(p, parse_peek(p),
                    "'_Alignof' requires a parenthesized type name (the "
                    "expression form is a GNU extension, lands in Sprint 55)");
        return n;
    }
    p->pos++;
    if (!parse_at_type_name(p)) {
        parse_error(p, parse_peek(p),
                    "'_Alignof' requires a type name (the expression form is "
                    "a GNU extension, lands in Sprint 55)");
        return n;
    }
    n->type = parse_type_name(p);
    parse_expect_punct(p, PUNCT_RPAREN, "after the type name");
    return n;
}

static AstNode *parse_unary_expr(Parser *p)
{
    const Token *t = parse_peek(p);

    if (is_unary_op(t)) {
        AstNode *n = expr_new(p, AST_EXPR_UNARY, t->span);

        n->op = t->punct;
        p->pos++;
        /* A unary operator's operand is a CAST-expression, so `-(int)x`
         * and `!(char)c` parse. */
        n->lhs = parse_cast_expr(p);
        return n;
    }
    if (parse_at_punct(p, PUNCT_PLUSPLUS) ||
        parse_at_punct(p, PUNCT_MINUSMINUS)) {
        AstNode *n = expr_new(p, AST_EXPR_UNARY, t->span);

        n->op = t->punct;
        p->pos++;
        /* Prefix ++ takes a UNARY-expression, so `++(int)x` is a syntax
         * error rather than a cast; sema rejects non-lvalues separately. */
        n->lhs = parse_unary_expr(p);
        return n;
    }
    if (parse_at_kw(p, KW_SIZEOF))
        return parse_sizeof(p);
    if (parse_at_kw(p, KW_ALIGNOF) || parse_at_kw(p, KW_ALT_ALIGNOF) ||
        parse_at_kw(p, KW_ALT_ALIGNOF2))
        return parse_alignof(p);
    return parse_postfix_expr(p);
}

/* --- 6.5.4 cast ---------------------------------------------------------- */

static AstNode *parse_cast_expr(Parser *p)
{
    const Token *t = parse_peek(p);

    if (parse_at_punct(p, PUNCT_LPAREN)) {
        u32 save = p->pos;

        p->pos++;
        if (parse_at_type_name(p)) {
            AstType *ty = parse_type_name(p);
            AstNode *n;

            parse_expect_close(p, PUNCT_RPAREN, t->span, "after the type name");
            /* `(T){...}` is a compound literal — a POSTFIX expression that
             * can take further postfix operators, as in `(int[]){1,2}[0]`
             * and `&(struct S){0}`. */
            if (parse_at_punct(p, PUNCT_LBRACE))
                return parse_postfix_ops(
                    p, finish_compound_literal(p, ty, t->span));
            n = expr_new(p, AST_EXPR_CAST, t->span);
            n->type = ty;
            n->lhs = parse_cast_expr(p); /* right-associative */
            return n;
        }
        /* Not a type-name after all: `(x)(y)` is a CALL when x is an
         * ordinary identifier. Rewind and let the unary/postfix path take
         * it — the entire cast-vs-call ambiguity resolves on this one
         * lookup in the typedef table. */
        p->pos = save;
    }
    return parse_unary_expr(p);
}

/* --- 6.5.5 - 6.5.14 binary ----------------------------------------------- */

/* One table instead of ten near-identical functions. The levels match the
 * 6.5 subclause order exactly, which is what makes the mapping auditable;
 * adjacent levels differ by one, so `prec + 1` in the recursive call is
 * what encodes LEFT associativity. */
static int binop_prec(const Token *t)
{
    if (t->kind != TOK_PUNCT)
        return -1;
    switch (t->punct) {
    case PUNCT_STAR: /* 6.5.5 multiplicative */
    case PUNCT_SLASH:
    case PUNCT_PERCENT:
        return 10;
    case PUNCT_PLUS: /* 6.5.6 additive */
    case PUNCT_MINUS:
        return 9;
    case PUNCT_SHL: /* 6.5.7 shift */
    case PUNCT_SHR:
        return 8;
    case PUNCT_LT: /* 6.5.8 relational */
    case PUNCT_GT:
    case PUNCT_LE:
    case PUNCT_GE:
        return 7;
    case PUNCT_EQEQ: /* 6.5.9 equality */
    case PUNCT_NOTEQ:
        return 6;
    case PUNCT_AMP: /* 6.5.10 bitwise AND */
        return 5;
    case PUNCT_CARET: /* 6.5.11 bitwise XOR */
        return 4;
    case PUNCT_PIPE: /* 6.5.12 bitwise OR */
        return 3;
    case PUNCT_AMPAMP: /* 6.5.13 logical AND */
        return 2;
    case PUNCT_PIPEPIPE: /* 6.5.14 logical OR */
        return 1;
    default:
        return -1;
    }
}

static AstNode *parse_binary_expr(Parser *p, int min_prec)
{
    AstNode *lhs = parse_cast_expr(p);

    for (;;) {
        const Token *t = parse_peek(p);
        int prec = binop_prec(t);
        AstNode *n;

        if (prec < min_prec)
            return lhs;
        p->pos++;
        n = expr_new(p, AST_EXPR_BINARY, t->span);
        n->op = t->punct;
        n->lhs = lhs;
        n->rhs = parse_binary_expr(p, prec + 1); /* +1 => left-assoc */
        lhs = n;
    }
}

/* --- 6.5.15 conditional -------------------------------------------------- */

AstNode *parse_cond_expr(Parser *p)
{
    AstNode *cond = parse_binary_expr(p, 1);
    const Token *q = parse_peek(p);

    if (parse_at_punct(p, PUNCT_QUESTION)) {
        AstNode *n;

        p->pos++;
        if (parse_at_punct(p, PUNCT_COLON))
            parse_error(p, q,
                        "the GNU '?:' form with an omitted middle operand is "
                        "not yet supported (lands in Sprint 55)");
        n = expr_new(p, AST_EXPR_COND, q->span);
        n->lhs = cond;
        /* The middle operand is a FULL expression — `a ? b, c : d` is
         * legal and that comma IS the comma operator. */
        n->mid = parse_expr(p);
        parse_expect_punct(p, PUNCT_COLON, "in a conditional expression");
        /* Right-associative: a?b:c?d:e is a?b:(c?d:e). */
        n->rhs = parse_cond_expr(p);
        return n;
    }
    return cond;
}

/* --- 6.5.16 assignment --------------------------------------------------- */

static bool is_assign_op(const Token *t)
{
    if (t->kind != TOK_PUNCT)
        return false;
    switch (t->punct) {
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

AstNode *parse_assign_expr(Parser *p)
{
    AstNode *lhs = parse_cond_expr(p);
    const Token *t = parse_peek(p);

    if (is_assign_op(t)) {
        AstNode *n = expr_new(p, AST_EXPR_BINARY, t->span);

        n->op = t->punct;
        n->lhs = lhs;
        p->pos++;
        n->rhs = parse_assign_expr(p); /* right-associative */
        return n;
    }
    return lhs;
}

/* --- 6.5.17 comma -------------------------------------------------------- */

AstNode *parse_expr(Parser *p)
{
    AstNode *lhs = parse_assign_expr(p);

    while (parse_at_punct(p, PUNCT_COMMA)) {
        AstNode *n = expr_new(p, AST_EXPR_BINARY, parse_peek(p)->span);

        p->pos++;
        n->op = PUNCT_COMMA;
        n->lhs = lhs;
        n->rhs = parse_assign_expr(p); /* left-associative */
        lhs = n;
    }
    return lhs;
}
