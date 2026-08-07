#include <string.h>

#include "parse/parse.h"
#include "warn/warn.h"

void gnu_attrs_merge(GnuDeclAttrs *dst, const GnuDeclAttrs *src)
{
    if (!src)
        return;
    dst->weak |= src->weak;
    /* Last visibility wins, which is gcc's rule; an unspecified one never
     * clears a specified one. */
    if (src->visibility)
        dst->visibility = src->visibility;
}

const char *gnu_visibility_name(u8 vis)
{
    switch ((GnuVisibility)vis) {
    case GNU_VIS_DEFAULT:
        return "default";
    case GNU_VIS_HIDDEN:
        return "hidden";
    case GNU_VIS_PROTECTED:
        return "protected";
    case GNU_VIS_INTERNAL:
        return "internal";
    case GNU_VIS_UNSPEC:
        break;
    }
    return "";
}

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

/* The GNU attribute classification, from src/parse/gnu_attrs.def. See that
 * file for the one question that decides every row. */
typedef enum { GA_IGNORE, GA_UNSAFE, GA_UNKNOWN } GnuAttrClass;

/* `packed` and `__packed__` are one attribute. Headers use the underscored
 * spelling precisely so a macro named `packed` cannot capture it, so every
 * comparison has to see through it. */
static bool gnu_attr_is(const char *spelling, const char *bare)
{
    size_t n = strlen(spelling);
    size_t b = strlen(bare);

    if (strcmp(spelling, bare) == 0)
        return true;
    return n == b + 4 && strncmp(spelling, "__", 2) == 0 &&
           strncmp(spelling + 2, bare, b) == 0 &&
           strcmp(spelling + 2 + b, "__") == 0;
}

/* visibility("default"|"hidden"|"protected"|"internal"). The argument is a
 * STRING rather than an identifier, and an unknown one is an error rather
 * than a silent default -- picking a visibility nobody asked for is the
 * linkage equivalent of ignoring the attribute outright. */
static void parse_visibility(Parser *p, const Token *name, GnuDeclAttrs *gnu)
{
    const Token *arg;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, name, "attribute 'visibility' requires an argument");
        return;
    }
    arg = parse_peek(p);
    if (arg->kind != TOK_STRING || !arg->str.bytes) {
        parse_error(p, arg, "'visibility' takes a string argument");
    } else if (strcmp((const char *)arg->str.bytes, "default") == 0) {
        gnu->visibility = GNU_VIS_DEFAULT;
        p->pos++;
    } else if (strcmp((const char *)arg->str.bytes, "hidden") == 0) {
        gnu->visibility = GNU_VIS_HIDDEN;
        p->pos++;
    } else if (strcmp((const char *)arg->str.bytes, "protected") == 0) {
        gnu->visibility = GNU_VIS_PROTECTED;
        p->pos++;
    } else if (strcmp((const char *)arg->str.bytes, "internal") == 0) {
        gnu->visibility = GNU_VIS_INTERNAL;
        p->pos++;
    } else {
        parse_error(p, arg,
                    "unknown visibility '%s'; expected \"default\", "
                    "\"hidden\", \"protected\" or \"internal\"",
                    (const char *)arg->str.bytes);
        p->pos++;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

static GnuAttrClass gnu_attr_class(const char *spelling)
{
    /* `__packed__` and `packed` are the same attribute. Headers use the
     * underscored spelling precisely so a macro named `packed` cannot
     * capture it, so both must classify identically. */
    char norm[64];
    size_t n = strlen(spelling);

    if (n > 4 && strncmp(spelling, "__", 2) == 0 &&
        strcmp(spelling + n - 2, "__") == 0) {
        n -= 4;
        if (n >= sizeof(norm))
            return GA_UNKNOWN;
        memcpy(norm, spelling + 2, n);
        norm[n] = '\0';
    } else {
        if (n >= sizeof(norm))
            return GA_UNKNOWN;
        memcpy(norm, spelling, n + 1);
    }

#define GA(name, cls)                                                          \
    if (strcmp(norm, #name) == 0)                                              \
        return cls;
#include "parse/gnu_attrs.def"
#undef GA
    return GA_UNKNOWN;
}

CgfAttr *parse_cgf_attributes(Parser *p, GnuDeclAttrs *gnu)
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
                /* Two attributes are REFUSED rather than deferred: they
                 * would create types the rest of the compiler has no axis
                 * for, so accepting and ignoring either one miscompiles
                 * silently. docs/gnu-extensions.md carries the rationale;
                 * this is the code half of that contract. */
                if (strcmp(name->spelling, "mode") == 0 ||
                    strcmp(name->spelling, "__mode__") == 0)
                    parse_error(p, name,
                                "the 'mode' attribute is not supported: it "
                                "selects a machine mode independent of the C "
                                "type, and this compiler has no such axis "
                                "(docs/gnu-extensions.md)");
                else if (strcmp(name->spelling, "vector_size") == 0 ||
                         strcmp(name->spelling, "__vector_size__") == 0)
                    parse_error(p, name,
                                "the 'vector_size' attribute is not "
                                "supported: it would create vector types "
                                "with no SysV or AAPCS64 parameter contract "
                                "(docs/gnu-extensions.md)");
                else if (strncmp(name->spelling, "cgf_", 4) == 0)
                    parse_error(p, name, "unknown cgf_ attribute '%s'",
                                name->spelling);
                else if (gnu && gnu_attr_is(name->spelling, "weak")) {
                    gnu->weak = true;
                    valid = false;
                } else if (gnu && gnu_attr_is(name->spelling, "visibility")) {
                    parse_visibility(p, name, gnu);
                    valid = false;
                } else {
                    switch (gnu_attr_class(name->spelling)) {
                    case GA_IGNORE:
                    case GA_UNKNOWN:
                        /* gcc's own behaviour and its own flag: accept, and
                         * say so under -Wattributes (on by default, so
                         * -Wno-attributes works as a reader expects). An
                         * unknown attribute is accepted rather than refused
                         * because a compiler that rejects a name it has
                         * never heard of cannot read next year's headers. */
                        warn_at(p->lang->warnings, WARN_ATTRIBUTES, name->span,
                                "'%s' attribute directive ignored",
                                name->spelling);
                        break;
                    case GA_UNSAFE:
                        parse_error(p, name,
                                    "the '%s' attribute is not yet "
                                    "implemented, and ignoring it would "
                                    "change layout, linkage or behaviour "
                                    "rather than just a diagnostic "
                                    "(docs/gnu-extensions.md)",
                                    name->spelling);
                        break;
                    }
                }
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

    (void)saw_non_cgf; /* every non-cgf attribute is classified above */
    return head;
}
