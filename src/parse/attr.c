#include <string.h>

#include "parse/parse.h"

const char *cgf_attr_name(CgfAttrKind kind)
{
    static const char *const names[CGF_ATTR_COUNT] = {
        "cgf_returns_owned", "cgf_takes_ownership", "cgf_borrows",
        "cgf_returns_borrowed", "cgf_no_escape"};

    return (u32)kind < CGF_ATTR_COUNT ? names[kind] : "<invalid-cgf-attr>";
}

static CgfAttr *attr_new(Parser *p, CgfAttrKind kind, u32 arg, Span span)
{
    CgfAttr *a = arena_alloc(p->arena, sizeof(*a), _Alignof(CgfAttr));

    a->kind = kind;
    a->arg = arg;
    a->ir_arg = 0;
    a->span = span;
    a->next = NULL;
    return a;
}

static void append(CgfAttr **head, CgfAttr **tail, CgfAttr *a)
{
    if (*tail)
        (*tail)->next = a;
    else
        *head = a;
    *tail = a;
}

CgfAttr *parse_cgf_attrs_concat(Parser *p, const CgfAttr *a, const CgfAttr *b)
{
    CgfAttr *head = NULL;
    CgfAttr *tail = NULL;
    const CgfAttr *it;

    for (it = a; it; it = it->next)
        append(&head, &tail, attr_new(p, it->kind, it->arg, it->span));
    for (it = b; it; it = it->next)
        append(&head, &tail, attr_new(p, it->kind, it->arg, it->span));
    return head;
}

static bool attr_kind(const char *name, CgfAttrKind *out)
{
    u32 i;

    for (i = 0; i < CGF_ATTR_COUNT; i++)
        if (strcmp(name, cgf_attr_name((CgfAttrKind)i)) == 0) {
            *out = (CgfAttrKind)i;
            return true;
        }
    return false;
}

/* Consume through the matching closer, including nested parentheses. */
static void skip_parens(Parser *p)
{
    u32 depth = 0;

    if (!parse_eat_punct(p, PUNCT_LPAREN))
        return;
    depth = 1;
    while (depth && parse_peek(p)->kind != TOK_EOF) {
        if (parse_eat_punct(p, PUNCT_LPAREN))
            depth++;
        else if (parse_eat_punct(p, PUNCT_RPAREN))
            depth--;
        else
            p->pos++;
    }
}

CgfAttr *parse_cgf_attributes(Parser *p)
{
    const Token *kw = parse_peek(p);
    CgfAttr *head = NULL;
    CgfAttr *tail = NULL;
    bool saw_non_cgf = false;

    p->pos++; /* __attribute__ / __attribute; check_bans allow: token text */
    if (!parse_eat_punct(p, PUNCT_LPAREN) ||
        !parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, kw, "expected '((' after '%s'", kw->spelling);
        skip_parens(p);
        return NULL;
    }

    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF) {
        const Token *name = parse_peek(p);
        CgfAttrKind kind;
        u32 arg = 0;
        bool valid = true;

        if (name->kind != TOK_IDENT && name->kind != TOK_KEYWORD) {
            parse_error(p, name, "expected an attribute name");
            p->pos++;
            valid = false;
        } else {
            p->pos++;
            if (!attr_kind(name->spelling, &kind)) {
                if (strncmp(name->spelling, "cgf_", 4) == 0)
                    parse_error(p, name, "unknown cgf_ attribute '%s'",
                                name->spelling);
                else
                    saw_non_cgf = true;
                valid = false;
            }
        }

        if (valid && kind == CGF_ATTR_RETURNS_OWNED) {
            if (parse_at_punct(p, PUNCT_LPAREN)) {
                const Token *open = parse_peek(p);
                p->pos++;
                if (!parse_eat_punct(p, PUNCT_RPAREN)) {
                    parse_error(p, open, "attribute '%s' takes no arguments",
                                cgf_attr_name(kind));
                    while (!parse_at_punct(p, PUNCT_RPAREN) &&
                           parse_peek(p)->kind != TOK_EOF)
                        p->pos++;
                    parse_eat_punct(p, PUNCT_RPAREN);
                    valid = false;
                }
            }
        } else if (valid) {
            if (!parse_eat_punct(p, PUNCT_LPAREN)) {
                parse_error(p, name,
                            "attribute '%s' requires one integer argument",
                            cgf_attr_name(kind));
                valid = false;
            } else {
                const Token *argtok = parse_peek(p);

                if (argtok->kind != TOK_INT_CONST ||
                    argtok->int_val > UINT32_MAX) {
                    parse_error(p, argtok,
                                "attribute '%s' requires one integer "
                                "constant argument",
                                cgf_attr_name(kind));
                    valid = false;
                } else {
                    arg = (u32)argtok->int_val;
                    p->pos++;
                }
                if (!parse_eat_punct(p, PUNCT_RPAREN)) {
                    parse_error(p, parse_peek(p),
                                "attribute '%s' requires exactly one "
                                "argument",
                                cgf_attr_name(kind));
                    while (!parse_at_punct(p, PUNCT_RPAREN) &&
                           parse_peek(p)->kind != TOK_EOF)
                        p->pos++;
                    parse_eat_punct(p, PUNCT_RPAREN);
                    valid = false;
                }
            }
        } else if (parse_at_punct(p, PUNCT_LPAREN)) {
            /* Unknown/non-cgf attributes are rejected, but consume their
             * complete argument grammar first so recovery always advances. */
            skip_parens(p);
        }

        if (valid)
            append(&head, &tail, attr_new(p, kind, arg, name->span));
        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
    }
    parse_expect_punct(p, PUNCT_RPAREN, "after attribute list");
    parse_expect_punct(p, PUNCT_RPAREN,
                       "after '__attribute__'"); /* check_bans allow */

    if (saw_non_cgf)
        parse_error(p, kw,
                    "GNU '__attribute__' " /* check_bans allow */
                    "is not yet supported for non-cgf attributes "
                    "(lands in Sprint 55)");
    return head;
}
