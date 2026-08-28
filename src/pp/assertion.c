#include <string.h>

#include "pp/pp.h"
#include "warn/warn.h"

typedef enum {
    ASSERT_PARSE_DEFINE,
    ASSERT_PARSE_UNDEF,
    ASSERT_PARSE_TEST,
} AssertParseMode;

typedef struct {
    const char *predicate;
    const PpToken *answer;
    u32 nanswer;
    u32 consumed;
} AssertParts;

static void warn_directive(Preprocessor *pp, SrcLoc loc, const char *name)
{
    Span sp = pp_span(pp, loc, (u32)strlen(name));

    /* GCC gives the ISO-mode pedwarn precedence over -Wdeprecated. */
    if (warn_enabled(pp->warn, WARN_PEDANTIC, sp))
        pp_pedwarn_at(pp, WARN_PEDANTIC, loc, (u32)strlen(name),
                      "'#%s' is a GCC extension", name);
    else
        pp_warn_at(pp, WARN_DEPRECATED, loc, (u32)strlen(name),
                   "'#%s' is a deprecated GCC extension", name);
}

static void warn_test(Preprocessor *pp, SrcLoc loc)
{
    Span sp = pp_span(pp, loc, 1);

    if (warn_enabled(pp->warn, WARN_PEDANTIC, sp))
        pp_pedwarn_at(pp, WARN_PEDANTIC, loc, 1,
                      "assertions are a GCC extension");
    else
        pp_warn_at(pp, WARN_DEPRECATED, loc, 1,
                   "assertions are a deprecated extension");
}

static bool parse_parts(Preprocessor *pp, const PpToken *toks, u32 n,
                        SrcLoc fallback_loc, AssertParseMode mode,
                        AssertParts *out)
{
    u32 close;

    memset(out, 0, sizeof(*out));
    if (n == 0) {
        pp_diag_at(pp, DIAG_ERROR, fallback_loc, 1,
                   "assertion without predicate");
        return false;
    }
    if (toks[0].kind != PPTOK_IDENT) {
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "predicate must be an identifier");
        return false;
    }
    out->predicate = toks[0].spelling;
    out->consumed = 1;

    if (n == 1) {
        if (mode == ASSERT_PARSE_DEFINE) {
            pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                       "missing '(' after predicate");
            return false;
        }
        return true;
    }
    if (toks[1].kind != PPTOK_PUNCT || toks[1].punct != PUNCT_LPAREN) {
        /* In #if, an answer-less test consumes only the predicate and leaves
         * the following token to the ordinary expression parser. */
        if (mode == ASSERT_PARSE_TEST)
            return true;
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "missing '(' after predicate");
        return false;
    }

    for (close = 2; close < n; close++) {
        if (toks[close].kind == PPTOK_PUNCT &&
            toks[close].punct == PUNCT_RPAREN)
            break;
    }
    if (close == n) {
        pp_diag_at(pp, DIAG_ERROR, toks[1].loc, toks[1].len,
                   "missing ')' to complete answer");
        return false;
    }
    if (close == 2) {
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "predicate's answer is empty");
        return false;
    }
    out->answer = toks + 2;
    out->nanswer = close - 2;
    out->consumed = close + 1;
    return true;
}

static PpAssertion *find_predicate(Preprocessor *pp, const char *name)
{
    PpAssertion *p;

    for (p = pp->assertions; p; p = p->next)
        if (p->predicate == name || strcmp(p->predicate, name) == 0)
            return p;
    return NULL;
}

static bool token_equal(const PpToken *a, const PpToken *b, bool first)
{
    if (a->kind != b->kind || a->len != b->len)
        return false;
    if (a->kind == PPTOK_PUNCT && a->punct != b->punct)
        return false;
    if (a->len && memcmp(a->spelling, b->spelling, a->len) != 0)
        return false;
    return first || ((a->flags ^ b->flags) & PPTOK_F_SPACE) == 0;
}

static bool answer_equal(const PpAssertionAnswer *a, const PpToken *toks, u32 n)
{
    u32 i;

    if (a->ntoks != n)
        return false;
    for (i = 0; i < n; i++)
        if (!token_equal(&a->toks[i], &toks[i], i == 0))
            return false;
    return true;
}

static PpAssertionAnswer **find_answer_link(PpAssertion *p, const PpToken *toks,
                                            u32 n)
{
    PpAssertionAnswer **link;

    for (link = &p->answers; *link; link = &(*link)->next)
        if (answer_equal(*link, toks, n))
            break;
    return link;
}

void pp_assert_define_line(Preprocessor *pp, const PpToken *toks, u32 n,
                           SrcLoc directive_loc)
{
    AssertParts parts;
    PpAssertion *p;
    PpAssertionAnswer **link;
    PpAssertionAnswer *answer;

    warn_directive(pp, directive_loc, "assert");
    if (!parse_parts(pp, toks, n, directive_loc, ASSERT_PARSE_DEFINE, &parts))
        return;
    if (parts.consumed < n)
        pp_warn_at(pp, WARN_CPP, toks[parts.consumed].loc,
                   toks[parts.consumed].len,
                   "extra tokens at end of #assert directive");

    p = find_predicate(pp, parts.predicate);
    if (!p) {
        p = arena_alloc(pp->arena, sizeof(*p), _Alignof(PpAssertion));
        memset(p, 0, sizeof(*p));
        p->predicate = parts.predicate;
        if (pp->assertions_tail)
            pp->assertions_tail->next = p;
        else
            pp->assertions = p;
        pp->assertions_tail = p;
    }
    link = find_answer_link(p, parts.answer, parts.nanswer);
    if (*link) {
        pp_warn_at(pp, WARN_CPP, toks[0].loc, toks[0].len, "'%s' re-asserted",
                   parts.predicate);
        return;
    }

    answer =
        arena_alloc(pp->arena, sizeof(*answer), _Alignof(PpAssertionAnswer));
    memset(answer, 0, sizeof(*answer));
    answer->toks = arena_alloc(pp->arena, parts.nanswer * sizeof(PpToken),
                               _Alignof(PpToken));
    memcpy(answer->toks, parts.answer, parts.nanswer * sizeof(PpToken));
    answer->ntoks = parts.nanswer;
    *link = answer;
}

void pp_assert_undef_line(Preprocessor *pp, const PpToken *toks, u32 n,
                          SrcLoc directive_loc)
{
    AssertParts parts;
    PpAssertion *p;

    warn_directive(pp, directive_loc, "unassert");
    if (!parse_parts(pp, toks, n, directive_loc, ASSERT_PARSE_UNDEF, &parts))
        return;
    if (parts.consumed < n)
        pp_warn_at(pp, WARN_CPP, toks[parts.consumed].loc,
                   toks[parts.consumed].len,
                   "extra tokens at end of #unassert directive");

    p = find_predicate(pp, parts.predicate);
    if (!p)
        return;
    if (!parts.answer) {
        p->answers = NULL;
    } else {
        PpAssertionAnswer **link =
            find_answer_link(p, parts.answer, parts.nanswer);
        if (*link)
            *link = (*link)->next;
    }
}

bool pp_assert_test_tokens(Preprocessor *pp, const PpToken *toks, u32 n,
                           SrcLoc hash_loc, u32 *consumed, bool *ok)
{
    AssertParts parts;
    PpAssertion *p;

    warn_test(pp, hash_loc);
    *ok = parse_parts(pp, toks, n, hash_loc, ASSERT_PARSE_TEST, &parts);
    if (!*ok) {
        *consumed = n;
        return false;
    }
    *consumed = parts.consumed;
    p = find_predicate(pp, parts.predicate);
    if (!p || !p->answers)
        return false;
    if (!parts.answer)
        return true;
    return *find_answer_link(p, parts.answer, parts.nanswer) != NULL;
}
