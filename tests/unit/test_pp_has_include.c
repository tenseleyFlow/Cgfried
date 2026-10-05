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
} HasIncludeFix;

static void has_include_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    HasIncludeFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

static void has_include_init(HasIncludeFix *f)
{
    DiagCtx *dc;
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    sink.handle = has_include_sink;
    sink.user = f;
    diag_set_sink(dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
    f->pp.warn = warn_ctx_new(&f->arena, dc);
    f->pp.include_dirs[f->pp.n_include++] = "tests/programs/pp";
}

static void has_include_free(HasIncludeFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static int run_source(HasIncludeFix *f, const char *src, const char *needle)
{
    SourceFile *sf = pp_source_add_buffer(
        &f->pp, "tests/programs/pp/has_include_unit.c", src, strlen(src));
    PpToken tok;
    int seen = 0;

    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &tok))
        if (strcmp(tok.spelling, needle) == 0)
            seen++;
    return seen;
}

void test_pp_has_include_search_and_macro_operand(TestCtx *t)
{
    HasIncludeFix f;
    const char *src =
        "#if !defined(__has_include)\nNOT_DEFINED\n#endif\n"
        "#define LOCAL_HEADER \"dep.h\"\n"
        "#if __has_include(LOCAL_HEADER)\nPROBES_OK\n#endif\n"
        "#if !__has_include(<dep.h>)\nANGLE_FAILED\n#endif\n"
        "#if __has_include(\"definitely-missing.h\")\nMISSING_TRUE\n"
        "#endif\n";

    has_include_init(&f);
    T_ASSERT_EQ_INT(t, run_source(&f, src, "PROBES_OK"), 1);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.pp.nfiles, 2); /* main file plus <built-in> only */
    T_ASSERT_EQ_INT(t, f.pp.inc_opened, 0);
    T_ASSERT_EQ_INT(t, f.pp.inc_guard_skipped, 0);
    T_ASSERT_EQ_INT(t, f.pp.inc_once_skipped, 0);
    has_include_free(&f);
}

void test_pp_has_include_probe_does_not_consume_include(TestCtx *t)
{
    HasIncludeFix f;
    const char *src = "#if __has_include(\"dep.h\")\n"
                      "#include \"dep.h\"\n"
                      "#endif\n";

    has_include_init(&f);
    T_ASSERT_EQ_INT(t, run_source(&f, src, "local_dep"), 1);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.pp.inc_opened, 1);
    has_include_free(&f);
}

void test_pp_has_include_redefine_and_undef(TestCtx *t)
{
    HasIncludeFix f;
    const char *src = "#define __has_include(x) 0\n"
                      "#if __has_include(\"dep.h\")\nOVERRIDE_FAILED\n"
                      "#else\nOVERRIDE_OK\n#endif\n"
                      "#undef __has_include\n"
                      "#if defined(__has_include)\nUNDEF_FAILED\n"
                      "#else\nUNDEF_OK\n#endif\n";

    has_include_init(&f);
    T_ASSERT_EQ_INT(t, run_source(&f, src, "OVERRIDE_OK"), 1);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    /* Redefining the builtin warns. Once replaced by the user's definition,
     * the subsequent #undef is no longer an undefinition of a builtin. */
    T_ASSERT_EQ_INT(t, f.warnings, 1);
    T_ASSERT(t, pp_macro_lookup(&f.pp, "__has_include") == NULL);
    has_include_free(&f);
}

void test_pp_has_include_malformed_operand(TestCtx *t)
{
    HasIncludeFix f;

    has_include_init(&f);
    (void)run_source(&f, "#if __has_include()\nBAD\n#endif\n", "BAD");
    T_ASSERT_EQ_INT(t, f.errors, 1);
    has_include_free(&f);
}
