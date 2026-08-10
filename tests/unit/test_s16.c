#include <string.h>

#include "parse/parse.h"
#include "sema/sema.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Sprint 16: the inline matrix (all orderings), tentative resolution in
 * both -fcommon modes, and the VM classifier — asserted STRUCTURALLY on
 * the symbols, where the fixtures assert the dump text. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    int warnings;
} S16Fix;

static void s16_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    S16Fix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

VEC_DECL(PpVecS16, PpToken);

static void run16_std(S16Fix *f, const char *src, bool fcommon, CStd std)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecS16 pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec spec;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = s16_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);
    memset(&lang, 0, sizeof(lang));
    lang.std = std;
    lang.gnu_mode = std >= STD_GNU89;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    f->pp.warn = lang.warnings;
    spec.kind = CGF_TARGET_X86_64_LINUX_GNU;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecS16_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, spec, &f->arena);
    PpVecS16_free(&pv);
    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, spec);
    f->sema.fcommon = fcommon;
    sema_run(&f->sema, tu);
}

static void run16(S16Fix *f, const char *src, bool fcommon)
{
    run16_std(f, src, fcommon, STD_C17);
}

static void s16_free(S16Fix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static Symbol *sym16(S16Fix *f, const char *name)
{
    return scope_lookup(f->sema.file_scope,
                        intern_str(&f->in, intern_cstr(&f->in, name)),
                        NS_ORDINARY);
}

/* One case per matrix row and per ordering variant. The DECISION is the
 * assertion; scripts/inline_diff.sh separately proves each row against
 * what gcc -S emits, so this table and the oracle can never drift apart
 * without one of them failing. */
static void inline_is(TestCtx *t, const char *src, int want, const char *label)
{
    S16Fix f;
    Symbol *sym;

    run16(&f, src, true);
    sym = sym16(&f, "f");
    if (!sym)
        t_fail(t, __FILE__, __LINE__, "%s: f not found", label);
    else if ((int)sym->inline_kind != want)
        t_fail(t, __FILE__, __LINE__, "%s: inline_kind %d, want %d", label,
               (int)sym->inline_kind, want);
    t->assertions++;
    s16_free(&f);
}

void test_s16_inline_matrix(TestCtx *t)
{
    S16Fix f;

    /* The four rows. */
    inline_is(t, "inline int f(void) { return 1; }\n", INL_INLINE_DEF,
              "all-inline no-extern");
    inline_is(t, "extern inline int f(void) { return 1; }\n", INL_EXTERN_INLINE,
              "extern inline");
    inline_is(t, "static inline int f(void) { return 1; }\n", INL_STATIC,
              "static inline");
    inline_is(t, "int f(void) { return 1; }\n", INL_NONE, "not inline");
    /* The six ordering variants: the decision needs EVERY declaration,
     * whichever side of the body it lands on. */
    inline_is(t, "inline int f(void) { return 1; }\nint f(void);\n",
              INL_EXTERN_INLINE, "plain decl AFTER flips");
    inline_is(t, "int f(void);\ninline int f(void) { return 1; }\n",
              INL_EXTERN_INLINE, "plain decl BEFORE flips");
    inline_is(t, "inline int f(void) { return 1; }\nextern int f(void);\n",
              INL_EXTERN_INLINE, "extern decl after flips");
    inline_is(t,
              "extern inline int f(void);\ninline int f(void) { return 1; }\n",
              INL_EXTERN_INLINE, "extern inline decl then inline def");
    inline_is(t, "inline int f(void);\ninline int f(void) { return 1; }\n",
              INL_INLINE_DEF, "inline decl + inline def stays inline-def");
    inline_is(t, "inline int f(void) { return 1; }\ninline int f(void);\n",
              INL_INLINE_DEF, "and in the other order");
    /* Declared inline, never defined: calls bind elsewhere; nothing to
     * emit and nothing to suppress. */
    inline_is(t, "inline int f(void);\nint g(void) { return f(); }\n", INL_NONE,
              "inline declaration without definition");

    /* GNU89 semantics invert the two external-definition rows while a
     * static inline remains an ordinary internal definition. The actual
     * definition's storage class wins over unrelated prior prototypes. */
    inline_is(t,
              "extern inline __attribute((gnu_inline)) int f(void) "
              "{ return 1; }\n",
              INL_INLINE_DEF, "GNU extern inline does not emit");
    inline_is(t,
              "int f(void);\nextern inline __attribute((gnu_inline)) "
              "int f(void) { return 1; }\n",
              INL_INLINE_DEF, "prior prototype does not flip GNU extern");
    inline_is(t,
              "inline __attribute((gnu_inline)) int f(void) "
              "{ return 1; }\n",
              INL_EXTERN_INLINE, "GNU plain inline emits");
    inline_is(t,
              "static inline __attribute((gnu_inline)) int f(void) "
              "{ return 1; }\n",
              INL_STATIC, "GNU static inline stays internal");

    run16_std(&f, "extern inline int f(void) { return 1; }\n", true, STD_GNU89);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t,
             sym16(&f, "f") && sym16(&f, "f")->inline_kind == INL_INLINE_DEF);
    s16_free(&f);

    run16_std(&f, "inline int f(void) { return 1; }\n", true, STD_GNU89);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, sym16(&f, "f") &&
                    sym16(&f, "f")->inline_kind == INL_EXTERN_INLINE);
    s16_free(&f);

    run16(&f, "__attribute((gnu_inline)) int f(void);\n", true);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 1);
    T_ASSERT(t, sym16(&f, "f") && !sym16(&f, "f")->gnu.gnu_inline);
    s16_free(&f);

    run16(&f,
          "inline int f(void);\n"
          "extern inline __attribute((gnu_inline)) int f(void) "
          "{ return 1; }\n",
          true);
    T_ASSERT(t, f.errors > 0);
    s16_free(&f);
}

void test_s16_tentative_resolution(TestCtx *t)
{
    S16Fix f;
    Symbol *sym;

    /* External tentative, -fcommon: a COMMON symbol. */
    run16(&f, "int x;\nint x;\n", true);
    sym = sym16(&f, "x");
    T_ASSERT(t, sym && sym->def_kind == DEF_COMMON);
    s16_free(&f);

    /* The same TU under -fno-common: a zero-initialized definition. */
    run16(&f, "int x;\nint x;\n", false);
    sym = sym16(&f, "x");
    T_ASSERT(t, sym && sym->def_kind == DEF_ZERO_INIT);
    s16_free(&f);

    /* Internal tentatives are never COMMON, whatever the flag says. */
    run16(&f, "static int x;\n", true);
    sym = sym16(&f, "x");
    T_ASSERT(t, sym && sym->def_kind == DEF_ZERO_INIT);
    s16_free(&f);

    /* A real definition wins over any number of tentatives. */
    run16(&f, "int x;\nint x = 3;\nint x;\n", true);
    sym = sym16(&f, "x");
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, sym && sym->def_kind == DEF_INIT);
    s16_free(&f);

    /* A pure extern declaration defines nothing anywhere. */
    run16(&f, "extern int x;\n", true);
    sym = sym16(&f, "x");
    T_ASSERT(t, sym && sym->def_kind == DEF_NONE);
    s16_free(&f);

    /* `int a[];` completes to one element at end of TU, with the gcc
     * warning; the COMMON decision applies to the completed type. */
    run16(&f, "int a[];\n", true);
    sym = sym16(&f, "a");
    T_ASSERT(t, f.warnings >= 1);
    T_ASSERT(t, sym && sym->type->kind == TY_ARRAY && sym->type->has_size);
    T_ASSERT_EQ_INT(t, (int)sym->type->size, 1);
    T_ASSERT(t, sym->def_kind == DEF_COMMON);
    s16_free(&f);

    /* 6.9.2p3: an internal tentative still incomplete at end of TU. */
    run16(&f, "struct S;\nstatic struct S st;\n", true);
    T_ASSERT(t, f.errors >= 1);
    s16_free(&f);
}

void test_s16_vm_classifier(TestCtx *t)
{
    S16Fix f;

    /* The classifier's truth table, observed through the constraints:
     * a VM type is a VLA anywhere in the derivation chain. */
    static const struct {
        const char *src;
        bool is_error;
        const char *why;
    } rows[] = {
        {"int n; int a[n];", true, "VLA itself at file scope"},
        {"int n; int (*p)[n];", true, "pointer TO a VLA is VM"},
        {"int n; int a[n][3];", true, "VLA of arrays"},
        {"int n; int a[3][n];", true, "array of VLAs"},
        {"int a[3];", false, "constant bound is not VM"},
        {"int (*p)[3];", false, "pointer to constant array"},
        {"enum { N = 4 }; int a[N];", false, "enum constant bound is ICE"},
        {"int a[sizeof(int)];", false, "sizeof bound is ICE"},
        {"void f(int n){ int a[n]; (void)a; }", false,
         "automatic VLA is the LEGAL case"},
        {"void f(int n){ int (*p)[n]; (void)p; }", false,
         "automatic VM pointer is legal"},
    };
    u32 i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        char src[256];

        snprintf(src, sizeof(src), "%s\n", rows[i].src);
        run16(&f, src, true);
        if ((f.errors > 0) != rows[i].is_error)
            t_fail(t, __FILE__, __LINE__, "%s: errors=%d, want %s", rows[i].src,
                   f.errors, rows[i].why);
        t->assertions++;
        s16_free(&f);
    }
}

void test_s16_kr_and_main(TestCtx *t)
{
    S16Fix f;
    Symbol *sym;

    /* The K&R float trap warns; the promoted-compatible shape does not. */
    run16(&f, "void f(float);\nvoid f(x) float x; { (void)x; }\n", true);
    T_ASSERT(t, f.warnings >= 1);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    s16_free(&f);

    run16(&f,
          "void f(int, double);\nvoid f(i, d) int i; double d; "
          "{ (void)i; (void)d; }\n",
          true);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    s16_free(&f);

    /* Parameter-count mismatch between prototype and K&R list. */
    run16(&f, "void f(int);\nvoid f(a, b) int a, b; { (void)a; (void)b; }\n",
          true);
    T_ASSERT(t, f.errors >= 1);
    s16_free(&f);

    /* main is recognized and marked; its implicit return 0 is Sprint
     * 18's lowering, keyed off this bit. */
    run16(&f, "int main(void) { return 0; }\n", true);
    sym = sym16(&f, "main");
    T_ASSERT(t, sym && sym->is_main);
    s16_free(&f);
}
