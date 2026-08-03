#include <stdio.h>
#include <string.h>
#include <time.h>

#include "driver/toolchain.h"
#include "pp/pp.h"
#include "target.h"

/* The macro engine: table + Prosser expansion (hide-sets, argument
 * pre-expansion, # and ## with placemarkers, rescan). Reference: Prosser's
 * algorithm as followed by Boost.Wave and clang. */

/* Marks a ## that came from a macro BODY (a real paste operator). A ##
 * arriving through an argument is an ordinary token and must not paste. */
#define PPTOK_F_PASTEOP 0x80

/* --- hide-sets ---------------------------------------------------------- */

HideSet *pp_hs_insert(Arena *a, HideSet *hs, const char *name)
{
    HideSet *n = arena_alloc(a, sizeof(HideSet), _Alignof(HideSet));

    n->name = name;
    n->next = hs;
    return n;
}

bool pp_hs_contains(const HideSet *hs, const char *name)
{
    for (; hs; hs = hs->next)
        if (hs->name == name) /* interned: pointer compare */
            return true;
    return false;
}

HideSet *pp_hs_intersect(Arena *a, HideSet *x, HideSet *y)
{
    HideSet *r = NULL;

    for (; x; x = x->next)
        if (pp_hs_contains(y, x->name))
            r = pp_hs_insert(a, r, x->name);
    return r;
}

/* --- table (Sprint 4, plus builtin handling) ---------------------------- */

const MacroDef *pp_macro_lookup(const Preprocessor *pp, const char *name)
{
    return strmap_get(&pp->macros, name, strlen(name));
}

const MacroDef *pp_macro_lookup_at_seq(const Preprocessor *pp, const char *name,
                                       u32 seq)
{
    const MacroDef *active = NULL;
    size_t i;

    if (!pp || !name || !seq)
        return NULL;
    for (i = 0; i < pp->nmacro_events; i++) {
        const PpMacroEvent *event = &pp->macro_events[i];

        if (event->seq && event->seq < seq && strcmp(event->name, name) == 0)
            active = event->definition;
    }
    return active;
}

static void macro_event_append(Preprocessor *pp, const char *name,
                               const MacroDef *definition, SrcLoc loc)
{
    PpMacroEvent *event;

    if (pp->nmacro_events == pp->macro_events_cap) {
        size_t new_cap = pp->macro_events_cap ? pp->macro_events_cap * 2 : 32;
        PpMacroEvent *grown = arena_alloc(pp->arena, new_cap * sizeof(*grown),
                                          _Alignof(PpMacroEvent));

        if (pp->nmacro_events)
            memcpy(grown, pp->macro_events, pp->nmacro_events * sizeof(*grown));
        pp->macro_events = grown;
        pp->macro_events_cap = new_cap;
    }
    event = &pp->macro_events[pp->nmacro_events++];
    event->name = name;
    event->definition = definition;
    event->seq = pp_loc_mark(&pp->loc, loc);
}

/* `defined(_Pragma)` and `#ifdef _Pragma` answer TRUE in both gcc and
 * clang — the operator is registered as a macro-like name for that
 * purpose even though it never expands like one. Verified against both
 * oracles (findings row F20); found by ppfuzz seed 1837. */
bool pp_name_is_defined(const Preprocessor *pp, const char *name)
{
    return pp_macro_lookup(pp, name) != NULL || strcmp(name, "_Pragma") == 0;
}

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
    return strcmp(name, "defined") == 0;
}

static bool loc_is_builtin_file(Preprocessor *pp, SrcLoc loc)
{
    FileId f;
    u32 line, col;

    if (loc == SRCLOC_INVALID)
        return false;
    pp_loc_resolve(&pp->loc, loc, &f, &line, &col);
    return f && (size_t)f <= pp->nfiles &&
           strcmp(pp->files[f - 1]->path, "<built-in>") == 0;
}

/* Is body[i] (an IDENT) a parameter? Returns index, nparams for
 * __VA_ARGS__ in a variadic, or -1. */
static int find_param(const MacroDef *m, const PpToken *t)
{
    u16 k;

    if (t->kind != PPTOK_IDENT)
        return -1;
    for (k = 0; k < m->nparams; k++)
        if (m->params[k] == t->spelling)
            return (int)k;
    if (m->is_variadic && strcmp(t->spelling, "__VA_ARGS__") == 0)
        return (int)m->nparams;
    return -1;
}

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
    /* Directive operands do not flow through raw_next(), so stamp the
     * definition at the moment it becomes visible.  Besides preserving a
     * total lexical order for diagnostics, this lets source-edit clients
     * prove that an annotation macro predates the prototype they edit. */
    pp_loc_mark(&pp->loc, m->loc);
    m->is_builtin = loc_is_builtin_file(pp, toks[0].loc);
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
                if (toks[i].kind == PPTOK_PUNCT &&
                    toks[i].punct == PUNCT_ELLIPSIS && i > 0 &&
                    toks[i - 1].kind == PPTOK_IDENT) {
                    /* `#define M(a, rest...)`: GNU named variadics. */
                    pp_diag_at(pp, DIAG_ERROR, toks[i].loc, toks[i].len,
                               "GNU named variadic macro parameters are not "
                               "yet supported" LANDS_IN_SPRINT(55));
                    return;
                }
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
            if (bt->kind == PPTOK_IDENT && !m->is_variadic &&
                strcmp(bt->spelling, "__VA_ARGS__") == 0) {
                pp_diag_at(pp, DIAG_ERROR, bt->loc, bt->len,
                           "__VA_ARGS__ is only valid in a variadic macro");
                return;
            }
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
                bool ok =
                    b + 1 < m->body_len && find_param(m, &m->body[b + 1]) >= 0;
                if (!ok) {
                    pp_diag_at(pp, DIAG_ERROR, bt->loc, bt->len,
                               "'#' is not followed by a macro parameter");
                    return;
                }
            }
        }
    }

    {
        MacroDef *prev = strmap_get(&pp->macros, m->name, strlen(m->name));
        if (prev) {
            if (prev->is_builtin && !m->is_builtin) {
                pp_warn_at(pp, WARN_BUILTIN_MACRO_REDEFINED, m->loc,
                           (u32)strlen(m->name),
                           "redefining builtin macro '%s'", m->name);
            } else if (bodies_identical(prev, m)) {
                return; /* benign */
            } else {
                pp_warn_at(pp, WARN_MACRO_REDEFINED, m->loc,
                           (u32)strlen(m->name), "'%s' macro redefined",
                           m->name);
                pp_diag_at(pp, DIAG_NOTE, prev->loc, (u32)strlen(m->name),
                           "previous definition is here");
            }
        }
    }
    strmap_put(&pp->macros, m->name, strlen(m->name), m);
    macro_event_append(pp, m->name, m, m->loc);
}

void pp_macro_undef(Preprocessor *pp, const char *name, SrcLoc loc)
{
    if (name_is_protected(name)) {
        pp_diag_at(pp, DIAG_ERROR, loc, (u32)strlen(name),
                   "'%s' cannot be used as a macro name", name);
        return;
    }
    {
        const MacroDef *prev = pp_macro_lookup(pp, name);
        if (prev && prev->is_builtin && !loc_is_builtin_file(pp, loc))
            pp_warn_at(pp, WARN_BUILTIN_MACRO_REDEFINED, loc, (u32)strlen(name),
                       "undefining builtin macro '%s'", name);
    }
    strmap_put(&pp->macros, name, strlen(name), NULL);
    macro_event_append(pp, name, NULL, loc);
}

/* --- expansion helpers -------------------------------------------------- */

static PpToken make_tok(Preprocessor *pp, PpTokKind kind, const char *spell,
                        SrcLoc loc, u8 flags)
{
    PpToken t;
    u32 id = intern(pp->interner, spell, strlen(spell));

    memset(&t, 0, sizeof(t));
    t.kind = (u8)kind;
    t.spelling = intern_str(pp->interner, id);
    t.len = (u32)strlen(spell);
    t.loc = loc;
    t.flags = flags;
    return t;
}

/* Wraps a token with an expansion location (spelled_at = its current loc,
 * expanded_from = the invocation site) and strips BOL: expansion output
 * never begins a directive line. */
static PpToken wrap_tok(Preprocessor *pp, PpToken t, SrcLoc inv,
                        const MacroDef *m)
{
    if (t.loc != SRCLOC_INVALID && inv != SRCLOC_INVALID)
        t.loc = pp_loc_expansion(&pp->loc, t.loc, inv, m ? m->name : NULL,
                                 m ? m->loc : SRCLOC_INVALID);
    t.flags &= (u8)~PPTOK_F_BOL;
    return t;
}

PpToken pp_builtin_token(Preprocessor *pp, const MacroDef *m, SrcLoc loc)
{
    char buf[4160];
    FileId f = 0;
    u32 line = 0, col = 0;
    const char *path = "<unknown>";
    u32 shown;

    /* __LINE__/__FILE__ report the INVOCATION point (gcc parity): walk
     * expansion parents to the outermost use site, not the spelling site
     * inside some macro body. */
    while (loc != SRCLOC_INVALID && pp_loc_is_expansion(&pp->loc, loc))
        loc = pp_loc_expansion_parent(&pp->loc, loc);
    if (loc != SRCLOC_INVALID)
        pp_loc_resolve(&pp->loc, loc, &f, &line, &col);
    shown = line;
    if (f && (size_t)f <= pp->nfiles) {
        const SourceFile *sf = pp->files[f - 1];
        u32 lo = 0, hi = sf->nremaps;
        path = sf->path;
        while (hi > lo) {
            u32 mid = lo + (hi - lo) / 2;
            if (sf->remaps[mid].from_line <= line)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo > 0) {
            const PresumedRemap *r = &sf->remaps[lo - 1];
            shown = r->presumed_line + (line - r->from_line);
            if (r->path)
                path = r->path;
        }
    }

    switch ((MacroBuiltinKind)m->builtin_kind) {
    case MACRO_BUILTIN_LINE:
        snprintf(buf, sizeof(buf), "%u", (unsigned)shown);
        return make_tok(pp, PPTOK_PPNUM, buf, loc, 0);
    case MACRO_BUILTIN_FILE: {
        /* Escape \ and " in the path for the string literal. */
        size_t o = 0;
        const char *p;
        buf[o++] = '"';
        for (p = path; *p && o + 3 < sizeof(buf); p++) {
            if (*p == '\\' || *p == '"')
                buf[o++] = '\\';
            buf[o++] = *p;
        }
        buf[o++] = '"';
        buf[o] = '\0';
        return make_tok(pp, PPTOK_STRLIT, buf, loc, 0);
    }
    case MACRO_BUILTIN_COUNTER:
        snprintf(buf, sizeof(buf), "%u", (unsigned)pp->counter++);
        return make_tok(pp, PPTOK_PPNUM, buf, loc, 0);
    case MACRO_BUILTIN_NONE:
        break;
    }
    CGF_ICE("pp_builtin_token: bad builtin kind %d", (int)m->builtin_kind);
}

/* # stringize: ORIGINAL spelling of the raw argument tokens; one space
 * exactly where any whitespace was (PPTOK_F_SPACE); \ and " escaped inside
 * string/char-literal spellings and around their quotes. */
static PpToken stringize(Preprocessor *pp, const PpToken *raw, u32 n,
                         SrcLoc loc)
{
    Buf b;
    PpToken r;
    u32 i;

    buf_init(&b);
    buf_push_u8(&b, '"');
    for (i = 0; i < n; i++) {
        if (i > 0 && (raw[i].flags & PPTOK_F_SPACE))
            buf_push_u8(&b, ' ');
        if (raw[i].kind == PPTOK_STRLIT || raw[i].kind == PPTOK_CHARCONST) {
            const char *s = raw[i].spelling;
            u32 k;
            for (k = 0; k < raw[i].len; k++) {
                if (s[k] == '\\' || s[k] == '"')
                    buf_push_u8(&b, '\\');
                buf_push_u8(&b, (u8)s[k]);
            }
        } else {
            buf_append(&b, raw[i].spelling, raw[i].len);
        }
    }
    buf_push_u8(&b, '"');
    {
        char *spell = arena_strndup(pp->arena, (const char *)b.data, b.len);
        buf_free(&b);
        r = make_tok(pp, PPTOK_STRLIT, spell, loc, 0);
    }
    return r;
}

/* Silent re-lex of a pasted spelling: valid iff it yields exactly ONE
 * pp-token consuming the whole spelling. Diagnostics are suppressed (the
 * caller reports the paste error with better context). */
static void null_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static bool relex_one(Preprocessor *pp, const char *spell, PpToken *out)
{
    SourceFile tmp;
    PpLexer lx;
    PpToken t, extra;
    u32 zero = 0;
    bool one;

    memset(&tmp, 0, sizeof(tmp));
    tmp.id = 0; /* loc entries created during relex resolve to file 0 and
                   are never used: the caller assigns the real loc */
    tmp.path = "<paste>";
    tmp.contents = (char *)spell;
    tmp.size = (u32)strlen(spell);
    tmp.line_offsets = &zero;
    tmp.nlines = 1;

    {
        DiagSink quiet = {null_sink, NULL};
        DiagSink prev = diag_swap_sink(pp->diag, quiet);
        pp_lexer_init(&lx, pp, &tmp);
        one = pp_lex_token(&lx, &t) && t.len == tmp.size &&
              t.kind != PPTOK_OTHER && !lx.had_error &&
              !pp_lex_token(&lx, &extra);
        buf_free(&lx.scratch);
        diag_swap_sink(pp->diag, prev);
    }
    if (one)
        *out = t;
    return one;
}

/* Pastes left##right (C11 6.10.3.3): placemarker rules first, then the
 * re-lex validity check. Result tokens are NOT painted unless their
 * sources were (they remain expansion candidates). */
static bool paste(Preprocessor *pp, const PpToken *l, const PpToken *r,
                  SrcLoc oploc, PpToken *out)
{
    if (l->kind == PPTOK_PLACEMARKER && r->kind == PPTOK_PLACEMARKER) {
        *out = *l;
        return true;
    }
    if (l->kind == PPTOK_PLACEMARKER) {
        *out = *r;
        return true;
    }
    if (r->kind == PPTOK_PLACEMARKER) {
        *out = *l;
        return true;
    }
    {
        size_t ll = l->len, rl = r->len;
        char *spell = arena_alloc(pp->arena, ll + rl + 1, 1);
        PpToken merged;

        memcpy(spell, l->spelling, ll);
        memcpy(spell + ll, r->spelling, rl);
        spell[ll + rl] = '\0';
        if (!relex_one(pp, spell, &merged)) {
            pp_diag_at(pp, DIAG_ERROR, oploc, 2,
                       "pasting \"%s\" and \"%s\" does not give a valid "
                       "preprocessing token",
                       l->spelling, r->spelling);
            return false;
        }
        merged.loc = oploc;
        merged.flags = l->flags & (u8)~PPTOK_F_PASTEOP;
        merged.hideset = pp_hs_intersect(pp->arena, l->hideset, r->hideset);
        *out = merged;
        return true;
    }
}

/* --- argument machinery ------------------------------------------------- */

VEC_DECL(PpTokVec, PpToken);

bool pp_macro_check_args(Preprocessor *pp, const MacroDef *m, u32 nargs,
                         SrcLoc loc)
{
    u32 want = m->nparams;

    if (m->is_variadic) {
        if (nargs < want) {
            /* C17: at least one argument for ... — but empty is accepted
             * everywhere with a pedwarn (gcc parity; hook Sprint 37). */
            pp_diag_at(pp, DIAG_ERROR, loc, 1,
                       "macro '%s' requires at least %u argument(s), got "
                       "%u",
                       m->name, (unsigned)want, (unsigned)nargs);
            return false;
        }
        return true;
    }
    if (want == 0) {
        /* Z() is zero args (one empty "argument" tolerated). */
        return nargs <= 1;
    }
    if (nargs != want) {
        pp_diag_at(pp, DIAG_ERROR, loc, 1,
                   "macro '%s' expects %u argument(s), got %u", m->name,
                   (unsigned)want, (unsigned)nargs);
        return false;
    }
    return true;
}

/* Lazily pre-expands an argument (isolated: 6.10.3.1 "as if it formed the
 * rest of the file" minus the following tokens). */
static void arg_expanded(Preprocessor *pp, MacroArg *a)
{
    if (a->computed)
        return;
    a->computed = true;
    a->nexp = pp_expand_list(pp, a->raw, a->nraw, &a->expanded);
}

/* --- substitution (the heart) ------------------------------------------ */

PpToken *pp_macro_subst(Preprocessor *pp, const MacroDef *m, MacroArg *args,
                        u32 nargs, HideSet *hs_new, SrcLoc inv, u32 *out_n)
{
    PpTokVec out = {NULL, 0, 0};
    u32 i;

    (void)nargs;

    /* Pass A: parameter substitution, stringize, placemarkers; body-origin
     * ## marked as paste operators. */
    for (i = 0; i < m->body_len; i++) {
        const PpToken *bt = &m->body[i];
        int pi;

        if (m->is_function && bt->kind == PPTOK_PUNCT &&
            bt->punct == PUNCT_HASH && i + 1 < m->body_len &&
            (pi = find_param(m, &m->body[i + 1])) >= 0) {
            MacroArg *a = &args[pi];
            PpToken s =
                stringize(pp, a->raw, a->nraw, wrap_tok(pp, *bt, inv, m).loc);
            s.flags |= bt->flags & PPTOK_F_SPACE;
            PpTokVec_push(&out, s);
            i++; /* skip the parameter */
            continue;
        }
        if (bt->kind == PPTOK_PUNCT && bt->punct == PUNCT_HASHHASH) {
            PpToken op = wrap_tok(pp, *bt, inv, m);

            /* GNU , ## __VA_ARGS__: gcc applies this in ALL modes (the
             * ISO-mode difference is only a -Wpedantic pedwarn — verified
             * against gcc -std=c17; the hook lands with Sprint 37). Empty
             * varargs delete the comma; non-empty keep it and substitute
             * the RAW varargs with NO pasting. Full matrix: Sprint 55. */
            if (i + 1 < m->body_len && out.len > 0 &&
                out.data[out.len - 1].kind == PPTOK_PUNCT &&
                out.data[out.len - 1].punct == PUNCT_COMMA && m->is_variadic &&
                find_param(m, &m->body[i + 1]) == (int)m->nparams) {
                MacroArg *va = &args[m->nparams];
                if (va->nraw == 0) {
                    out.len--; /* delete the comma */
                } else {
                    /* Raw varargs keep their ORIGINAL spacing (gcc emits
                     * "g(2, 3)" for LOG2(2, 3) — argument flags win). */
                    u32 k;
                    for (k = 0; k < va->nraw; k++)
                        PpTokVec_push(&out, wrap_tok(pp, va->raw[k], inv, m));
                }
                i++; /* skip __VA_ARGS__ */
                continue;
            }
            op.flags |= PPTOK_F_PASTEOP;
            PpTokVec_push(&out, op);
            continue;
        }
        if (m->is_function && (pi = find_param(m, bt)) >= 0) {
            MacroArg *a = &args[pi];
            bool next_is_paste = i + 1 < m->body_len &&
                                 m->body[i + 1].kind == PPTOK_PUNCT &&
                                 m->body[i + 1].punct == PUNCT_HASHHASH;
            bool prev_is_paste =
                out.len > 0 && (out.data[out.len - 1].flags & PPTOK_F_PASTEOP);

            if (next_is_paste || prev_is_paste) {
                /* Operand of ##: RAW argument tokens; placemarker if
                 * empty (C11 6.10.3.3p2-3). */
                if (a->nraw == 0) {
                    PpToken pm;
                    memset(&pm, 0, sizeof(pm));
                    pm.kind = PPTOK_PLACEMARKER;
                    pm.spelling = "";
                    pm.loc = wrap_tok(pp, *bt, inv, m).loc;
                    PpTokVec_push(&out, pm);
                } else {
                    u32 k;
                    for (k = 0; k < a->nraw; k++) {
                        PpToken t = wrap_tok(pp, a->raw[k], inv, m);
                        if (k == 0)
                            t.flags |= bt->flags & PPTOK_F_SPACE;
                        PpTokVec_push(&out, t);
                    }
                }
            } else {
                /* Ordinary position: PRE-EXPANDED argument (lazy — an
                 * argument used only stringized must never expand). */
                u32 k;
                arg_expanded(pp, a);
                for (k = 0; k < a->nexp; k++) {
                    PpToken t = wrap_tok(pp, a->expanded[k], inv, m);
                    if (k == 0) /* UNION: body spacing or the arg's own
                                   (gcc emits "2 +(3,4)" — tinycc pp/02) */
                        t.flags |= bt->flags & PPTOK_F_SPACE;
                    PpTokVec_push(&out, t);
                }
            }
            continue;
        }
        PpTokVec_push(&out, wrap_tok(pp, *bt, inv, m));
    }

    /* Pass B: process body-origin ## left-to-right. */
    {
        u32 w = 0, rp = 0;
        while (rp < out.len) {
            if ((out.data[rp].flags & PPTOK_F_PASTEOP) && w > 0 &&
                rp + 1 < out.len) {
                PpToken merged;
                if (paste(pp, &out.data[w - 1], &out.data[rp + 1],
                          out.data[rp].loc, &merged)) {
                    out.data[w - 1] = merged;
                    rp += 2;
                    continue;
                }
                /* Paste error: drop the operator, keep both operands. */
                rp++;
                continue;
            }
            out.data[w++] = out.data[rp++];
        }
        out.len = w;
    }

    /* Pass C: delete placemarkers; apply the new hideset to every token. */
    {
        u32 w = 0, rp;
        for (rp = 0; rp < out.len; rp++) {
            PpToken t = out.data[rp];
            if (t.kind == PPTOK_PLACEMARKER)
                continue;
            t.flags &= (u8)~PPTOK_F_PASTEOP;
            {
                HideSet *hs = t.hideset;
                HideSet *add;
                for (add = hs_new; add; add = add->next)
                    if (!pp_hs_contains(hs, add->name))
                        hs = pp_hs_insert(pp->arena, hs, add->name);
                t.hideset = hs;
            }
            out.data[w++] = t;
        }
        out.len = w;
    }

    /* Arena-copy the result (uniform ownership: callers never free). */
    {
        PpToken *stable = NULL;
        if (out.len) {
            stable = arena_alloc(pp->arena, out.len * sizeof(PpToken),
                                 _Alignof(PpToken));
            memcpy(stable, out.data, out.len * sizeof(PpToken));
        }
        *out_n = (u32)out.len;
        PpTokVec_free(&out);
        return stable;
    }
}

/* --- isolated list expansion ------------------------------------------- */

/* Collects a function-like invocation's arguments from list[pos..] where
 * list[pos] is the '('. Returns false if unterminated (diagnosed). */
static bool collect_args_list(Preprocessor *pp, const MacroDef *m,
                              const PpToken *list, u32 n, u32 lparen,
                              u32 *end_rparen, MacroArg **out_args,
                              u32 *out_nargs)
{
    MacroArg *args;
    u32 max_args = m->nparams + (m->is_variadic ? 1u : 0u);
    u32 i = lparen + 1, depth = 0, start = i, nargs = 0;
    PpTokVec cur = {NULL, 0, 0};

    (void)cur;
    args = arena_alloc(pp->arena, (max_args ? max_args : 1) * sizeof(MacroArg),
                       _Alignof(MacroArg));
    memset(args, 0, (max_args ? max_args : 1) * sizeof(MacroArg));

    for (;; i++) {
        bool split;

        if (i >= n) {
            pp_diag_at(pp, DIAG_ERROR, list[lparen].loc, 1,
                       "unterminated argument list invoking macro '%s'",
                       m->name);
            return false;
        }
        if (list[i].kind == PPTOK_PUNCT && list[i].punct == PUNCT_LPAREN)
            depth++;
        if (list[i].kind == PPTOK_PUNCT && list[i].punct == PUNCT_RPAREN) {
            if (depth == 0) {
                /* final argument */
                if (nargs < max_args) {
                    args[nargs].raw = list + start;
                    args[nargs].nraw = i - start;
                    nargs++;
                } else if (i > start || nargs > 0) {
                    nargs++; /* overflow arg: counted for the error */
                }
                *end_rparen = i;
                break;
            }
            depth--;
        }
        /* Commas split only at depth 0, and never once we are inside the
         * merged variadic tail (which keeps its commas). */
        split = list[i].kind == PPTOK_PUNCT && list[i].punct == PUNCT_COMMA &&
                depth == 0 && !(m->is_variadic && nargs >= m->nparams);
        if (split) {
            if (nargs < max_args) {
                args[nargs].raw = list + start;
                args[nargs].nraw = i - start;
                nargs++;
            } else {
                nargs++;
            }
            start = i + 1;
        }
    }

    /* Z() and I(): a single empty argument is zero args for a 0-param
     * macro, one empty argument for a 1-param macro. */
    if (nargs == 1 && args[0].nraw == 0 && m->nparams == 0 && !m->is_variadic)
        nargs = 0;
    /* V(1) with (a, ...): missing varargs = present-but-empty. */
    if (m->is_variadic && nargs == m->nparams) {
        args[nargs].raw = NULL;
        args[nargs].nraw = 0;
        nargs++;
    }

    *out_args = args;
    *out_nargs = nargs;
    return true;
}

u32 pp_expand_list(Preprocessor *pp, const PpToken *in, u32 n, PpToken **out)
{
    PpTokVec work = {NULL, 0, 0};
    PpTokVec res = {NULL, 0, 0};
    u32 i;

    for (i = 0; i < n; i++)
        PpTokVec_push(&work, in[i]);

    i = 0;
    while (i < work.len) {
        PpToken t = work.data[i];
        const MacroDef *m;

        /* In #if lines a `defined` operator arriving through expansion
         * protects its operand from further expansion (6.10.1p4 is UB;
         * gcc/tcc evaluate it as the operator — match). */
        if (pp->in_if_line && t.kind == PPTOK_IDENT &&
            strcmp(t.spelling, "defined") == 0) {
            u32 take = 1;
            if (i + 1 < work.len && work.data[i + 1].kind == PPTOK_IDENT)
                take = 2;
            else if (i + 3 < work.len && work.data[i + 1].kind == PPTOK_PUNCT &&
                     work.data[i + 1].punct == PUNCT_LPAREN &&
                     work.data[i + 2].kind == PPTOK_IDENT &&
                     work.data[i + 3].kind == PPTOK_PUNCT &&
                     work.data[i + 3].punct == PUNCT_RPAREN)
                take = 4;
            while (take--) {
                PpTokVec_push(&res, work.data[i]);
                i++;
            }
            continue;
        }

        if (t.kind != PPTOK_IDENT || pp_hs_contains(t.hideset, t.spelling) ||
            !(m = pp_macro_lookup(pp, t.spelling))) {
            PpTokVec_push(&res, t);
            i++;
            continue;
        }
        if (m->builtin_kind != MACRO_BUILTIN_NONE) {
            PpToken b = pp_builtin_token(pp, m, t.loc);
            b.flags |= t.flags & (PPTOK_F_SPACE | PPTOK_F_BOL);
            PpTokVec_push(&res, b);
            i++;
            continue;
        }
        if (!m->is_function) {
            HideSet *hs = pp_hs_insert(pp->arena, t.hideset, m->name);
            u32 sn;
            PpToken *sub = pp_macro_subst(pp, m, NULL, 0, hs, t.loc, &sn);
            /* Splice: replace [i] with sub; rescan from the same index. */
            u32 tail = (u32)work.len - i - 1;
            PpTokVec_reserve(&work, work.len - 1 + sn);
            memmove(work.data + i + sn, work.data + i + 1,
                    tail * sizeof(PpToken));
            if (sn)
                memcpy(work.data + i, sub, sn * sizeof(PpToken));
            work.len = work.len - 1 + sn;
            if (sn)
                work.data[i].flags =
                    (work.data[i].flags & (u8) ~(PPTOK_F_SPACE | PPTOK_F_BOL)) |
                    (t.flags & (PPTOK_F_SPACE | PPTOK_F_BOL));
            continue;
        }
        /* Function-like: the ( must be present IN the list (isolated
         * expansion never reads past the end — 6.10.3.1). */
        if (i + 1 >= work.len || work.data[i + 1].kind != PPTOK_PUNCT ||
            work.data[i + 1].punct != PUNCT_LPAREN) {
            PpTokVec_push(&res, t);
            i++;
            continue;
        }
        {
            MacroArg *args;
            u32 nargs, end;
            if (!collect_args_list(pp, m, work.data, (u32)work.len, i + 1, &end,
                                   &args, &nargs)) {
                PpTokVec_push(&res, t);
                i++;
                continue;
            }
            if (!pp_macro_check_args(pp, m, nargs, t.loc)) {
                i = end + 1; /* swallow the bad invocation */
                continue;
            }
            {
                /* Copy raw args out of `work` BEFORE splicing moves it. */
                u32 a, sn;
                HideSet *hs;
                PpToken *sub;
                u32 total = m->nparams + (m->is_variadic ? 1u : 0u);
                for (a = 0; a < total && a < nargs; a++) {
                    if (args[a].nraw) {
                        PpToken *copy = arena_alloc(
                            pp->arena, args[a].nraw * sizeof(PpToken),
                            _Alignof(PpToken));
                        memcpy(copy, args[a].raw,
                               args[a].nraw * sizeof(PpToken));
                        args[a].raw = copy;
                    }
                }
                hs = pp_hs_intersect(pp->arena, t.hideset,
                                     work.data[end].hideset);
                hs = pp_hs_insert(pp->arena, hs, m->name);
                sub = pp_macro_subst(pp, m, args, nargs, hs, t.loc, &sn);
                {
                    u32 tail = (u32)work.len - end - 1;
                    u32 span = end - i + 1;
                    PpTokVec_reserve(&work, work.len - span + sn);
                    memmove(work.data + i + sn, work.data + end + 1,
                            tail * sizeof(PpToken));
                    if (sn)
                        memcpy(work.data + i, sub, sn * sizeof(PpToken));
                    work.len = work.len - span + sn;
                    if (sn)
                        work.data[i].flags =
                            (work.data[i].flags &
                             (u8) ~(PPTOK_F_SPACE | PPTOK_F_BOL)) |
                            (t.flags & (PPTOK_F_SPACE | PPTOK_F_BOL));
                }
            }
            continue;
        }
    }

    PpTokVec_free(&work);
    {
        /* Copy into the arena so callers never free (uniform ownership). */
        PpToken *stable = NULL;
        u32 rn = (u32)res.len;
        if (res.len) {
            stable = arena_alloc(pp->arena, res.len * sizeof(PpToken),
                                 _Alignof(PpToken));
            memcpy(stable, res.data, res.len * sizeof(PpToken));
        }
        *out = stable;
        PpTokVec_free(&res);
        return rn;
    }
}

/* Would printing a and b adjacently re-lex as a DIFFERENT first token?
 * The -E writer inserts a space when so (gcc's avoid-paste rule: `long`
 * `double` from adjacent expansions must not print as `longdouble`;
 * `+` `++` must not print as `+++`). Probe by re-lexing the concat. */
bool pp_tokens_would_merge(Preprocessor *pp, const PpToken *a, const PpToken *b)
{
    size_t al = a->len, bl = b->len;
    char *spell = arena_alloc(pp->arena, al + bl + 1, 1);
    PpToken first;

    /* gcc's avoid_paste is CONSERVATIVE for numbers: after a pp-number,
     * + - . always get a space (the e+/E+/p+ ambiguity) even where exact
     * re-lexing would not merge — match it, the differential is the
     * referee (tinycc pp/02: "2 +(3,4)"). */
    if (a->kind == PPTOK_PPNUM &&
        (b->spelling[0] == '+' || b->spelling[0] == '-' ||
         b->spelling[0] == '.'))
        return true;
    memcpy(spell, a->spelling, al);
    memcpy(spell + al, b->spelling, bl);
    spell[al + bl] = '\0';
    {
        SourceFile tmp;
        PpLexer lx;
        u32 zero = 0;
        bool got;
        DiagSink quiet = {null_sink, NULL};
        DiagSink prev = diag_swap_sink(pp->diag, quiet);

        memset(&tmp, 0, sizeof(tmp));
        tmp.path = "<adjacency-probe>";
        tmp.contents = spell;
        tmp.size = (u32)(al + bl);
        tmp.line_offsets = &zero;
        tmp.nlines = 1;
        pp_lexer_init(&lx, pp, &tmp);
        got = pp_lex_token(&lx, &first);
        buf_free(&lx.scratch);
        diag_swap_sink(pp->diag, prev);
        if (!got)
            return true; /* e.g. "//" became a comment: definitely merged */
        return first.len != a->len;
    }
}

/* --- predefines --------------------------------------------------------- */

static void register_dynamic(Preprocessor *pp, const char *name,
                             MacroBuiltinKind kind)
{
    MacroDef *m = arena_alloc(pp->arena, sizeof(MacroDef), _Alignof(MacroDef));

    memset(m, 0, sizeof(*m));
    m->name =
        intern_str(pp->interner, intern(pp->interner, name, strlen(name)));
    m->is_builtin = true;
    m->builtin_kind = (u8)kind;
    strmap_put(&pp->macros, m->name, strlen(m->name), m);
}

/* CRITICAL POLICY — no __GNUC__. glibc's sys/cdefs.h, seeing no __GNUC__,
 * expands the attribute keyword to NOTHING — exactly what we need
 * until Sprint 55 lands attribute support. Defining __GNUC__ prematurely
 * routes glibc headers into attribute / builtin / extension paths
 * we cannot satisfy and produces thousands of cascading errors. Revisit at
 * Sprint 55 (gnu modes only, with the builtins to back it). */
SourceFile *pp_predefine_all(Preprocessor *pp)
{
    Buf b;
    SourceFile *sf;

    buf_init(&b);
    buf_printf(&b, "#define __CGFRIED__ 1\n");
    buf_printf(&b, "#define __STDC__ 1\n");
    /* C17 4p6: a freestanding implementation says 0 here, and the
     * library subset it promises is exactly the headers we ship. */
    buf_printf(&b, "#define __STDC_HOSTED__ %d\n", pp->freestanding ? 0 : 1);
    switch (pp->std) {
    case STD_C99:
    case STD_GNU99:
        buf_printf(&b, "#define __STDC_VERSION__ 199901L\n");
        break;
    case STD_C11:
    case STD_GNU11:
        buf_printf(&b, "#define __STDC_VERSION__ 201112L\n");
        break;
    case STD_C17:
    case STD_GNU17:
        buf_printf(&b, "#define __STDC_VERSION__ 201710L\n");
        break;
    case STD_C89:
    case STD_GNU89:
        break; /* undefined in C90 */
    }
    /* Honest scope answers (index scope contract). NO_ATOMICS is
     * deliberately absent: we ship _Atomic. */
    buf_printf(&b, "#define __STDC_NO_COMPLEX__ 1\n");
    buf_printf(&b, "#define __STDC_NO_THREADS__ 1\n");
    /* Deliberately no __STDC_IEC_559__. The optimizer does not yet carry
     * #pragma STDC FENV_ACCESS into IR, so claiming Annex F conformance
     * would be false even in the default strict-math mode. Fast math does
     * not change this: the macro is undefined in every mode. */

    /* __DATE__/__TIME__: frozen once per run; SOURCE_DATE_EPOCH (UTC) wins
     * for reproducible builds — determinism invariant. */
    {
        static const char *const mon[] = {"Jan", "Feb", "Mar", "Apr",
                                          "May", "Jun", "Jul", "Aug",
                                          "Sep", "Oct", "Nov", "Dec"};
        const char *sde = cgf_env("SOURCE_DATE_EPOCH");
        time_t when = 0;
        struct tm tmv;

        if (sde) {
            long long v = 0;
            const char *p;
            for (p = sde; *p >= '0' && *p <= '9'; p++)
                v = v * 10 + (*p - '0');
            when = (time_t)v;
            /* reproducible-builds.org mandates UTC for SOURCE_DATE_EPOCH. */
            gmtime_r(&when, &tmv);
        } else {
            /* No epoch pinned: gcc uses LOCAL time, and parity beats a
             * private preference here (the reproducible case is the one
             * that must be pinned, and it is). */
            when = time(NULL);
            localtime_r(&when, &tmv);
        }
        buf_printf(&b, "#define __DATE__ \"%s %2d %d\"\n", mon[tmv.tm_mon],
                   tmv.tm_mday, tmv.tm_year + 1900);
        buf_printf(&b, "#define __TIME__ \"%02d:%02d:%02d\"\n", tmv.tm_hour,
                   tmv.tm_min, tmv.tm_sec);
    }

    cgf_target_predef_lines(cgf_target_host(), pp->gnu_mode, &b);

    sf = pp_source_add_buffer(pp, "<built-in>", (const char *)b.data, b.len);
    buf_free(&b);

    register_dynamic(pp, "__FILE__", MACRO_BUILTIN_FILE);
    register_dynamic(pp, "__LINE__", MACRO_BUILTIN_LINE);
    register_dynamic(pp, "__COUNTER__", MACRO_BUILTIN_COUNTER);
    return sf;
}

/* --- -dM ---------------------------------------------------------------- */

void pp_dump_macros(Preprocessor *pp)
{
    StrmapIter it = strmap_iter(&pp->macros);
    const char *key;
    size_t klen;
    void *val;

    while (strmap_iter_next(&it, &key, &klen, &val)) {
        const MacroDef *m = val;
        u32 i;

        if (!m)
            continue; /* #undef'd */
        if (m->builtin_kind != MACRO_BUILTIN_NONE)
            continue; /* dynamic builtins omitted (gcc parity) */
        printf("#define %s", m->name);
        if (m->is_function) {
            u16 k;
            putchar('(');
            for (k = 0; k < m->nparams; k++)
                printf("%s%s", k ? "," : "", m->params[k]);
            if (m->is_variadic)
                printf("%s...", m->nparams ? "," : "");
            putchar(')');
        }
        if (m->body_len)
            putchar(' ');
        for (i = 0; i < m->body_len; i++) {
            if (i > 0 && (m->body[i].flags & PPTOK_F_SPACE))
                putchar(' ');
            fputs(m->body[i].spelling, stdout);
        }
        putchar('\n');
    }
}
