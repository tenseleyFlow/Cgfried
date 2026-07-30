#include <string.h>

#include "pp/pp.h"

/* Macro TABLE only: #define/#undef recorded, lookups answered, definition-
 * time constraints enforced. Expansion LANDS_IN_SPRINT(5). */

const MacroDef *pp_macro_lookup(const Preprocessor *pp, const char *name)
{
    return strmap_get(&pp->macros, name, strlen(name));
}

/* ISO 6.10.3p2: benign redefinition requires identical spelling AND spacing
 * of every body token, and identical parameter spellings. */
static bool bodies_identical(const MacroDef *a, const MacroDef *b)
{
    u32 i;

    if (a->is_function != b->is_function || a->is_variadic != b->is_variadic ||
        a->nparams != b->nparams || a->body_len != b->body_len)
        return false;
    for (i = 0; i < a->nparams; i++)
        if (a->params[i] != b->params[i]) /* interned: pointer compare */
            return false;
    for (i = 0; i < a->body_len; i++) {
        const PpToken *x = &a->body[i], *y = &b->body[i];
        if (x->spelling != y->spelling ||
            (x->flags & PPTOK_F_SPACE) != (y->flags & PPTOK_F_SPACE))
            return false;
    }
    return true;
}

static bool name_is_protected(const char *name)
{
    /* `defined` is never definable. The __LINE__ family becomes builtin
     * macros in Sprint 5; the protection list grows there. */
    return strcmp(name, "defined") == 0;
}

/* Tokens after the `define` keyword: NAME [( params )] body... */
void pp_macro_define_line(Preprocessor *pp, const PpToken *toks, u32 n)
{
    MacroDef *m;
    u32 i = 0;
    const char *params[128];
    u16 nparams = 0;

    if (n == 0 || toks[0].kind != PPTOK_IDENT) {
        pp_diag_at(pp, DIAG_ERROR, n ? toks[0].loc : SRCLOC_INVALID,
                   n ? toks[0].len : 0, "macro name must be an identifier");
        return;
    }
    if (name_is_protected(toks[0].spelling)) {
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "'%s' cannot be used as a macro name", toks[0].spelling);
        return;
    }

    m = arena_alloc(pp->arena, sizeof(MacroDef), _Alignof(MacroDef));
    memset(m, 0, sizeof(*m));
    m->name = toks[0].spelling;
    m->loc = toks[0].loc;
    i = 1;

    /* Function-like iff `(` IMMEDIATELY follows the name — a space makes it
     * object-like with a body starting at `(` (the classic #define f (x)). */
    if (i < n && toks[i].kind == PPTOK_PUNCT && toks[i].punct == PUNCT_LPAREN &&
        !(toks[i].flags & PPTOK_F_SPACE)) {
        bool first = true;
        m->is_function = true;
        i++;
        for (;;) {
            if (i >= n) {
                pp_diag_at(pp, DIAG_ERROR, m->loc, 1,
                           "unterminated macro parameter list");
                return;
            }
            if (toks[i].kind == PPTOK_PUNCT && toks[i].punct == PUNCT_RPAREN) {
                i++;
                break;
            }
            if (!first) {
                if (toks[i].kind != PPTOK_PUNCT ||
                    toks[i].punct != PUNCT_COMMA) {
                    pp_diag_at(pp, DIAG_ERROR, toks[i].loc, toks[i].len,
                               "expected ',' or ')' in macro parameter list");
                    return;
                }
                i++;
                if (i >= n) {
                    pp_diag_at(pp, DIAG_ERROR, m->loc, 1,
                               "unterminated macro parameter list");
                    return;
                }
            }
            first = false;
            if (toks[i].kind == PPTOK_PUNCT &&
                toks[i].punct == PUNCT_ELLIPSIS) {
                m->is_variadic = true;
                i++;
                if (i >= n || toks[i].kind != PPTOK_PUNCT ||
                    toks[i].punct != PUNCT_RPAREN) {
                    pp_diag_at(pp, DIAG_ERROR, toks[i < n ? i : n - 1].loc, 1,
                               "'...' must be the last macro parameter");
                    return;
                }
                i++;
                break;
            }
            if (toks[i].kind != PPTOK_IDENT) {
                pp_diag_at(pp, DIAG_ERROR, toks[i].loc, toks[i].len,
                           "expected parameter name in macro parameter "
                           "list");
                return;
            }
            {
                u16 k;
                for (k = 0; k < nparams; k++) {
                    if (params[k] == toks[i].spelling) {
                        pp_diag_at(pp, DIAG_ERROR, toks[i].loc, toks[i].len,
                                   "duplicate macro parameter '%s'",
                                   toks[i].spelling);
                        return;
                    }
                }
            }
            if (nparams == CGF_ARRAY_LEN(params)) {
                pp_diag_at(pp, DIAG_ERROR, toks[i].loc, toks[i].len,
                           "too many macro parameters (max 128)");
                return;
            }
            params[nparams++] = toks[i].spelling;
            i++;
        }
    }

    m->nparams = nparams;
    if (nparams) {
        m->params = arena_alloc(pp->arena, nparams * sizeof(const char *),
                                _Alignof(const char *));
        memcpy(m->params, params, nparams * sizeof(const char *));
    }

    /* Body: rest verbatim. Definition-time constraints on # and ##. */
    m->body_len = n - i;
    if (m->body_len) {
        m->body = arena_alloc(pp->arena, m->body_len * sizeof(PpToken),
                              _Alignof(PpToken));
        memcpy(m->body, toks + i, m->body_len * sizeof(PpToken));
    }
    {
        u32 b;
        for (b = 0; b < m->body_len; b++) {
            const PpToken *bt = &m->body[b];
            if (bt->kind != PPTOK_PUNCT)
                continue;
            if (bt->punct == PUNCT_HASHHASH &&
                (b == 0 || b == m->body_len - 1)) {
                pp_diag_at(pp, DIAG_ERROR, bt->loc, bt->len,
                           "'##' cannot appear at either end of a macro "
                           "body");
                return;
            }
            if (bt->punct == PUNCT_HASH && m->is_function) {
                /* # must be followed by a parameter (or __VA_ARGS__). */
                bool ok = false;
                if (b + 1 < m->body_len && m->body[b + 1].kind == PPTOK_IDENT) {
                    u16 k;
                    const char *s = m->body[b + 1].spelling;
                    for (k = 0; k < m->nparams; k++)
                        if (m->params[k] == s)
                            ok = true;
                    if (m->is_variadic && strcmp(s, "__VA_ARGS__") == 0)
                        ok = true;
                }
                if (!ok) {
                    pp_diag_at(pp, DIAG_ERROR, bt->loc, bt->len,
                               "'#' is not followed by a macro parameter");
                    return;
                }
            }
        }
    }

    /* Redefinition. */
    {
        MacroDef *prev = strmap_get(&pp->macros, m->name, strlen(m->name));
        if (prev) {
            if (bodies_identical(prev, m))
                return; /* benign */
            pp_diag_at(pp, DIAG_WARNING, m->loc, (u32)strlen(m->name),
                       "'%s' macro redefined", m->name);
            pp_diag_at(pp, DIAG_NOTE, prev->loc, (u32)strlen(m->name),
                       "previous definition is here");
            /* fall through: the new definition wins (gcc parity) */
        }
    }
    strmap_put(&pp->macros, m->name, strlen(m->name), m);
}

void pp_macro_undef(Preprocessor *pp, const char *name, SrcLoc loc)
{
    if (name_is_protected(name)) {
        pp_diag_at(pp, DIAG_ERROR, loc, (u32)strlen(name),
                   "'%s' cannot be used as a macro name", name);
        return;
    }
    /* #undef of an undefined name is silently OK (ISO). */
    strmap_put(&pp->macros, name, strlen(name), NULL);
}

bool pp_expansion_needed(Preprocessor *pp, const PpToken *toks, u32 n,
                         const char *context)
{
    u32 i;

    for (i = 0; i < n; i++) {
        if (toks[i].kind != PPTOK_IDENT)
            continue;
        if (pp_macro_lookup(pp, toks[i].spelling)) {
            pp_diag_at(pp, DIAG_ERROR, toks[i].loc, toks[i].len,
                       "macro expansion of '%s' in %s is not yet "
                       "supported" LANDS_IN_SPRINT(5),
                       toks[i].spelling, context);
            return true;
        }
    }
    return false;
}
