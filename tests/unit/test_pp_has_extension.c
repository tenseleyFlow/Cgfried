#include <string.h>

#include "pp/pp.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    int errors;
    int warnings;
} HasExtensionFix;

typedef struct {
    int marker;
    int zero;
    int builtin_name;
} HasExtensionOut;

static void has_extension_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    HasExtensionFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

static void has_extension_init(HasExtensionFix *f)
{
    DiagCtx *dc;
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    sink.handle = has_extension_sink;
    sink.user = f;
    diag_set_sink(dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
    f->pp.warn = warn_ctx_new(&f->arena, dc);
}

static void has_extension_free(HasExtensionFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static HasExtensionOut run_source(HasExtensionFix *f, const char *src,
                                  const char *marker)
{
    SourceFile *sf = pp_source_add_buffer(
        &f->pp, "tests/programs/pp/has_extension_unit.c", src, strlen(src));
    HasExtensionOut out = {0, 0, 0};
    PpToken tok;

    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &tok)) {
        if (marker && strcmp(tok.spelling, marker) == 0)
            out.marker++;
        if (strcmp(tok.spelling, "0") == 0)
            out.zero++;
        if (strcmp(tok.spelling, "__has_extension") == 0)
            out.builtin_name++;
    }
    return out;
}

void test_pp_has_extension_zero_visibility_and_raw_operand(TestCtx *t)
{
    HasExtensionFix f;
    HasExtensionOut out;
    const char *src = "#if !defined(__has_extension)\nNOT_DEFINED\n#endif\n"
                      "#ifndef __has_extension\nNOT_VISIBLE_TO_IFDEF\n#endif\n"
                      "#if __has_extension(c_atomic)\nFALSE_CLAIM\n#endif\n"
                      "#define NOT_AN_IDENTIFIER 1 +\n"
                      "query_one __has_extension(define_target_os_macros)\n"
                      "query_two __has_extension(NOT_AN_IDENTIFIER)\n"
                      "FEATURE_QUERY_OK\n";

    has_extension_init(&f);
    out = run_source(&f, src, "FEATURE_QUERY_OK");
    T_ASSERT_EQ_INT(t, out.marker, 1);
    T_ASSERT_EQ_INT(t, out.zero, 2);
    T_ASSERT_EQ_INT(t, out.builtin_name, 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    has_extension_free(&f);
}

void test_pp_has_extension_survives_containing_macro_expansion(TestCtx *t)
{
    HasExtensionFix f;
    HasExtensionOut out;
    const char *src = "#define QUERY __has_extension\n"
                      "#define APPLY(x) __has_extension(x)\n"
                      "#if QUERY(c_atomic)\nFALSE_WRAPPED_CLAIM\n#endif\n"
                      "wrapped_one QUERY(c_atomic)\n"
                      "wrapped_two APPLY(c_atomic)\n"
                      "WRAPPED_QUERY_OK\n";

    has_extension_init(&f);
    out = run_source(&f, src, "WRAPPED_QUERY_OK");
    T_ASSERT_EQ_INT(t, out.marker, 1);
    T_ASSERT_EQ_INT(t, out.zero, 2);
    T_ASSERT_EQ_INT(t, out.builtin_name, 0);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    has_extension_free(&f);
}

void test_pp_has_extension_redefine_and_undef(TestCtx *t)
{
    HasExtensionFix f;
    HasExtensionOut out;
    const char *src = "#define __has_extension(x) 1\n"
                      "#if !__has_extension(anything)\nOVERRIDE_FAILED\n"
                      "#endif\n"
                      "#undef __has_extension\n"
                      "#if defined(__has_extension)\nUNDEF_FAILED\n#endif\n"
                      "REDEFINE_UNDEF_OK\n";

    has_extension_init(&f);
    out = run_source(&f, src, "REDEFINE_UNDEF_OK");
    T_ASSERT_EQ_INT(t, out.marker, 1);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 1);
    T_ASSERT(t, pp_macro_lookup(&f.pp, "__has_extension") == NULL);
    has_extension_free(&f);
}

void test_pp_has_extension_rejects_malformed_invocations(TestCtx *t)
{
    static const char *const cases[] = {
        "__has_extension\n",
        "__has_extension()\n",
        "__has_extension(1)\n",
        "__has_extension(feature extra)\n",
        "__has_extension(feature, other)\n",
        "#if __has_extension()\nBAD\n#endif\n",
        "#if __has_extension(1)\nBAD\n#endif\n",
        "#if __has_extension(feature extra)\nBAD\n#endif\n",
        "#if __has_extension(feature, other)\nBAD\n#endif\n",
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        HasExtensionFix f;

        has_extension_init(&f);
        (void)run_source(&f, cases[i], NULL);
        T_ASSERT_EQ_INT(t, f.errors, 1);
        has_extension_free(&f);
    }
}
