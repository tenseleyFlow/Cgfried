#include <string.h>

#include "pp/pp.h"
#include "unit.h"
#include "util/arena.h"

/* Macro-table units: define/undef/lookup/redefinition compare, and the
 * #line presumed-vs-physical split, all through the full engine. */
typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    int errors;
    int warnings;
    Span last_span;
} MacFix;

static void count_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    MacFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
    f->last_span = d->span;
}

static void mfix_init(MacFix *f)
{
    DiagCtx *dc;
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    sink.handle = count_sink;
    sink.user = f;
    diag_set_sink(dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
}

static void mfix_free(MacFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* Runs `src` through the engine, draining text tokens into out (if given). */
static u32 run_pp(MacFix *f, const char *src, PpToken *out, u32 max)
{
    SourceFile *sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    PpToken t;
    u32 n = 0;

    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t)) {
        if (out && n < max)
            out[n] = t;
        n++;
    }
    return n;
}

void test_pp_macro_table(TestCtx *t)
{
    MacFix f;
    const MacroDef *m;

    mfix_init(&f);
    run_pp(&f,
           "#define OBJ 1 + 2\n"
           "#define FN(a, b) a + b\n"
           "#define VAR(x, ...) x\n"
           "#define TRAP (x) obj_like\n"
           "#undef NEVER_DEFINED\n",
           NULL, 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);

    m = pp_macro_lookup(&f.pp, "OBJ");
    T_ASSERT(t, m && !m->is_function);
    T_ASSERT_EQ_INT(t, m->body_len, 3);

    m = pp_macro_lookup(&f.pp, "FN");
    T_ASSERT(t, m && m->is_function && !m->is_variadic);
    T_ASSERT_EQ_INT(t, m->nparams, 2);
    T_ASSERT_EQ_STR(t, m->params[0], "a");
    T_ASSERT_EQ_STR(t, m->params[1], "b");

    m = pp_macro_lookup(&f.pp, "VAR");
    T_ASSERT(t, m && m->is_function && m->is_variadic);
    T_ASSERT_EQ_INT(t, m->nparams, 1);

    /* The classic: space before ( makes it OBJECT-like. */
    m = pp_macro_lookup(&f.pp, "TRAP");
    T_ASSERT(t, m && !m->is_function);
    T_ASSERT_EQ_INT(t, m->body_len, 4); /* ( x ) obj_like */

    T_ASSERT(t, pp_macro_lookup(&f.pp, "NEVER_DEFINED") == NULL);
    mfix_free(&f);
}

void test_pp_macro_redefinition(TestCtx *t)
{
    MacFix f;

    /* Identical tokens AND spacing: benign, silent. */
    mfix_init(&f);
    run_pp(&f, "#define A x + y\n#define A x + y\n", NULL, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    mfix_free(&f);

    /* Same tokens, different spacing: warn + note, new wins (ISO 6.10.3p2:
     * spacing participates in the comparison). */
    mfix_init(&f);
    run_pp(&f, "#define A x + y\n#define A x +y\n", NULL, 0);
    T_ASSERT(t, f.warnings >= 1);
    mfix_free(&f);

    /* Different body: warn. */
    mfix_init(&f);
    run_pp(&f, "#define A 1\n#define A 2\n#if A == 2\n#endif\n", NULL, 0);
    T_ASSERT(t, f.warnings >= 1);
    mfix_free(&f);

    /* Param spelling participates too. */
    mfix_init(&f);
    run_pp(&f, "#define F(a) a\n#define F(b) b\n", NULL, 0);
    T_ASSERT(t, f.warnings >= 1);
    mfix_free(&f);

    /* Protected names. */
    mfix_init(&f);
    run_pp(&f, "#define defined 1\n", NULL, 0);
    T_ASSERT(t, f.errors >= 1);
    mfix_free(&f);

    /* Duplicate parameter. */
    mfix_init(&f);
    run_pp(&f, "#define F(a, a) a\n", NULL, 0);
    T_ASSERT(t, f.errors >= 1);
    mfix_free(&f);

    /* ## at end of body. */
    mfix_init(&f);
    run_pp(&f, "#define B x ##\n", NULL, 0);
    T_ASSERT(t, f.errors >= 1);
    mfix_free(&f);

    /* # not followed by a parameter (function-like). */
    mfix_init(&f);
    run_pp(&f, "#define F(a) # b\n", NULL, 0);
    T_ASSERT(t, f.errors >= 1);
    mfix_free(&f);
}

void test_pp_line_presumed_vs_physical(TestCtx *t)
{
    MacFix f;
    PpToken toks[4];
    u32 n;
    FileId fid;
    u32 line, col;

    mfix_init(&f);
    n = run_pp(&f, "#line 100 \"virt.c\"\nx\n#error boom\n", toks, 4);
    T_ASSERT_EQ_INT(t, n, 1);

    /* Physical location stays true internally (DoD 4)... */
    pp_loc_resolve(&f.pp.loc, toks[0].loc, &fid, &line, &col);
    T_ASSERT_EQ_INT(t, line, 2);
    /* ...while the diagnostic Span carries the presumed remap. */
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT_EQ_INT(t, f.last_span.presumed_line, 101); /* #error on 3 */
    T_ASSERT(t, f.last_span.presumed_path != NULL);
    T_ASSERT_EQ_STR(t, f.last_span.presumed_path, "virt.c");
    T_ASSERT_EQ_INT(t, f.last_span.line, 3); /* physical in the Span too */
    mfix_free(&f);
}

void test_pp_engine_skipping(TestCtx *t)
{
    MacFix f;
    PpToken toks[8];
    u32 n;

    /* Skipped groups: only conditional directives are even looked at —
     * #garbage and stray junk in a false branch are fine (ISO). */
    mfix_init(&f);
    n = run_pp(&f,
               "#if 0\n#garbage anything\n@ $ `\n#if 1\nnested\n#endif\n"
               "#else\ntaken\n#endif\n",
               toks, 8);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, n, 1);
    T_ASSERT_EQ_STR(t, toks[0].spelling, "taken");
    mfix_free(&f);

    /* #elif chains: first true group wins, later ones stay dead. */
    mfix_init(&f);
    n = run_pp(&f, "#if 0\na\n#elif 1\nb\n#elif 1/0\nc\n#else\nd\n#endif\n",
               toks, 8);
    T_ASSERT_EQ_INT(t, f.errors, 0); /* dead #elif is never evaluated */
    T_ASSERT_EQ_INT(t, n, 1);
    T_ASSERT_EQ_STR(t, toks[0].spelling, "b");
    mfix_free(&f);

    /* defined() works today; expansion still routes to the seam. */
    mfix_init(&f);
    n = run_pp(&f,
               "#define FOO 1\n#if defined(FOO) && defined FOO\nyes\n"
               "#endif\n",
               toks, 8);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, n, 1);
    T_ASSERT_EQ_STR(t, toks[0].spelling, "yes");
    mfix_free(&f);

    /* Real expansion in program text. */
    mfix_init(&f);
    n = run_pp(&f, "#define M 41 + 1\nM\n", toks, 8);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, n, 3);
    T_ASSERT_EQ_STR(t, toks[0].spelling, "41");
    T_ASSERT_EQ_STR(t, toks[2].spelling, "1");
    mfix_free(&f);
}

void test_pp_hidesets(TestCtx *t)
{
    MacFix f;
    HideSet *a, *b, *i;
    const char *n1, *n2;

    mfix_init(&f);
    n1 = intern_str(&f.in, intern(&f.in, "alpha", 5));
    n2 = intern_str(&f.in, intern(&f.in, "beta", 4));
    a = pp_hs_insert(&f.arena, NULL, n1);
    a = pp_hs_insert(&f.arena, a, n2);
    b = pp_hs_insert(&f.arena, NULL, n2);
    T_ASSERT(t, pp_hs_contains(a, n1));
    T_ASSERT(t, pp_hs_contains(a, n2));
    T_ASSERT(t, !pp_hs_contains(b, n1));
    i = pp_hs_intersect(&f.arena, a, b);
    T_ASSERT(t, pp_hs_contains(i, n2));
    T_ASSERT(t, !pp_hs_contains(i, n1));
    T_ASSERT(t, !pp_hs_contains(NULL, n1));
    mfix_free(&f);
}

/* Runs src through the engine and renders text tokens SPACE-separated. */
static void expect_pp(TestCtx *t, MacFix *f, const char *src, const char *want)
{
    PpToken toks[64];
    u32 n, i;
    char got[512];
    size_t o = 0;

    mfix_init(f);
    n = run_pp(f, src, toks, 64);
    for (i = 0; i < n && o + toks[i].len + 2 < sizeof(got); i++) {
        if (i && (toks[i].flags & (PPTOK_F_SPACE | PPTOK_F_BOL)))
            got[o++] = ' ';
        memcpy(got + o, toks[i].spelling, toks[i].len);
        o += toks[i].len;
    }
    got[o] = '\0';
    if (strcmp(got, want) != 0)
        t_fail(t, __FILE__, __LINE__, "pp of %s: got \"%s\", want \"%s\"", src,
               got, want);
    t->assertions++;
    mfix_free(f);
}

void test_pp_placemarker_table(TestCtx *t)
{
    MacFix f;

    /* C11 6.10.3.3p2-3 truth table. */
    expect_pp(t, &f, "#define cat(a,b) a##b\ncat(x,y)\n", "xy");
    expect_pp(t, &f, "#define cat(a,b) a##b\ncat(x,)\n", "x");
    expect_pp(t, &f, "#define cat(a,b) a##b\ncat(,y)\n", "y");
    expect_pp(t, &f, "#define cat(a,b) a##b\ncat(,)\n", "");
    /* Chains evaluate left-to-right with placemarkers intact. */
    expect_pp(t, &f, "#define c3(a,b,c) a##b##c\nc3(1,,3)\n", "13");
    expect_pp(t, &f, "#define c3(a,b,c) a##b##c\nc3(,,)\n", "");
    expect_pp(t, &f, "#define c3(a,b,c) a##b##c\nc3(x,y,z)\n", "xyz");
}

void test_pp_paste_matrix(TestCtx *t)
{
    MacFix f;
    PpToken toks[8];

    expect_pp(t, &f, "#define P(a,b) a##b\nP(+,+)\n", "++");
    expect_pp(t, &f, "#define P(a,b) a##b\nP(<,<)\n", "<<");
    expect_pp(t, &f, "#define P(a,b) a##b\nP(x,1)\n", "x1");
    expect_pp(t, &f, "#define P(a,b) a##b\nP(1,e5)\n", "1e5");

    /* Invalid pastes: error, operands kept (gcc keeps both tokens). */
    mfix_init(&f);
    run_pp(&f, "#define P(a,b) a##b\nP(x,+)\n", toks, 8);
    T_ASSERT(t, f.errors >= 1);
    mfix_free(&f);
    mfix_init(&f);
    run_pp(&f, "#define P(a,b) a##b\nP(.,.)\n", toks, 8);
    T_ASSERT(t, f.errors >= 1); /* .. is not a pp-token */
    mfix_free(&f);

    /* Pasted tokens are expansion candidates (not painted). */
    expect_pp(t, &f, "#define X 42\n#define GLUE(a,b) a##b\nGLUE(,X)\n", "42");
}

void test_pp_stringize_fidelity(TestCtx *t)
{
    MacFix f;

    expect_pp(t, &f, "#define S(x) #x\nS(a   +b)\n", "\"a +b\"");
    expect_pp(t, &f, "#define S(x) #x\nS(\"q\\n\")\n", "\"\\\"q\\\\n\\\"\"");
    expect_pp(t, &f, "#define S(x) #x\nS()\n", "\"\"");
    expect_pp(t, &f, "#define S(x) #x\nS(  lead trail  )\n", "\"lead trail\"");
    /* Unexpanded: # operand uses the ORIGINAL argument tokens. */
    expect_pp(t, &f, "#define N 42\n#define S(x) #x\nS(N)\n", "\"N\"");
    /* Both roles: #x x. */
    expect_pp(t, &f, "#define N 42\n#define B(x) #x x\nB(N)\n", "\"N\" 42");
}

void test_pp_expansion_loc_chain(TestCtx *t)
{
    MacFix f;
    PpToken toks[8];
    u32 n;
    FileId fid;
    u32 line, col;
    SrcLoc l;
    int depth = 0;

    /* 3-level nest: INNER's token reaches the output through three
     * expansions; parents walk to the outermost use site (line 4). */
    mfix_init(&f);
    n = run_pp(&f,
               "#define INNER 7\n#define MID INNER\n#define OUTER MID\n"
               "OUTER\n",
               toks, 8);
    T_ASSERT_EQ_INT(t, n, 1);
    T_ASSERT_EQ_STR(t, toks[0].spelling, "7");
    l = toks[0].loc;
    T_ASSERT(t, pp_loc_is_expansion(&f.pp.loc, l));
    while (pp_loc_is_expansion(&f.pp.loc, l)) {
        l = pp_loc_expansion_parent(&f.pp.loc, l);
        depth++;
        T_ASSERT(t, depth < 10);
    }
    T_ASSERT(t, depth >= 3);
    pp_loc_resolve(&f.pp.loc, l, &fid, &line, &col);
    T_ASSERT_EQ_INT(t, line, 4); /* the OUTER use site */
    /* And the spelling side resolves to the definition of INNER (line 1). */
    pp_loc_resolve(&f.pp.loc, toks[0].loc, &fid, &line, &col);
    T_ASSERT_EQ_INT(t, line, 1);
    {
        Span spelling = pp_span(&f.pp, toks[0].loc, toks[0].len);
        Span debug = diag_span_for_debug(f.pp.diag, spelling);

        T_ASSERT_EQ_INT(t, spelling.line, 1);
        T_ASSERT(t, spelling.debug_loc != 0);
        T_ASSERT_EQ_INT(t, debug.line, 4);
        T_ASSERT_EQ_STR(t, diag_span_path(f.pp.diag, debug), "t.c");
    }
    mfix_free(&f);
}
