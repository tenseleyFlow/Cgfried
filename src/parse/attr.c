#include <stdlib.h>
#include <string.h>

#include "parse/parse.h"
#include "warn/warn.h"

void gnu_attrs_merge(GnuDeclAttrs *dst, const GnuDeclAttrs *src)
{
    if (!src)
        return;
    dst->weak |= src->weak;
    dst->packed |= src->packed;
    if (src->aligned_expr || src->aligned_bare) {
        if (dst->aligned_expr || dst->aligned_bare)
            dst->aligned_conflict = true;
        dst->aligned_expr = src->aligned_expr;
        dst->aligned_bare = src->aligned_bare;
    }
    dst->aligned_conflict |= src->aligned_conflict;
    if (src->alias_target)
        dst->alias_target = src->alias_target;
    dst->used |= src->used;
    /* Union, priority included. gcc drops the priority when the attribute
     * arrives on a definition whose earlier declaration had none; keeping it
     * is the deliberate divergence recorded in attr.h. */
    dst->constructor |= src->constructor;
    dst->destructor |= src->destructor;
    if (src->ctor_priority)
        dst->ctor_priority = src->ctor_priority;
    if (src->dtor_priority)
        dst->dtor_priority = src->dtor_priority;
    /* LAST WINS, not union, and the difference is observable: gcc runs only
     * the last `cleanup` named on a declaration and says nothing about the
     * others (measured by execution). See attr.h. */
    if (src->cleanup_fn)
        dst->cleanup_fn = src->cleanup_fn;
    if (src->asm_name)
        dst->asm_name = src->asm_name;
    if (src->section_name)
        dst->section_name = src->section_name;
    /* Union, and the MESSAGE survives independently: gcc takes the reason
     * from whichever declaration supplied one. */
    dst->deprecated |= src->deprecated;
    dst->warn_unused_result |= src->warn_unused_result;
    dst->noreturn |= src->noreturn;
    dst->nonnull_all |= src->nonnull_all;
    dst->nonnull_mask |= src->nonnull_mask;
    if (src->has_format) {
        dst->has_format = true;
        dst->fmt_family = src->fmt_family;
        dst->fmt_arg = src->fmt_arg;
        dst->fmt_first_vararg = src->fmt_first_vararg;
    }
    if (src->deprecated_msg)
        dst->deprecated_msg = src->deprecated_msg;
    /* Last visibility wins, which is gcc's rule; an unspecified one never
     * clears a specified one. */
    if (src->visibility)
        dst->visibility = src->visibility;
    /* Last mode wins, like visibility: gcc takes the last of two `mode`
     * attributes on one declaration rather than erroring. */
    if (src->mode)
        dst->mode = src->mode;
}

/* Did this declaration say anything that is a SYMBOL property? Callers with
 * nowhere to put one — a function parameter is the only such position — use
 * this to say so rather than dropping it silently.
 *
 * It lives beside gnu_attrs_merge deliberately: both must enumerate every
 * field, so a new attribute that forgets one forgets the other in the same
 * glance. Enumerating only the three fields that existed when the parameter
 * path was written is exactly how `aligned`, `alias`, `used`, `section`,
 * `constructor`, `destructor` and the asm label all came to be dropped there
 * without a word. */
bool gnu_attrs_any_symbol_property(const GnuDeclAttrs *g)
{
    return g->weak || g->packed || g->visibility || g->used ||
           g->aligned_expr || g->aligned_bare || g->constructor ||
           g->destructor || g->alias_target || g->asm_name || g->section_name ||
           g->cleanup_fn || g->deprecated || g->warn_unused_result ||
           g->has_format || g->nonnull_all || g->nonnull_mask || g->noreturn;
}

bool gnu_attrs_any_type_property(const GnuDeclAttrs *g)
{
    return g->mode != GNU_MODE_NONE;
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
typedef enum { GA_IMPLEMENTED, GA_IGNORE, GA_UNSAFE, GA_UNKNOWN } GnuAttrClass;

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

/* `aligned` or `aligned(N)`. The argument is a constant EXPRESSION, not a
 * literal -- `aligned(sizeof(long))` and `aligned(2 * BLK)` both appear in real
 * headers -- so it is recorded and folded in sema by the evaluator `_Alignas`
 * already uses. The bare form is the target's biggest alignment, resolved in
 * sema for the same reason every other target fact is: the parser is not the
 * place that knows. */
static void parse_aligned(Parser *p, GnuDeclAttrs *gnu)
{
    if (!parse_at_punct(p, PUNCT_LPAREN)) {
        gnu->aligned_bare = true;
        if (gnu->aligned_expr)
            gnu->aligned_conflict = true;
        gnu->aligned_expr = NULL;
        return;
    }
    p->pos++; /* '(' */
    if (gnu->aligned_expr || gnu->aligned_bare)
        gnu->aligned_conflict = true;
    gnu->aligned_bare = false;
    gnu->aligned_expr = parse_cond_expr(p);
    if (!parse_eat_punct(p, PUNCT_RPAREN)) {
        parse_error(p, parse_peek(p),
                    "expected ')' after the 'aligned' argument");
        while (!parse_at_punct(p, PUNCT_RPAREN) &&
               parse_peek(p)->kind != TOK_EOF)
            p->pos++;
        parse_eat_punct(p, PUNCT_RPAREN);
    }
}

/* `constructor` / `destructor`, each with an optional priority. The argument
 * is a constant EXPRESSION for the same reason `aligned`'s is: gcc folds
 * `constructor(sizeof(long) * 20)` to 160, and a literal-only parser would
 * reject real code. Recorded here, folded and range-checked in sema. */
static void parse_ctor_dtor(Parser *p, bool is_ctor, GnuDeclAttrs *gnu)
{
    AstNode **slot = is_ctor ? &gnu->ctor_priority : &gnu->dtor_priority;

    if (is_ctor)
        gnu->constructor = true;
    else
        gnu->destructor = true;

    if (!parse_at_punct(p, PUNCT_LPAREN))
        return; /* the bare form: default priority */
    p->pos++;   /* '(' */
    *slot = parse_cond_expr(p);
    if (!parse_eat_punct(p, PUNCT_RPAREN)) {
        parse_error(p, parse_peek(p), "expected ')' after the '%s' priority",
                    is_ctor ? "constructor" : "destructor");
        while (!parse_at_punct(p, PUNCT_RPAREN) &&
               parse_peek(p)->kind != TOK_EOF)
            p->pos++;
        parse_eat_punct(p, PUNCT_RPAREN);
    }
}

/* alias("target"). The argument is a STRING holding a symbol name. Token
 * spellings arrive pre-interned but a string literal's BYTES do not, and the
 * parser holds no interner, so this is arena-owned here and interned by sema
 * at the one place that needs pointer identity: the symbol lookup. */
static void parse_alias_attr(Parser *p, const Token *name, GnuDeclAttrs *gnu)
{
    const Token *arg;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, name, "attribute 'alias' requires a target name");
        return;
    }
    arg = parse_peek(p);
    if (arg->kind != TOK_STRING || !arg->str.bytes) {
        parse_error(p, arg,
                    "'alias' takes a string argument naming the "
                    "symbol to alias");
    } else {
        gnu->alias_target =
            arena_strdup(p->arena, (const char *)arg->str.bytes);
        p->pos++;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

/* cleanup(func). The argument is strictly an IDENTIFIER — not an expression,
 * not a string. gcc has a dedicated diagnostic for each way of getting that
 * wrong, and the two are genuinely different checks: `cleanup(&f)` is
 * "argument not an identifier" (a grammar failure, caught here), while
 * `cleanup(fp)` naming a function POINTER is "argument not a function" (a
 * lookup failure, caught in sema once the name resolves).
 *
 * The spelling is interned already — identifier tokens arrive that way — so
 * unlike `alias` and `section` this needs no arena copy. */
static void parse_cleanup_attr(Parser *p, const Token *name, GnuDeclAttrs *gnu)
{
    const Token *arg;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, name, "attribute 'cleanup' requires a function name");
        return;
    }
    arg = parse_peek(p);
    if (arg->kind != TOK_IDENT) {
        parse_error(p, arg, "cleanup argument not an identifier");
    } else {
        gnu->cleanup_fn = arg->spelling;
        p->pos++;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

/* section("name"). One string, taken verbatim: a section name is not an
 * identifier and `.note.GNU-stack` style names are ordinary. */
static void parse_section_attr(Parser *p, const Token *name, GnuDeclAttrs *gnu)
{
    const Token *arg;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, name, "attribute 'section' requires a section name");
        return;
    }
    arg = parse_peek(p);
    if (arg->kind != TOK_STRING || !arg->str.bytes) {
        parse_error(p, arg, "'section' takes a string naming the section");
    } else {
        gnu->section_name =
            arena_strdup(p->arena, (const char *)arg->str.bytes);
        p->pos++;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

/* deprecated, or deprecated("why"). The argument is OPTIONAL, which is what
 * separates this from `section`: a bare `deprecated` is the common spelling
 * and must not be an error. An empty string is still the with-message form
 * -- gcc prints a trailing `: ` for it -- so presence of the token decides,
 * not the length of the bytes. */
static void parse_deprecated_attr(Parser *p, GnuDeclAttrs *gnu)
{
    const Token *arg;

    gnu->deprecated = true;
    if (!parse_at_punct(p, PUNCT_LPAREN))
        return; /* the bare form */
    p->pos++;
    arg = parse_peek(p);
    if (arg->kind == TOK_STRING && arg->str.bytes) {
        gnu->deprecated_msg =
            arena_strdup(p->arena, (const char *)arg->str.bytes);
        p->pos++;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

/* `__packed__` and `packed` are the same attribute. Headers use the
 * underscored spelling precisely so a macro named `packed` cannot capture
 * it, so both must normalize identically -- and so must a format ARCHETYPE,
 * which headers spell `__printf__` for exactly the same reason. Returns a
 * pointer into `buf`, or the original spelling when it does not fit. */
static const char *gnu_attr_norm_name(const char *spelling, char *buf,
                                      size_t bufsz)
{
    size_t n = strlen(spelling);

    if (n > 4 && strncmp(spelling, "__", 2) == 0 &&
        strcmp(spelling + n - 2, "__") == 0) {
        n -= 4;
        if (n >= bufsz)
            return spelling;
        memcpy(buf, spelling + 2, n);
        buf[n] = '\0';
        return buf;
    }
    return spelling;
}

GnuMode gnu_mode_from_name(const char *spelling)
{
    static const struct {
        const char *name;
        u8 mode;
    } modes[] = {{"QI", GNU_MODE_QI},          {"HI", GNU_MODE_HI},
                 {"SI", GNU_MODE_SI},          {"DI", GNU_MODE_DI},
                 {"byte", GNU_MODE_BYTE},      {"word", GNU_MODE_WORD},
                 {"pointer", GNU_MODE_POINTER}};
    char buf[32];
    const char *norm = gnu_attr_norm_name(spelling, buf, sizeof(buf));
    size_t i;

    for (i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
        if (strcmp(modes[i].name, norm) == 0)
            return (GnuMode)modes[i].mode;
    return GNU_MODE_NONE;
}

/* mode(M) / __mode__(__M__).
 *
 * Only the integer modes land. TI (128-bit), the floating modes SF/DF/XF/TF
 * and the vector modes each name a type this compiler does not have, and
 * gcc accepts all of them -- so each is refused BY NAME rather than
 * silently ignored, which would give the declaration a type of the wrong
 * size with no diagnostic. An unknown name gets gcc's own wording. */
static void parse_mode_attr(Parser *p, const Token *name, GnuDeclAttrs *gnu)
{
    const Token *arg;
    GnuMode m;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, name, "expected '(' after 'mode'");
        return;
    }
    arg = parse_peek(p);
    if (arg->kind != TOK_IDENT && arg->kind != TOK_KEYWORD) {
        parse_error(p, arg, "expected a machine mode name");
        while (!parse_at_punct(p, PUNCT_RPAREN) &&
               parse_peek(p)->kind != TOK_EOF)
            p->pos++;
        parse_eat_punct(p, PUNCT_RPAREN);
        return;
    }
    p->pos++;
    m = gnu_mode_from_name(arg->spelling);
    if (m == GNU_MODE_NONE) {
        char buf[32];
        const char *norm = gnu_attr_norm_name(arg->spelling, buf, sizeof(buf));

        /* The modes gcc knows and we do not, separated from a typo: naming
         * the missing feature is more useful than "unknown", and a typo
         * should not read as an unimplemented type. */
        if (strcmp(norm, "TI") == 0)
            parse_error(p, arg,
                        "only integer machine modes are supported: mode "
                        "'%s' names a 128-bit integer type, which this "
                        "compiler does not have (docs/gnu-extensions.md)",
                        norm);
        else if (strcmp(norm, "SF") == 0 || strcmp(norm, "DF") == 0 ||
                 strcmp(norm, "XF") == 0 || strcmp(norm, "TF") == 0)
            parse_error(p, arg,
                        "only integer machine modes are supported: mode "
                        "'%s' names a floating type, and selecting one by "
                        "width would silently disagree with the target's "
                        "float/double/long double (docs/gnu-extensions.md)",
                        norm);
        else if (norm[0] == 'V')
            parse_error(p, arg,
                        "only integer machine modes are supported: mode "
                        "'%s' names a vector type, which has no SysV or "
                        "AAPCS64 parameter contract here "
                        "(docs/gnu-extensions.md)",
                        norm);
        else
            parse_error(p, arg, "unknown machine mode '%s'", arg->spelling);
    } else {
        gnu->mode = (u8)m;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

/* nonnull, or nonnull(1,2,...). The bare form means EVERY pointer
 * parameter, which is why it gets its own flag rather than an empty mask. */
static void parse_nonnull_attr(Parser *p, GnuDeclAttrs *gnu)
{
    if (!parse_at_punct(p, PUNCT_LPAREN)) {
        gnu->nonnull_all = true;
        return;
    }
    p->pos++;
    for (;;) {
        const Token *n = parse_peek(p);
        long v;

        if (n->kind != TOK_INT_CONST)
            break;
        v = strtol(n->spelling, NULL, 0);
        if (v >= 1 && v <= 64)
            gnu->nonnull_mask |= 1ull << (v - 1);
        p->pos++;
        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

/* format(archetype, string-index, first-to-check).
 *
 * The archetype is an IDENTIFIER, and headers spell it three ways for the
 * same thing -- `printf`, `__printf__` (so a macro named printf cannot
 * capture it) and `gnu_printf` (to pick glibc's dialect over MS's). All
 * three normalize to one family here; we implement one dialect, so
 * distinguishing gnu_ from ms_ would be a promise we do not keep.
 *
 * Both indices are 1-BASED, matching gcc's diagnostics, and a
 * first-to-check of 0 is the va_list form rather than an error. */
static void parse_format_attr(Parser *p, const Token *name, GnuDeclAttrs *gnu)
{
    const Token *arch;
    const char *a;
    char abuf[64];
    long idx[2] = {0, 0};
    int got = 0;

    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, name, "attribute 'format' requires three arguments");
        return;
    }
    arch = parse_peek(p);
    if (arch->kind != TOK_IDENT) {
        parse_error(p, arch, "'format' archetype must be an identifier");
    } else {
        a = gnu_attr_norm_name(arch->spelling, abuf, sizeof(abuf));
        p->pos++;
        if (strcmp(a, "printf") == 0 || strcmp(a, "gnu_printf") == 0)
            gnu->fmt_family = 0; /* FMT_PRINTF */
        else if (strcmp(a, "scanf") == 0 || strcmp(a, "gnu_scanf") == 0)
            gnu->fmt_family = 1; /* FMT_SCANF */
        else if (strcmp(a, "strftime") == 0 || strcmp(a, "gnu_strftime") == 0)
            gnu->fmt_family = 2; /* FMT_STRFTIME */
        else if (strcmp(a, "strfmon") == 0 || strcmp(a, "gnu_strfmon") == 0)
            gnu->fmt_family = 3; /* FMT_STRFMON */
        else {
            parse_error(p, arch, "unrecognized format archetype '%s'",
                        arch->spelling);
            while (!parse_at_punct(p, PUNCT_RPAREN) &&
                   parse_peek(p)->kind != TOK_EOF)
                p->pos++;
            parse_eat_punct(p, PUNCT_RPAREN);
            return;
        }
        while (got < 2 && parse_eat_punct(p, PUNCT_COMMA)) {
            const Token *n = parse_peek(p);

            if (n->kind != TOK_INT_CONST)
                break;
            idx[got++] = strtol(n->spelling, NULL, 0);
            p->pos++;
        }
        if (got == 2 && idx[0] >= 1 && idx[0] <= 255 && idx[1] >= 0 &&
            idx[1] <= 255) {
            gnu->has_format = true;
            gnu->fmt_arg = (u8)idx[0];
            gnu->fmt_first_vararg = (u8)idx[1];
        } else {
            parse_error(p, name,
                        "'format' takes an archetype and two argument "
                        "positions");
        }
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_eat_punct(p, PUNCT_RPAREN);
}

static GnuAttrClass gnu_attr_class(const char *spelling)
{
    char nbuf[64];
    const char *norm = gnu_attr_norm_name(spelling, nbuf, sizeof(nbuf));

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
                if (strcmp(name->spelling, "vector_size") == 0 ||
                    strcmp(name->spelling, "__vector_size__") == 0)
                    parse_error(p, name,
                                "the 'vector_size' attribute is not "
                                "supported: it would create vector types "
                                "with no SysV or AAPCS64 parameter contract "
                                "(docs/gnu-extensions.md)");
                else if (strncmp(name->spelling, "cgf_", 4) == 0)
                    parse_error(p, name, "unknown cgf_ attribute '%s'",
                                name->spelling);
                else {
                    switch (gnu_attr_class(name->spelling)) {
                    case GA_IMPLEMENTED:
                        /* `gnu` is NULL wherever the caller has nowhere to
                         * put a symbol property -- a parameter, say. gcc
                         * warns there rather than erroring, so fall through
                         * to the ignored path and say so. */
                        if (gnu && gnu_attr_is(name->spelling, "weak")) {
                            gnu->weak = true;
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "visibility")) {
                            parse_visibility(p, name, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "packed")) {
                            gnu->packed = true;
                            break;
                        }
                        if (gnu_attr_is(name->spelling, "mode")) {
                            /* Deliberately NOT gated on `gnu`: mode is a
                             * type property, so the parameter position that
                             * passes a scratch sink must still see it and
                             * reject it. */
                            parse_mode_attr(p, name, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "aligned")) {
                            parse_aligned(p, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "alias")) {
                            parse_alias_attr(p, name, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "used")) {
                            gnu->used = true;
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "section")) {
                            parse_section_attr(p, name, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "constructor")) {
                            parse_ctor_dtor(p, true, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "destructor")) {
                            parse_ctor_dtor(p, false, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "cleanup")) {
                            parse_cleanup_attr(p, name, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "deprecated")) {
                            parse_deprecated_attr(p, gnu);
                            break;
                        }
                        if (gnu &&
                            gnu_attr_is(name->spelling, "warn_unused_result")) {
                            gnu->warn_unused_result = true;
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "format")) {
                            parse_format_attr(p, name, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "nonnull")) {
                            parse_nonnull_attr(p, gnu);
                            break;
                        }
                        if (gnu && gnu_attr_is(name->spelling, "noreturn")) {
                            gnu->noreturn = true;
                            break;
                        }
                        warn_at(p->lang->warnings, WARN_ATTRIBUTES, name->span,
                                "'%s' attribute ignored", name->spelling);
                        break;
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
