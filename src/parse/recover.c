#include <string.h>

#include "parse/parse.h"

/* Panic-mode recovery.
 *
 * The shape is the classic one: on an error, poison the construct, skip to
 * a token the grammar can restart from, and stay QUIET in between. The
 * quiet part is what turns one mistake into one diagnostic — a parser that
 * keeps reporting while it is lost produces a screenful of consequences
 * and buries the cause.
 *
 * Two invariants make this safe rather than merely quiet:
 *
 *   PROGRESS. parse_sync must always leave the cursor strictly ahead of
 *   where a caller would loop. If the cursor is already ON a sync token
 *   that the failing production cannot restart from, we consume one token
 *   anyway. Without this the "no progress" guards in the block and
 *   translation-unit loops are the only thing between us and a hang, and
 *   the fuzzer finds that immediately.
 *
 *   SCOPE BALANCE. parse_sync stops BEFORE a '}' and never consumes it.
 *   The brace belongs to an enclosing block whose scope the caller must
 *   pop; eating it here would unbalance parse_scope_enter/leave and
 *   corrupt the typedef table for the rest of the file. */

static bool tok_is_decl_start(Parser *p)
{
    return parse_at_decl_specs(p);
}

static bool tok_is_stmt_start(Parser *p)
{
    const Token *t = parse_peek(p);

    if (t->kind == TOK_KEYWORD) {
        switch ((Keyword)t->kw) {
        case KW_IF:
        case KW_WHILE:
        case KW_FOR:
        case KW_DO:
        case KW_SWITCH:
        case KW_RETURN:
        case KW_BREAK:
        case KW_CONTINUE:
        case KW_GOTO:
        case KW_CASE:
        case KW_DEFAULT:
            return true;
        default:
            break;
        }
    }
    return parse_at_punct(p, PUNCT_LBRACE) || tok_is_decl_start(p);
}

/* True if `t` is a point the given production can resume from. `;` and `}`
 * are universal; the rest are FIRST sets. */
static bool at_sync_point(Parser *p, SyncSet set)
{
    const Token *t = parse_peek(p);

    if (t->kind == TOK_EOF)
        return true;
    if (parse_at_punct(p, PUNCT_SEMI) || parse_at_punct(p, PUNCT_RBRACE))
        return true;
    switch (set) {
    case SYNC_STMT:
        return tok_is_stmt_start(p);
    case SYNC_DECL:
        return tok_is_decl_start(p);
    case SYNC_MEMBER:
    case SYNC_PAREN:
        return false; /* `;` and `}` above are the whole set */
    }
    return false;
}

void parse_sync(Parser *p, SyncSet set)
{
    u32 start = p->pos;
    u32 depth = 0;

    for (;;) {
        const Token *t = parse_peek(p);

        if (t->kind == TOK_EOF)
            break;

        /* Never sync out of a bracket we are inside: a stray ';' between
         * '(' and ')' is not a restart point for the enclosing statement,
         * and treating it as one strands the ')'. */
        if (parse_at_punct(p, PUNCT_LPAREN) ||
            parse_at_punct(p, PUNCT_LBRACKET)) {
            depth++;
            p->pos++;
            continue;
        }
        if (parse_at_punct(p, PUNCT_RPAREN) ||
            parse_at_punct(p, PUNCT_RBRACKET)) {
            if (depth == 0) {
                /* A closer for a bracket opened OUTSIDE this production.
                 * SYNC_PAREN stops here so the caller can consume it;
                 * everything else must not run past it either. */
                break;
            }
            depth--;
            p->pos++;
            continue;
        }
        if (depth == 0 && at_sync_point(p, set)) {
            /* Stop BEFORE '}' — see SCOPE BALANCE above. A ';' we may
             * consume, since it ends the broken construct. */
            if (parse_at_punct(p, PUNCT_SEMI) && p->pos > start)
                p->pos++;
            break;
        }
        p->pos++;
    }

    /* PROGRESS: a caller that called us because it could not proceed must
     * not find the cursor where it left it. */
    if (p->pos == start && parse_peek(p)->kind != TOK_EOF)
        p->pos++;

    /* Recovery complete: diagnostics are meaningful again. */
    p->recovering = false;
}

AstNode *parse_error_node(Parser *p, Span sp)
{
    AstNode *n = ast_new(p->arena, AST_ERROR, sp);

    n->poisoned = true;
    /* Everything until the next successful sync is a CONSEQUENCE of the
     * error we just reported, so parse_error stays silent (and counts what
     * it dropped) until parse_sync clears this. */
    p->recovering = true;
    return n;
}

/* Poison flows UPWARD at construction time — a node built from a poisoned
 * child is itself poisoned — so nothing ever re-walks the tree looking for
 * error nodes. Sema's contract (Sprint 12+): never emit a diagnostic about
 * a poisoned subtree; give it the error type and stay silent. */
AstNode *parse_poison_from(AstNode *n, const AstNode *child)
{
    if (n && child && child->poisoned)
        n->poisoned = true;
    return n;
}

/* Bracket nesting. The limit exists so pathological input reports a clean
 * error instead of overflowing the stack in recursive descent; C11 5.2.4.1
 * only requires 63 levels, and 256 is comfortably above anything real. */
bool parse_depth_enter(Parser *p, const Token *at)
{
    if (p->bracket_depth >= CGF_MAX_BRACKET_DEPTH) {
        parse_error(p, at, "bracket nesting exceeds %d", CGF_MAX_BRACKET_DEPTH);
        return false;
    }
    p->bracket_depth++;
    return true;
}

void parse_depth_leave(Parser *p)
{
    if (p->bracket_depth == 0)
        CGF_ICE("parse_depth_leave: bracket depth underflow");
    p->bracket_depth--;
}

/* "expected ')'" is far more useful with "to match this '('" attached, so
 * every bracket production records its opener and cites it on failure. */
void parse_expect_close(Parser *p, PpPunct closer, Span opener,
                        const char *what)
{
    if (parse_eat_punct(p, closer))
        return;
    parse_error_after_prev(p, closer, what);
    diag_emit(p->dc, DIAG_NOTE, opener, "to match this '%s'",
              closer == PUNCT_RPAREN   ? "("
              : closer == PUNCT_RBRACE ? "{"
                                       : "[");
}
