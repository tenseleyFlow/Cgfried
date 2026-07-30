#include <string.h>

#include "parse/parse.h"

/* The constant-expression subset declarations need: enum values, bitfield
 * widths, array bounds, initializer elements. SPRINT 10 REPLACES THIS with
 * the full C expression grammar (assignment, comma, casts, sizeof,
 * _Generic, compound literals, postfix chains). Everything parsed here is
 * stored verbatim — no folding, no typing; Sprint 15 evaluates. */

static AstNode *expr_binary(Parser *p, int min_prec);

static AstNode *expr_primary(Parser *p)
{
    const Token *t = parse_peek(p);
    AstNode *n;

    switch ((TokenKind)t->kind) {
    case TOK_INT_CONST:
        n = ast_new(p->arena, AST_EXPR_INT, t->span);
        n->tok = t;
        p->pos++;
        return n;
    case TOK_FLOAT_CONST:
        n = ast_new(p->arena, AST_EXPR_FLOAT, t->span);
        n->tok = t;
        p->pos++;
        return n;
    case TOK_CHAR_CONST:
        n = ast_new(p->arena, AST_EXPR_CHAR, t->span);
        n->tok = t;
        p->pos++;
        return n;
    case TOK_STRING:
        n = ast_new(p->arena, AST_EXPR_STRING, t->span);
        n->tok = t;
        p->pos++;
        return n;
    case TOK_IDENT:
        n = ast_new(p->arena, AST_EXPR_IDENT, t->span);
        n->tok = t;
        n->name = t->spelling;
        p->pos++;
        return n;
    default:
        break;
    }
    if (parse_eat_punct(p, PUNCT_LPAREN)) {
        n = ast_new(p->arena, AST_EXPR_PAREN, t->span);
        n->lhs = parse_cond_expr(p);
        parse_expect_punct(p, PUNCT_RPAREN, "after parenthesized expression");
        return n;
    }
    if (parse_at_kw(p, KW_SIZEOF) || parse_at_kw(p, KW_ALIGNOF) ||
        parse_at_kw(p, KW_ALT_ALIGNOF) || parse_at_kw(p, KW_ALT_ALIGNOF2)) {
        /* sizeof/_Alignof appear in constant contexts constantly; the
         * OPERAND grammar (type-name vs unary-expression, and the
         * `sizeof (T){1}` compound-literal case) is Sprint 10's. */
        n = ast_new(p->arena, AST_EXPR_UNARY, t->span);
        n->op = 0;
        n->tok = t;
        p->pos++;
        if (parse_at_punct(p, PUNCT_LPAREN)) {
            u32 depth = 0;
            do {
                if (parse_at_punct(p, PUNCT_LPAREN))
                    depth++;
                else if (parse_at_punct(p, PUNCT_RPAREN))
                    depth--;
                p->pos++;
            } while (depth && parse_peek(p)->kind != TOK_EOF);
        } else {
            n->lhs = expr_primary(p);
        }
        return n;
    }

    parse_error(p, t, "expected an expression but found '%s'",
                t->kind == TOK_EOF ? "end of file" : t->spelling);
    n = ast_new(p->arena, AST_ERROR, t->span);
    n->poisoned = true;
    return n;
}

static AstNode *expr_unary(Parser *p)
{
    const Token *t = parse_peek(p);

    if (t->kind == TOK_PUNCT &&
        (t->punct == PUNCT_MINUS || t->punct == PUNCT_PLUS ||
         t->punct == PUNCT_BANG || t->punct == PUNCT_TILDE ||
         t->punct == PUNCT_STAR || t->punct == PUNCT_AMP)) {
        AstNode *n = ast_new(p->arena, AST_EXPR_UNARY, t->span);
        n->op = t->punct;
        p->pos++;
        n->lhs = expr_unary(p);
        return n;
    }
    return expr_primary(p);
}

/* Precedence climbing over the binary operators. Sprint 10 replaces this
 * with the full 17-level table; the levels here are the same values so
 * the swap is mechanical. */
static int binop_prec(const Token *t)
{
    if (t->kind != TOK_PUNCT)
        return -1;
    switch (t->punct) {
    case PUNCT_STAR:
    case PUNCT_SLASH:
    case PUNCT_PERCENT:
        return 10;
    case PUNCT_PLUS:
    case PUNCT_MINUS:
        return 9;
    case PUNCT_SHL:
    case PUNCT_SHR:
        return 8;
    case PUNCT_LT:
    case PUNCT_GT:
    case PUNCT_LE:
    case PUNCT_GE:
        return 7;
    case PUNCT_EQEQ:
    case PUNCT_NOTEQ:
        return 6;
    case PUNCT_AMP:
        return 5;
    case PUNCT_CARET:
        return 4;
    case PUNCT_PIPE:
        return 3;
    case PUNCT_AMPAMP:
        return 2;
    case PUNCT_PIPEPIPE:
        return 1;
    default:
        return -1;
    }
}

static AstNode *expr_binary(Parser *p, int min_prec)
{
    AstNode *lhs = expr_unary(p);

    for (;;) {
        const Token *t = parse_peek(p);
        int prec = binop_prec(t);
        AstNode *n;

        if (prec < min_prec)
            return lhs;
        p->pos++;
        n = ast_new(p->arena, AST_EXPR_BINARY, t->span);
        n->op = t->punct;
        n->lhs = lhs;
        n->rhs = expr_binary(p, prec + 1);
        lhs = n;
    }
}

AstNode *parse_cond_expr(Parser *p)
{
    AstNode *cond = expr_binary(p, 1);

    if (parse_at_punct(p, PUNCT_QUESTION)) {
        AstNode *n = ast_new(p->arena, AST_EXPR_COND, parse_peek(p)->span);
        p->pos++;
        n->lhs = cond;
        /* The middle operand is a FULL expression (a?b,c:d is legal); the
         * comma operator arrives with Sprint 10. */
        n->mid = parse_cond_expr(p);
        parse_expect_punct(p, PUNCT_COLON, "in conditional expression");
        n->rhs = parse_cond_expr(p); /* right-associative */
        return n;
    }
    return cond;
}

AstNode *parse_assign_expr(Parser *p)
{
    AstNode *lhs = parse_cond_expr(p);
    const Token *t = parse_peek(p);

    if (t->kind == TOK_PUNCT &&
        (t->punct == PUNCT_ASSIGN || t->punct == PUNCT_STAR_ASSIGN ||
         t->punct == PUNCT_SLASH_ASSIGN || t->punct == PUNCT_PERCENT_ASSIGN ||
         t->punct == PUNCT_PLUS_ASSIGN || t->punct == PUNCT_MINUS_ASSIGN ||
         t->punct == PUNCT_SHL_ASSIGN || t->punct == PUNCT_SHR_ASSIGN ||
         t->punct == PUNCT_AMP_ASSIGN || t->punct == PUNCT_CARET_ASSIGN ||
         t->punct == PUNCT_PIPE_ASSIGN)) {
        AstNode *n = ast_new(p->arena, AST_EXPR_BINARY, t->span);
        n->op = t->punct;
        n->lhs = lhs;
        p->pos++;
        n->rhs = parse_assign_expr(p); /* right-associative */
        return n;
    }
    return lhs;
}
