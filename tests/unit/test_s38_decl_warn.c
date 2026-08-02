#include <string.h>

#include "parse/parse.h"
#include "sema/sema.h"
#include "sema/warn_stmt.h"
#include "unit.h"
#include "warn/warn.h"

VEC_DECL(S38PpVec, PpToken);

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser parser;
    Sema sema;
    LangOpts lang;
    DiagCtx *dc;
    int warnings[WARN_COUNT];
    int errors;
} S38Fix;

static void s38_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    S38Fix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    if ((d->level == DIAG_WARNING || d->level == DIAG_ERROR) &&
        d->warn_id > WARN_NONE && d->warn_id < WARN_COUNT)
        f->warnings[d->warn_id]++;
}

static void s38_run_target(S38Fix *f, const char *src, CStd std,
                           const char *const *flags, u32 nflags,
                           TargetKind target_kind)
{
    DiagSink sink = {s38_sink, f};
    SourceFile *sf;
    S38PpVec pv = {NULL, 0, 0};
    PpToken tok;
    TokenList tl;
    TargetSpec target;
    AstNode *tu;
    u32 i;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);
    f->lang.std = std;
    f->lang.gnu_mode = std >= STD_GNU89;
    f->lang.warnings = warn_ctx_new(&f->arena, f->dc);
    for (i = 0; i < nflags; i++)
        (void)warn_flag(f->lang.warnings, flags[i]);
    f->pp.warn = f->lang.warnings;
    target.kind = target_kind;

    sf = pp_source_add_buffer(&f->pp, "s38.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &tok))
        S38PpVec_push(&pv, tok);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &f->lang, target, &f->arena);
    S38PpVec_free(&pv);
    parse_init(&f->parser, &tl, &f->pp, f->dc, &f->arena, &f->lang);
    tu = parse_translation_unit(&f->parser);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &f->lang, target);
    sema_run(&f->sema, tu);
    sema_warn_translation_unit(&f->sema, tu, &f->pp);
}

static void s38_run(S38Fix *f, const char *src, CStd std,
                    const char *const *flags, u32 nflags)
{
    s38_run_target(f, src, std, flags, nflags, CGF_TARGET_X86_64_LINUX_GNU);
}

static void s38_free(S38Fix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

void test_s38_decl_unused_tracking(TestCtx *t)
{
    static const char *const flags[] = {"all", "extra"};
    S38Fix f;

    s38_run(&f, "void f(int p) { int x = 0; int y; y = 1; (void)x; }\n",
            STD_C17, flags, 2);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_UNUSED_PARAMETER], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_UNUSED_BUT_SET_VARIABLE], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_UNUSED_VARIABLE], 0);
    s38_free(&f);
}

void test_s38_decl_shadow_and_static_function(TestCtx *t)
{
    static const char *const flags[] = {"all", "shadow"};
    S38Fix f;

    s38_run(&f,
            "int g; static int helper(void) { return 1; } "
            "void f(void) { int g; }\n",
            STD_C17, flags, 2);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_SHADOW], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_UNUSED_FUNCTION], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_UNUSED_VARIABLE], 1);
    s38_free(&f);
}

void test_s38_decl_prototype_and_vla_family(TestCtx *t)
{
    static const char *const flags[] = {
        "strict-prototypes", "old-style-definition", "missing-prototypes",
        "missing-parameter-type", "vla"};
    S38Fix f;

    s38_run(&f,
            "int old(); int kr(x) { return x; } "
            "int sized(int n) { int a[n]; return sizeof a; }\n",
            STD_C17, flags, 5);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT(t, f.warnings[WARN_STRICT_PROTOTYPES] >= 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_OLD_STYLE_DEFINITION], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_MISSING_PARAMETER_TYPE], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_VLA], 1);
    T_ASSERT(t, f.warnings[WARN_MISSING_PROTOTYPES] >= 1);
    s38_free(&f);
}

void test_s38_decl_implicit_standard_policy(TestCtx *t)
{
    static const char *const explicit_flags[] = {
        "implicit-function-declaration", "implicit-int"};
    S38Fix f;

    s38_run(&f, "int f(void) { foo(); foo(); return 0; } x;\n", STD_C17, NULL,
            0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_IMPLICIT_FUNCTION_DECLARATION], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_IMPLICIT_INT], 1);
    s38_free(&f);

    s38_run(&f, "int f(void) { foo(); foo(); return 0; } x;\n", STD_C89, NULL,
            0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_IMPLICIT_FUNCTION_DECLARATION], 0);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_IMPLICIT_INT], 0);
    s38_free(&f);

    s38_run(&f, "int f(void) { foo(); foo(); return 0; } x;\n", STD_C89,
            explicit_flags, 2);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_IMPLICIT_FUNCTION_DECLARATION], 1);
    T_ASSERT_EQ_INT(t, f.warnings[WARN_IMPLICIT_INT], 1);
    s38_free(&f);

    /* Brace elision exists only inside an initializer list. A scalar cannot
     * initialize an aggregate root by silently targeting its first member. */
    s38_run(&f, "struct S { long a, b; }; struct S invalid = 1;\n", STD_C17,
            NULL, 0);
    T_ASSERT(t, f.errors >= 1);
    s38_free(&f);
}

void test_s38_cross_target_portability_warnings(TestCtx *t)
{
    static const char *const flags[] = {"all", "extra"};
    static const char src[] =
        "int cross_target(int *a, char c, unsigned u, int i, "
        "unsigned char uc) { return a[c] + (u < i) + (uc > 300) + "
        "(u >= 0xbff00000u); }\n";
    unsigned ti;

    for (ti = 0; ti < CGF_TARGET_COUNT; ti++) {
        S38Fix f;

        s38_run_target(&f, src, STD_C17, flags, 2, (TargetKind)ti);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        T_ASSERT_EQ_INT(t, f.warnings[WARN_CHAR_SUBSCRIPTS], 1);
        T_ASSERT_EQ_INT(t, f.warnings[WARN_SIGN_COMPARE], 1);
        T_ASSERT_EQ_INT(t, f.warnings[WARN_TYPE_LIMITS], 1);
        s38_free(&f);
    }
}
