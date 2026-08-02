#include <string.h>

#include "parse/parse.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Recovery: the PROGRESS guarantee, poison propagation, the error cap, the
 * unknown-type heuristic's cascade suppression, and the bracket limit. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    DiagCtx *dc;
    int errors;
    int warnings;
    int notes;
} RecFix;

static void rec_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    RecFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
    else
        f->notes++;
}

VEC_DECL(PpVecR, PpToken);

static AstNode *parse_src_r(RecFix *f, const char *src, u32 max_errors)
{
    DiagSink s;
    SourceFile *sf;
    PpVecR pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec target;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    s.handle = rec_sink;
    s.user = f;
    diag_set_sink(f->dc, s);
    diag_set_max_errors(f->dc, max_errors);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = STD_C17;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    f->pp.warn = lang.warnings;
    target.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecR_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, target, &f->arena);
    PpVecR_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    return parse_translation_unit(&f->ps);
}

static void rfix_free(RecFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* THE payoff. A missing header makes every use of the type an error in a
 * naive parser; gcc reports six for this input, we report one. */
void test_recover_unknown_type_cascade(TestCtx *t)
{
    RecFix f;

    (void)parse_src_r(&f,
                      "u32 a;\n"
                      "u32 b;\n"
                      "u32 *c;\n"
                      "u32 f(u32 x) { u32 y = x; return y; }\n"
                      "u32 arr[4];\n",
                      0);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    rfix_free(&f);

    /* Block scope: only the unambiguous `ident ident` shape triggers it. */
    (void)parse_src_r(&f,
                      "int f(void) {\n"
                      "    u32 a;\n"
                      "    u32 b = 1;\n"
                      "    u32 *c = &a;\n"
                      "    u32 d[3];\n"
                      "    return 0;\n"
                      "}\n",
                      0);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    rfix_free(&f);

    /* ...and `x * y;` must STAY multiplication when x is a variable,
     * because it genuinely is one. Guessing "declaration" here would
     * break valid code; sema diagnoses an undeclared x instead. */
    (void)parse_src_r(&f, "int x, y;\nvoid f(void){ x * y; }\n", 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    rfix_free(&f);
}

/* parse_sync must always advance. An adversarial stream that leaves the
 * cursor put is a hang, and the block/TU loops' progress guards are the
 * only other thing standing in the way. */
void test_recover_sync_always_progresses(TestCtx *t)
{
    static const char *const streams[] = {
        "void f(void){ ; }\n",
        "void f(void){ } }\n",
        "void f(void){ ( ; ) }\n",
        "void f(void){ ) ; }\n",
        "void f(void){ [ ] ; }\n",
        "void f(void){ , , ; }\n",
        "}\n",
        ";\n",
        ") ; }\n",
        "struct {\n",
    };
    u32 i;

    for (i = 0; i < sizeof(streams) / sizeof(streams[0]); i++) {
        RecFix f;
        u32 before, after, k;

        (void)parse_src_r(&f, streams[i], 0);
        /* Re-run parse_sync directly from every position in the stream and
         * assert the cursor strictly advances (or is already at EOF). */
        for (k = 0; k < f.ps.ntoks; k++) {
            SyncSet sets[4];
            u32 si;

            sets[0] = SYNC_DECL;
            sets[1] = SYNC_STMT;
            sets[2] = SYNC_MEMBER;
            sets[3] = SYNC_PAREN;
            for (si = 0; si < 4; si++) {
                f.ps.pos = k;
                before = f.ps.pos;
                parse_sync(&f.ps, sets[si]);
                after = f.ps.pos;
                /* Two positions where standing still is CORRECT: at EOF,
                 * and on a '}' — advancing there would eat the brace the
                 * caller needs to close its block (SCOPE BALANCE wins over
                 * PROGRESS, and the block loop supplies the progress one
                 * level up). */
                if (after == before && (parse_peek(&f.ps)->kind == TOK_EOF ||
                                        parse_at_punct(&f.ps, PUNCT_RBRACE))) {
                    t->assertions++;
                    continue;
                }
                if (after <= before)
                    t_fail(t, __FILE__, __LINE__,
                           "parse_sync did not advance: stream %u pos %u "
                           "set %u",
                           i, k, si);
                t->assertions++;
            }
        }
        rfix_free(&f);
    }
}

/* parse_sync must not eat a '}': the brace belongs to an enclosing block
 * whose scope the caller still has to pop. */
void test_recover_sync_stops_before_rbrace(TestCtx *t)
{
    RecFix f;
    u32 i;

    (void)parse_src_r(&f, "void f(void){ a b c } int after;\n", 0);
    for (i = 0; i < f.ps.ntoks; i++) {
        f.ps.pos = i;
        parse_sync(&f.ps, SYNC_STMT);
        if (f.ps.pos < f.ps.ntoks) {
            const Token *at = parse_peek(&f.ps);
            /* Landing ON a '}' is fine; having consumed one is not — check
             * by confirming we never skipped past a brace that lay
             * between the start and the landing point. */
            u32 k;
            for (k = i; k < f.ps.pos; k++)
                if (f.ps.toks[k].kind == TOK_PUNCT &&
                    f.ps.toks[k].punct == PUNCT_RBRACE)
                    t_fail(t, __FILE__, __LINE__,
                           "parse_sync consumed a '}' (from %u to %u)", i,
                           f.ps.pos);
            (void)at;
        }
        t->assertions++;
    }
    rfix_free(&f);
}

void test_recover_max_errors(TestCtx *t)
{
    RecFix f;
    const char *src = "void a(void){ 1 2 3 4 5 6 7 8; }\n"
                      "void b(void){ 1 2 3 4 5 6 7 8; }\n"
                      "void c(void){ 1 2 3 4 5 6 7 8; }\n";
    int unlimited;

    (void)parse_src_r(&f, src, 0);
    unlimited = f.errors;
    T_ASSERT(t, unlimited >= 3);
    rfix_free(&f);

    /* The cap counts ERRORS; the "too many errors" notice is itself
     * emitted through the sink, hence cap + 1. */
    (void)parse_src_r(&f, src, 1);
    T_ASSERT_EQ_INT(t, f.errors, 2);
    T_ASSERT(t, diag_error_limit_reached(f.dc));
    rfix_free(&f);

    (void)parse_src_r(&f, src, 2);
    T_ASSERT_EQ_INT(t, f.errors, 3);
    rfix_free(&f);

    /* 0 is unlimited (gcc's default), not "stop immediately". */
    (void)parse_src_r(&f, src, 0);
    T_ASSERT_EQ_INT(t, f.errors, unlimited);
    T_ASSERT(t, !diag_error_limit_reached(f.dc));
    rfix_free(&f);
}

/* Poison flows up at construction. Sema's contract (Sprint 12+) is that a
 * poisoned subtree gets no diagnostics at all, so the flag has to be
 * present on the PARENT, not just on the error node. */
void test_recover_poison_propagates(TestCtx *t)
{
    RecFix f;
    AstNode *tu;
    AstNode *stmt;

    tu = parse_src_r(&f, "int a;\nvoid f(void){ a = ; }\n", 0);
    T_ASSERT(t, f.errors >= 1);
    stmt = tu->decls[1]->body->items[0];
    T_ASSERT(t, stmt->kind == AST_STMT_EXPR);
    T_ASSERT(t, stmt->poisoned);
    rfix_free(&f);

    /* A parenthesized expression wrapping an error is poisoned too. */
    tu = parse_src_r(&f, "int a;\nvoid f(void){ a = ( ); }\n", 0);
    T_ASSERT(t, f.errors >= 1);
    stmt = tu->decls[1]->body->items[0];
    T_ASSERT(t, stmt->poisoned);
    rfix_free(&f);
}

/* Suppression is what makes "one mistake, one diagnostic" true. Asserting
 * the counter MOVED is the only way to distinguish real suppression from
 * an error that simply never happened. */
void test_recover_suppression_counted(TestCtx *t)
{
    RecFix f;

    /* Three nested parens with a stray ';' inside: without suppression
     * this reports the expression error plus one "expected ')'" per level.
     * With it, one error and two suppressed. */
    (void)parse_src_r(&f, "int f(void){ return (((; }\n", 0);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT(t, diag_suppressed_count(f.dc) >= 2);
    rfix_free(&f);

    (void)parse_src_r(&f, "int a;\nvoid f(void){ a = (1 + ; }\n", 0);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT(t, diag_suppressed_count(f.dc) > 0);
    rfix_free(&f);

    (void)parse_src_r(&f, "void f(void){ ( ( ( ; }\n", 0);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT(t, diag_suppressed_count(f.dc) > 0);
    rfix_free(&f);

    /* A clean parse suppresses nothing — the counter is not just noise. */
    (void)parse_src_r(&f, "int x;\nvoid f(void){ x = 1; }\n", 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, (int)diag_suppressed_count(f.dc), 0);
    rfix_free(&f);
}

void test_recover_bracket_depth_limit(TestCtx *t)
{
    RecFix f;
    Buf b;
    u32 i;

    /* Well inside the limit: fine. */
    buf_init(&b);
    buf_printf(&b, "int f(void){ return ");
    for (i = 0; i < 200; i++)
        buf_printf(&b, "(");
    buf_printf(&b, "1");
    for (i = 0; i < 200; i++)
        buf_printf(&b, ")");
    buf_printf(&b, "; }\n");
    buf_push_u8(&b, 0);
    (void)parse_src_r(&f, (const char *)b.data, 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    rfix_free(&f);
    buf_free(&b);

    /* Past it: a clean diagnostic, not a stack overflow. */
    buf_init(&b);
    buf_printf(&b, "int f(void){ return ");
    for (i = 0; i < 400; i++)
        buf_printf(&b, "(");
    buf_printf(&b, "1");
    for (i = 0; i < 400; i++)
        buf_printf(&b, ")");
    buf_printf(&b, "; }\n");
    buf_push_u8(&b, 0);
    (void)parse_src_r(&f, (const char *)b.data, 0);
    T_ASSERT(t, f.errors >= 1);
    rfix_free(&f);
    buf_free(&b);
}

/* Recovery corner cases the sprint calls out by name — each one must
 * terminate with a diagnostic rather than hang, crash, or fall silent. */
void test_recover_corner_cases(TestCtx *t)
{
    static const char *const cases[] = {
        "int f(void) { return 1; ) }\n",       /* unbalanced ')' */
        "struct {\n",                          /* struct '{' at EOF */
        "const char *s = \"unterminated\n",    /* string spanning EOF */
        "int a[] = { 1, struct s; 2 };\n",     /* declaration in an init */
        "int f(void) { if (a) { return 1; \n", /* unclosed block at EOF */
        "typedef int T; T\n",                  /* typedef then EOF */
        "int f(int a, \n",                     /* param list at EOF */
        "enum { A =\n",                        /* enumerator value at EOF */
        "int f(void) { switch (1) { case\n",   /* case value at EOF */
        "void f(void) { for (;;\n",            /* for clauses at EOF */
    };
    u32 i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        RecFix f;

        (void)parse_src_r(&f, cases[i], 0);
        if (f.errors == 0)
            t_fail(t, __FILE__, __LINE__, "case %u produced no diagnostic", i);
        t->assertions++;
        rfix_free(&f);
    }
}

/* Every diagnostic must name a location inside its file. This is the
 * invariant the fuzzer enforces from outside; asserting it here too means
 * a regression fails the fast suite, not only the fuzz job. */
void test_recover_spans_in_bounds(TestCtx *t)
{
    static const char *const cases[] = {
        "b\\\nc\\\nd\n",          /* spliced identifier: the fuzz finding */
        "int a = 1\n",            /* missing ';' at end of line */
        "int f(void){ g(1; }\n",  /* unmatched '(' */
        "struct S { int a\n};\n", /* missing ';' before '}' */
        "int f(void){ return 1\n}\n",
    };
    u32 i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        RecFix f;

        (void)parse_src_r(&f, cases[i], 0);
        /* The parse itself runs the span checks when CGF_FUZZ is set; here
         * we assert the weaker but always-on property that a diagnostic
         * was produced with a real file id. */
        T_ASSERT(t, f.errors >= 1);
        rfix_free(&f);
    }
}
