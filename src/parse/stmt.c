#include <string.h>

#include "parse/parse.h"

/* Statements (C11 6.8). Recursive descent gives the dangling-else binding
 * for free — an `else` is consumed by the innermost `if` still parsing —
 * and it is the reason `case` labels need no special handling: they are
 * ordinary labeled statements that happen to require an enclosing switch,
 * which is exactly why Duff's device parses without a rule for it. */

VEC_DECL(StmtVec, AstNode *);

static AstNode *stmt_new(Parser *p, AstKind k, Span sp)
{
    return ast_new(p->arena, k, sp);
}

/* --- labels: function scope, own namespace ------------------------------- */

static LabelEntry *label_find(Parser *p, const char *name)
{
    LabelEntry *e;

    for (e = p->labels; e; e = e->next)
        if (e->name == name) /* interned: pointer compare */
            return e;
    return NULL;
}

static LabelEntry *label_intern(Parser *p, const char *name, Span sp)
{
    LabelEntry *e = label_find(p, name);

    if (e)
        return e;
    e = arena_alloc(p->arena, sizeof(LabelEntry), _Alignof(LabelEntry));
    memset(e, 0, sizeof(*e));
    e->name = name;
    e->first_use = sp;
    e->next = p->labels;
    p->labels = e;
    return e;
}

/* --- block items --------------------------------------------------------- */

/* Inside a block, a declaration and an expression statement are told apart
 * by exactly the machinery Sprint 9 built: a leading typedef name means
 * declaration, an ordinary identifier means expression. `T * p;` is a
 * declaration iff T is a visible typedef, and multiplication otherwise. */
static AstNode *parse_block_item(Parser *p, bool *saw_stmt)
{
    /* `ident ident` at block scope is the one shape that cannot be an
     * expression, so it is the only one the unknown-type heuristic may
     * claim here — see parse_at_unknown_type. */
    if (parse_at_decl_specs(p) || parse_at_unknown_type(p)) {
        const Token *start = parse_peek(p);
        AstNode *d;
        AstNode *n;

        /* c89 forbids declarations after the first statement in a block.
         * We still PARSE them — rejecting outright would cascade — and
         * pedwarn with gcc's wording. */
        if (*saw_stmt && !std_is_c99_or_later(p->lang->std))
            diag_emit(p->dc, DIAG_WARNING, start->span,
                      "ISO C90 forbids mixed declarations and code");
        d = parse_declaration(p, false);
        n = stmt_new(p, AST_STMT_DECL, start->span);
        n->lhs = d;
        return n;
    }
    *saw_stmt = true;
    return parse_stmt(p);
}

/* Parses `{ ... }` WITHOUT pushing a scope — the caller owns that choice,
 * because a function body's outermost block shares the parameter scope
 * (6.2.1p4) while every other compound statement gets a fresh one. */
AstNode *parse_compound_stmt(Parser *p)
{
    const Token *lb = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_COMPOUND, lb->span);
    StmtVec items = {NULL, 0, 0};
    bool saw_stmt = false;

    parse_expect_punct(p, PUNCT_LBRACE, "to open a block");
    while (!parse_at_punct(p, PUNCT_RBRACE) && parse_peek(p)->kind != TOK_EOF) {
        u32 before = p->pos;
        AstNode *it;

        /* The cap latched: stop producing work nobody will read. Nothing
         * exits here — the driver turns the latch into exit 1. */
        if (diag_error_limit_reached(p->dc))
            break;
        it = parse_block_item(p, &saw_stmt);

        if (it)
            StmtVec_push(&items, it);
        if (p->pos == before) {
            /* No progress: consume a token so malformed input cannot spin.
             * Real recovery is Sprint 11's. */
            p->pos++;
        }
    }
    parse_expect_punct(p, PUNCT_RBRACE, "to close a block");

    n->nitems = (u32)items.len;
    if (items.len) {
        n->items = arena_alloc(p->arena, items.len * sizeof(AstNode *),
                               _Alignof(AstNode *));
        memcpy(n->items, items.data, items.len * sizeof(AstNode *));
    }
    StmtVec_free(&items);
    return n;
}

static AstNode *parse_scoped_compound(Parser *p)
{
    AstNode *n;

    parse_scope_enter(p);
    p->scope_depth++;
    n = parse_compound_stmt(p);
    p->scope_depth--;
    parse_scope_leave(p);
    return n;
}

/* --- individual statements ----------------------------------------------- */

static AstNode *parse_if(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_IF, kw->span);

    p->pos++;
    parse_expect_punct(p, PUNCT_LPAREN, "after 'if'");
    n->lhs = parse_expr(p);
    parse_expect_punct(p, PUNCT_RPAREN, "after the 'if' condition");
    n->body = parse_stmt(p);
    /* Dangling else: this recursive call belongs to the INNERMOST 'if'
     * still on the stack, which is the C rule. No lookahead needed. */
    if (parse_eat_kw(p, KW_ELSE))
        n->rhs = parse_stmt(p);
    return n;
}

static AstNode *parse_switch(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_SWITCH, kw->span);

    p->pos++;
    parse_expect_punct(p, PUNCT_LPAREN, "after 'switch'");
    n->lhs = parse_expr(p);
    parse_expect_punct(p, PUNCT_RPAREN,
                       "after the 'switch' controlling "
                       "expression");
    /* The body is just a statement, and `case`/`default` are LABELS that
     * may sit anywhere inside it — including inside a nested loop, which
     * is what makes Duff's device legal. So we track the switch with a
     * depth counter and let parse_stmt find the labels wherever they are,
     * rather than special-casing "case list at the top of a block". */
    p->switch_depth++;
    p->break_depth++;
    n->body = parse_stmt(p);
    p->break_depth--;
    p->switch_depth--;
    return n;
}

static AstNode *parse_while(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_WHILE, kw->span);

    p->pos++;
    parse_expect_punct(p, PUNCT_LPAREN, "after 'while'");
    n->lhs = parse_expr(p);
    parse_expect_punct(p, PUNCT_RPAREN, "after the 'while' condition");
    p->loop_depth++;
    p->break_depth++;
    n->body = parse_stmt(p);
    p->break_depth--;
    p->loop_depth--;
    return n;
}

static AstNode *parse_do(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_DO, kw->span);

    p->pos++;
    p->loop_depth++;
    p->break_depth++;
    n->body = parse_stmt(p);
    p->break_depth--;
    p->loop_depth--;
    if (!parse_eat_kw(p, KW_WHILE))
        parse_error(p, parse_peek(p), "expected 'while' after a 'do' body");
    parse_expect_punct(p, PUNCT_LPAREN, "after 'while'");
    n->lhs = parse_expr(p);
    parse_expect_punct(p, PUNCT_RPAREN, "after the 'while' condition");
    parse_expect_punct(p, PUNCT_SEMI, "after 'do ... while (...)'");
    return n;
}

static AstNode *parse_for(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_FOR, kw->span);

    p->pos++;
    /* The scope opens BEFORE the '(' because a for-init declaration is in
     * scope for the condition, the step, AND the body — and ends with the
     * loop, so a same-named variable after the loop is a different object.
     * A block inside the body is a further, inner scope. */
    parse_scope_enter(p);
    p->scope_depth++;
    parse_expect_punct(p, PUNCT_LPAREN, "after 'for'");

    if (parse_at_punct(p, PUNCT_SEMI)) {
        p->pos++;
    } else if (parse_at_decl_specs(p)) {
        if (!std_is_c99_or_later(p->lang->std))
            parse_error(p, parse_peek(p),
                        "declarations in a 'for' initializer are a C99 "
                        "feature");
        n->lhs = parse_declaration(p, false); /* consumes its own ';' */
    } else {
        AstNode *e = stmt_new(p, AST_STMT_EXPR, parse_peek(p)->span);
        e->lhs = parse_expr(p);
        n->lhs = e;
        parse_expect_punct(p, PUNCT_SEMI, "after the 'for' initializer");
    }

    if (!parse_at_punct(p, PUNCT_SEMI))
        n->mid = parse_expr(p);
    parse_expect_punct(p, PUNCT_SEMI, "after the 'for' condition");
    if (!parse_at_punct(p, PUNCT_RPAREN))
        n->rhs = parse_expr(p);
    parse_expect_punct(p, PUNCT_RPAREN, "after the 'for' clauses");

    p->loop_depth++;
    p->break_depth++;
    n->body = parse_stmt(p);
    p->break_depth--;
    p->loop_depth--;

    p->scope_depth--;
    parse_scope_leave(p);
    return n;
}

static AstNode *parse_return(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_RETURN, kw->span);

    p->pos++;
    if (!parse_at_punct(p, PUNCT_SEMI))
        n->lhs = parse_expr(p); /* value/void mismatch is Sprint 13's */
    parse_expect_punct(p, PUNCT_SEMI, "after 'return'");
    return n;
}

static AstNode *parse_goto(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_GOTO, kw->span);
    const Token *id;

    p->pos++;
    if (parse_at_punct(p, PUNCT_STAR)) {
        parse_error(p, parse_peek(p),
                    "computed goto is a GNU extension (lands in Sprint 55)");
        while (!parse_at_punct(p, PUNCT_SEMI) && parse_peek(p)->kind != TOK_EOF)
            p->pos++;
        parse_eat_punct(p, PUNCT_SEMI);
        return n;
    }
    id = parse_peek(p);
    if (id->kind != TOK_IDENT) {
        parse_error(p, id, "expected a label name after 'goto'");
    } else {
        n->name = id->spelling;
        /* A goto may precede its label, so record the use and reconcile
         * at the end of the function — this is the earliest moment an
         * undefined label is knowable. */
        label_intern(p, id->spelling, id->span);
        p->pos++;
    }
    parse_expect_punct(p, PUNCT_SEMI, "after 'goto'");
    return n;
}

static AstNode *parse_case(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_CASE, kw->span);

    p->pos++;
    if (p->switch_depth == 0)
        parse_error(p, kw, "'case' label not within a switch statement");
    /* The value is a constant expression, stored unevaluated: Sprint 15
     * folds it, and only then can duplicate labels be diagnosed. */
    n->lhs = parse_cond_expr(p);
    parse_expect_punct(p, PUNCT_COLON, "after a 'case' label");
    n->body = parse_stmt(p);
    return n;
}

static AstNode *parse_default(Parser *p)
{
    const Token *kw = parse_peek(p);
    AstNode *n = stmt_new(p, AST_STMT_DEFAULT, kw->span);

    p->pos++;
    if (p->switch_depth == 0)
        parse_error(p, kw, "'default' label not within a switch statement");
    parse_expect_punct(p, PUNCT_COLON, "after 'default'");
    n->body = parse_stmt(p);
    return n;
}

AstNode *parse_stmt(Parser *p)
{
    const Token *t = parse_peek(p);

    if (parse_at_punct(p, PUNCT_LBRACE))
        return parse_scoped_compound(p);
    if (parse_eat_punct(p, PUNCT_SEMI))
        return stmt_new(p, AST_STMT_NULL, t->span);

    if (t->kind == TOK_KEYWORD) {
        switch ((Keyword)t->kw) {
        case KW_IF:
            return parse_if(p);
        case KW_SWITCH:
            return parse_switch(p);
        case KW_WHILE:
            return parse_while(p);
        case KW_DO:
            return parse_do(p);
        case KW_FOR:
            return parse_for(p);
        case KW_RETURN:
            return parse_return(p);
        case KW_GOTO:
            return parse_goto(p);
        case KW_CASE:
            return parse_case(p);
        case KW_DEFAULT:
            return parse_default(p);
        case KW_BREAK: {
            AstNode *n = stmt_new(p, AST_STMT_BREAK, t->span);
            p->pos++;
            if (p->break_depth == 0)
                parse_error(p, t,
                            "'break' outside of a loop or switch statement");
            parse_expect_punct(p, PUNCT_SEMI, "after 'break'");
            return n;
        }
        case KW_CONTINUE: {
            AstNode *n = stmt_new(p, AST_STMT_CONTINUE, t->span);
            p->pos++;
            /* `continue` needs a LOOP; a switch does not accept it, which
             * is why break and continue keep separate counters. */
            if (p->loop_depth == 0)
                parse_error(p, t, "'continue' outside of a loop");
            parse_expect_punct(p, PUNCT_SEMI, "after 'continue'");
            return n;
        }
        default:
            break;
        }
    }

    /* A label: IDENT ':' — and labels live in their own namespace, so
     * `foo: foo = 1; goto foo;` is three different meanings of `foo` and
     * all three are legal. Two-token lookahead separates this from an
     * expression statement starting with the same identifier. */
    if (t->kind == TOK_IDENT && parse_peek_n(p, 1)->kind == TOK_PUNCT &&
        parse_peek_n(p, 1)->punct == PUNCT_COLON) {
        AstNode *n = stmt_new(p, AST_STMT_LABEL, t->span);
        LabelEntry *e = label_intern(p, t->spelling, t->span);

        if (e->defined)
            parse_error(p, t, "duplicate label '%s'", t->spelling);
        e->defined = true;
        n->name = t->spelling;
        p->pos += 2;
        n->body = parse_stmt(p);
        return n;
    }

    {
        AstNode *n = stmt_new(p, AST_STMT_EXPR, t->span);

        n->lhs = parse_expr(p);
        parse_poison_from(n, n->lhs);
        if (!parse_eat_punct(p, PUNCT_SEMI)) {
            parse_error_after_prev(p, PUNCT_SEMI, "after an expression");
            n->poisoned = true;
            /* Synchronize: one missing ';' should cost one diagnostic, not
             * one per statement for the rest of the function. */
            p->recovering = true;
            parse_sync(p, SYNC_STMT);
        }
        return n;
    }
}

/* A function body. The outermost block deliberately does NOT push a scope:
 * the parameter scope opened by the declarator is still current, and
 * 6.2.1p4 says parameters and the body's top-level declarations share it —
 * which is what makes `int f(int x) { int x; }` a redeclaration error
 * rather than legal shadowing. */
AstNode *parse_func_body(Parser *p)
{
    AstNode *body;
    LabelEntry *e;
    bool saved_in_body = p->in_func_body;
    LabelEntry *saved_labels = p->labels;

    p->labels = NULL;
    p->in_func_body = true;
    /* The caller (the function-definition path) already opened the scope
     * that the parameters live in; this block IS that scope. */
    body = parse_compound_stmt(p);

    /* Every goto must name a label defined SOMEWHERE in this function —
     * knowable only now, because a goto may precede its label. */
    for (e = p->labels; e; e = e->next)
        if (!e->defined)
            diag_emit(p->dc, DIAG_ERROR, e->first_use,
                      "use of undeclared label '%s'", e->name);
    if (p->labels && !p->nerrors) {
        /* nerrors is the parser's own counter; label errors go straight to
         * the sink, so keep the count honest for callers that gate on it. */
        for (e = p->labels; e; e = e->next)
            if (!e->defined)
                p->nerrors++;
    }
    p->labels = saved_labels;
    p->in_func_body = saved_in_body;
    return body;
}
