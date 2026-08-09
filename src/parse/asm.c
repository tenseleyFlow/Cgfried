#include <string.h>

#include "parse/parse.h"

/* Inline assembly (Sprint 55 D2).
 *
 *   asm [volatile|inline] ( template
 *                           [ : outputs [ : inputs [ : clobbers ] ] ] )
 *
 * TWO DIALECTS, and the trigger is PUNCTUATION rather than operand counts.
 * A template with no colon after it is BASIC: its `%` characters reach the
 * assembler verbatim, because there are no operands for them to name. The
 * moment a colon appears the construct is EXTENDED and `%` is the operand
 * escape, so `%%` means a literal one. Measured against gcc, which emits
 * `%%eax` for `asm("%%eax")` and `%eax` for `asm("%%eax" : : )` -- three
 * empty operand lists still switch dialects.
 *
 * The template and every constraint are STRING LITERALS, so adjacent-string
 * concatenation (phase 6) has already happened by the time the parser sees
 * them -- musl writes its syscall templates across several lines and relies
 * on exactly that.
 *
 * `asm goto` is refused in the statement path before this runs: jumping out
 * of an asm block needs CFG edges the IR verifier could only trust rather
 * than check. */

/* One string operand, already lexed and concatenated. Returns NULL and
 * reports at anything else, because every position here takes a string and
 * the alternative is silently accepting a construct we cannot honour. */
static const char *asm_string(Parser *p, const char *what)
{
    const Token *t = parse_peek(p);

    if (t->kind != TOK_STRING || !t->str.bytes) {
        parse_error(p, t, "expected a string literal for the %s", what);
        return NULL;
    }
    p->pos++;
    return arena_strdup(p->arena, (const char *)t->str.bytes);
}

/* `[name] "constraint" ( expr )`, one operand. The bracketed name is
 * optional and is how a template says `%[name]` instead of `%3` -- which is
 * the difference between an edit that survives inserting an operand and one
 * that silently renumbers every reference after it. */
static bool asm_operand(Parser *p, AsmOperand *out)
{
    memset(out, 0, sizeof(*out));
    out->span = parse_peek(p)->span;
    if (parse_at_punct(p, PUNCT_LBRACKET)) {
        const Token *id;

        p->pos++;
        id = parse_peek(p);
        if (id->kind != TOK_IDENT) {
            parse_error(p, id, "expected a symbolic operand name after '['");
            return false;
        }
        out->name = id->spelling;
        p->pos++;
        if (!parse_eat_punct(p, PUNCT_RBRACKET)) {
            parse_error(p, parse_peek(p),
                        "expected ']' after the symbolic operand name");
            return false;
        }
    }
    out->constraint = asm_string(p, "operand constraint");
    if (!out->constraint)
        return false;
    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, parse_peek(p),
                    "expected '(' before the asm operand expression");
        return false;
    }
    /* An ASSIGNMENT expression, not a comma expression: the comma separates
     * operands here, so `"r"(a, b)` is two operands' worth of syntax in one
     * pair of parentheses and gcc rejects it too. */
    out->expr = parse_assign_expr(p);
    if (!parse_eat_punct(p, PUNCT_RPAREN)) {
        parse_error(p, parse_peek(p),
                    "expected ')' after the asm operand expression");
        return false;
    }
    return true;
}

/* gcc's own ceiling, and it is a real one rather than a defensive guess:
 * `more than 30 operands in 'asm'` is what it says at 31. Matching it keeps
 * the operand arrays fixed-size through the whole pipeline, which is worth
 * more than supporting an asm nobody writes. */
#define ASM_MAX_OPERANDS 30
#define ASM_MAX_CLOBBERS 64

/* A colon-introduced operand list, appending into `buf`. An empty list is
 * legal and common: `asm("" : : : "memory")` has two of them. */
static bool asm_operand_list(Parser *p, AsmOperand *buf, u32 *total, u32 *count)
{
    *count = 0;
    if (parse_at_punct(p, PUNCT_RPAREN) || parse_at_punct(p, PUNCT_COLON))
        return true;
    for (;;) {
        AsmOperand op;

        if (!asm_operand(p, &op))
            return false;
        if (*total >= ASM_MAX_OPERANDS) {
            parse_error(p, parse_peek(p), "more than %d operands in 'asm'",
                        ASM_MAX_OPERANDS);
            return false;
        }
        buf[(*total)++] = op;
        (*count)++;
        if (!parse_eat_punct(p, PUNCT_COMMA))
            return true;
    }
}

static bool asm_clobber_list(Parser *p, const char **buf, u32 *n)
{
    if (parse_at_punct(p, PUNCT_RPAREN) || parse_at_punct(p, PUNCT_COLON))
        return true;
    for (;;) {
        const char *s = asm_string(p, "clobber");

        if (!s)
            return false;
        if (*n >= ASM_MAX_CLOBBERS) {
            parse_error(p, parse_peek(p), "more than %d clobbers in 'asm'",
                        ASM_MAX_CLOBBERS);
            return false;
        }
        buf[(*n)++] = s;
        if (!parse_eat_punct(p, PUNCT_COMMA))
            return true;
    }
}

/* Everything from the '(' to the matching ')'. The caller has already
 * consumed the asm keyword and its qualifiers and filled asm_volatile. */
bool parse_asm_body(Parser *p, AstNode *n)
{
    AsmOperand ops[ASM_MAX_OPERANDS];
    const char *clob[ASM_MAX_CLOBBERS];
    u32 nops = 0;
    u32 nclob = 0;
    u32 ninputs = 0;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, parse_peek(p), "expected '(' after 'asm'");
        return false;
    }
    n->asm_tmpl = asm_string(p, "asm template");
    if (!n->asm_tmpl)
        return false;

    /* THE DIALECT DECISION, and it is this one token. */
    n->asm_basic = !parse_at_punct(p, PUNCT_COLON);
    if (n->asm_basic) {
        if (!parse_eat_punct(p, PUNCT_RPAREN)) {
            parse_error(p, parse_peek(p),
                        "expected ')' after the asm template");
            return false;
        }
        return true;
    }

    p->pos++; /* the first ':' */
    if (!asm_operand_list(p, ops, &nops, &n->asm_noutputs))
        return false;
    if (parse_eat_punct(p, PUNCT_COLON)) {
        if (!asm_operand_list(p, ops, &nops, &ninputs))
            return false;
        if (parse_eat_punct(p, PUNCT_COLON)) {
            if (!asm_clobber_list(p, clob, &nclob))
                return false;
            /* A fourth colon introduces asm goto's label list. The
             * statement path refuses `asm goto` by name before reaching
             * here, so arriving with one means the `goto` was missing and
             * the labels are meaningless. */
            if (parse_at_punct(p, PUNCT_COLON)) {
                parse_error(p, parse_peek(p),
                            "a label list requires 'asm goto', which is not "
                            "supported (docs/gnu-extensions.md)");
                return false;
            }
        }
    }
    if (!parse_eat_punct(p, PUNCT_RPAREN)) {
        parse_error(p, parse_peek(p), "expected ')' after the asm operands");
        return false;
    }
    if (nops) {
        n->asm_ops = arena_alloc(p->arena, nops * sizeof(AsmOperand),
                                 _Alignof(AsmOperand));
        memcpy(n->asm_ops, ops, nops * sizeof(AsmOperand));
    }
    n->asm_nops = nops;
    if (nclob) {
        n->asm_clobbers = arena_alloc(p->arena, nclob * sizeof(const char *),
                                      _Alignof(const char *));
        memcpy(n->asm_clobbers, clob, nclob * sizeof(const char *));
    }
    n->asm_nclobbers = nclob;
    return true;
}
