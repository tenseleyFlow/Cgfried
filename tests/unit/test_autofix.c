#include <string.h>

#include "lex/lex.h"
#include "memsafe/autofix.h"
#include "parse/parse.h"
#include "pp/pp.h"
#include "sema/sema.h"
#include "unit.h"
#include "util/arena.h"
#include "util/intern.h"
#include "util/vec.h"
#include "warn/warn.h"

VEC_DECL(AutofixPpVec, PpToken);

typedef struct {
    Arena arena;
    Interner interner;
    Preprocessor pp;
    Parser parser;
    Sema sema;
    LangOpts lang;
    DiagCtx *dc;
    Diag seen[32];
    size_t nseen;
} AutofixFixture;

static void autofix_capture(void *user, const Diag *d, const DiagCtx *dc)
{
    AutofixFixture *f = user;

    (void)dc;
    if (f->nseen < CGF_ARRAY_LEN(f->seen))
        f->seen[f->nseen++] = *d;
}

static void autofix_run(AutofixFixture *f, const char *body,
                        const char *warning_flag)
{
    static const char prefix[] = "typedef unsigned long size_t;\n"
                                 "void *malloc(size_t);\n"
                                 "void *reallocarray(void *, size_t, size_t);\n"
                                 "char *strdup(const char *);\n"
                                 "char *strcpy(char *, const char *);\n"
                                 "char *strcat(char *, const char *);\n"
                                 "int sprintf(char *, const char *, ...);\n"
                                 "int snprintf(char *, size_t, const char *, "
                                 "...);\n";
    Buf source;
    DiagSink sink = {autofix_capture, f};
    SourceFile *sf;
    AutofixPpVec pp_tokens = {NULL, 0, 0};
    PpToken pp_tok;
    TokenList tokens;
    AstNode *tu;
    TargetSpec target = {CGF_TARGET_X86_64_LINUX_GNU};

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
    intern_init(&f->interner, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->interner);
    f->lang.std = STD_C17;
    f->lang.warnings = warn_ctx_new(&f->arena, f->dc);
    if (warning_flag)
        (void)warn_flag(f->lang.warnings, warning_flag);
    f->pp.warn = f->lang.warnings;

    buf_init(&source);
    buf_append(&source, prefix, sizeof(prefix) - 1);
    buf_append(&source, body, strlen(body));
    sf = pp_source_add_buffer(&f->pp, "autofix.c", (const char *)source.data,
                              source.len);
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &pp_tok))
        AutofixPpVec_push(&pp_tokens, pp_tok);
    tokens = lex_convert(&f->pp, pp_tokens.data, (u32)pp_tokens.len, &f->lang,
                         target, &f->arena);
    AutofixPpVec_free(&pp_tokens);
    parse_init(&f->parser, &tokens, &f->pp, f->dc, &f->arena, &f->lang);
    tu = parse_translation_unit(&f->parser);
    sema_init(&f->sema, &f->arena, f->dc, &f->interner, &f->lang, target);
    sema_run(&f->sema, tu);
    memsafe_autofix_translation_unit(f->lang.warnings, &f->sema, tu, &f->pp);
    buf_free(&source);
}

static void autofix_free(AutofixFixture *f)
{
    pp_end(&f->pp);
    intern_free(&f->interner);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static const Diag *find_warning(const AutofixFixture *f, WarnId id)
{
    size_t i;

    for (i = 0; i < f->nseen; i++)
        if (f->seen[i].warn_id == id)
            return &f->seen[i];
    return NULL;
}

void test_autofix_unbounded_copy_machine_fix_and_suppressions(TestCtx *t)
{
    AutofixFixture f;
    const Diag *d;

    autofix_run(&f,
                "void f(const char *src) { char name[64]; "
                "strcpy(name, src); }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT(t, d->fixits[0].machine_applicable);
    T_ASSERT_EQ_STR(t, d->fixits[0].insert,
                    "snprintf(name, sizeof name, \"%s\", src)");
    T_ASSERT(t, strstr(d->fixits[0].insert, "strncpy") == NULL);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *src) { char name[64]; "
                "sprintf(name, \"[%s]\", src); }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT_EQ_STR(t, d->fixits[0].insert,
                    "snprintf(name, sizeof name, \"[%s]\", src)");
    autofix_free(&f);

    autofix_run(&f,
                "void f(char *dst, const char *src) { strcpy(dst, src); }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 0);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *src) { char dst[8]; "
                "char *used = strcpy(dst, src); (void)used; }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 0);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *src) { char dst[8]; "
                "(void)sizeof(strcpy(dst, src)); }\n",
                "-Wmem-unbounded-copy");
    T_ASSERT(t, find_warning(&f, WARN_MEM_UNBOUNDED_COPY) == NULL);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *src) { char dst[8]; strcat(dst, src); }\n",
                "-Wno-mem-unbounded-copy");
    T_ASSERT_EQ_INT(t, f.nseen, 0);
    autofix_free(&f);

    autofix_run(&f,
                "#define CPY(d, s) strcpy(d, s)\n"
                "void f(const char *src) { char dst[8]; CPY(dst, src); }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT(t, !d->fixits[0].machine_applicable);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *src) { char dst[8]; strcat(dst, src); }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 0);
    autofix_free(&f);

    autofix_run(&f,
                "#define snprintf(...) ((void)0)\n"
                "void f(const char *src) { char dst[8]; strcpy(dst, src); }\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 0);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *src) { char dst[8]; strcpy(dst, src); }\n"
                "#define snprintf(...) ((void)0)\n",
                "-Wmem-unbounded-copy");
    d = find_warning(&f, WARN_MEM_UNBOUNDED_COPY);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT(t, d->fixits[0].machine_applicable);
    autofix_free(&f);
}

void test_autofix_sizeof_mismatch_and_no_fire_pairs(TestCtx *t)
{
    AutofixFixture f;
    const Diag *d;

    autofix_run(&f, "void f(void) { int *p = malloc(sizeof(p)); (void)p; }\n",
                "-Wmem-sizeof-mismatch");
    d = find_warning(&f, WARN_MEM_SIZEOF_MISMATCH);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT(t, d->fixits[0].machine_applicable);
    T_ASSERT_EQ_STR(t, d->fixits[0].insert, "sizeof *p");
    autofix_free(&f);

    autofix_run(&f, "void f(void) { int *p = malloc(sizeof(*p)); (void)p; }\n",
                "-Wmem-sizeof-mismatch");
    T_ASSERT(t, find_warning(&f, WARN_MEM_SIZEOF_MISMATCH) == NULL);
    autofix_free(&f);

    autofix_run(&f,
                "typedef const int CI;\n"
                "void f(void) { CI *p = malloc(sizeof(CI)); (void)p; }\n",
                "-Wmem-sizeof-mismatch");
    T_ASSERT(t, find_warning(&f, WARN_MEM_SIZEOF_MISMATCH) == NULL);
    autofix_free(&f);

    autofix_run(
        &f, "void f(void) { int **q = malloc(2 * sizeof(int)); (void)q; }\n",
        "-Wmem-sizeof-mismatch");
    d = find_warning(&f, WARN_MEM_SIZEOF_MISMATCH);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_STR(t, d->fixits[0].insert, "sizeof *q");
    autofix_free(&f);

    autofix_run(
        &f,
        "void f(size_t n) { int *p = malloc(sizeof(int[10]) + n); (void)p; }\n",
        "-Wmem-sizeof-mismatch");
    T_ASSERT(t, find_warning(&f, WARN_MEM_SIZEOF_MISMATCH) == NULL);
    autofix_free(&f);

    autofix_run(
        &f,
        "void f(void) { int *p; long *other; p = malloc(sizeof *other); "
        "(void)p; }\n",
        "-Wmem-sizeof-mismatch");
    d = find_warning(&f, WARN_MEM_SIZEOF_MISMATCH);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 0);
    autofix_free(&f);

    autofix_run(&f, "void f(size_t n) { void *p = malloc(n); (void)p; }\n",
                "-Wmem-sizeof-mismatch");
    T_ASSERT(t, find_warning(&f, WARN_MEM_SIZEOF_MISMATCH) == NULL);
    autofix_free(&f);
}

void test_autofix_null_check_is_advisory_and_guard_aware(TestCtx *t)
{
    AutofixFixture f;
    const Diag *d;

    autofix_run(&f, "void f(void) { int *p = malloc(sizeof(*p)); *p = 1; }\n",
                "-Wmem-strict");
    d = find_warning(&f, WARN_MEM_NULL_CHECK);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT(t, !d->fixits[0].machine_applicable);
    T_ASSERT(t, strstr(d->fixits[0].insert, "if (!p)") != NULL);
    autofix_free(&f);

    autofix_run(&f,
                "void f(void) { int *p = malloc(sizeof(\"a;b\")); *p = 1; }\n",
                "-Wmem-strict");
    d = find_warning(&f, WARN_MEM_NULL_CHECK);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    T_ASSERT_EQ_INT(t, d->fixits[0].where.col, 47);
    autofix_free(&f);

    autofix_run(&f,
                "void f(void) { int *p = malloc(sizeof(*p)); "
                "if (!p) return; *p = 1; }\n",
                "-Wmem-strict");
    T_ASSERT(t, find_warning(&f, WARN_MEM_NULL_CHECK) == NULL);
    autofix_free(&f);

    autofix_run(&f,
                "void f(void) { int *p; p = malloc(sizeof(*p)); *p = 1; }\n",
                "-Wmem-strict");
    d = find_warning(&f, WARN_MEM_NULL_CHECK);
    T_ASSERT(t, d != NULL);
    T_ASSERT(t, !d->fixits[0].machine_applicable);
    autofix_free(&f);

    autofix_run(&f,
                "void f(const char *s) { char *p = strdup(s); "
                "char c = *p; (void)c; }\n",
                "-Wmem-strict");
    d = find_warning(&f, WARN_MEM_NULL_CHECK);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    autofix_free(&f);

    autofix_run(&f,
                "int f(size_t n) { int *p = reallocarray(0, n, sizeof *p); "
                "return *p; }\n",
                "-Wmem-strict");
    d = find_warning(&f, WARN_MEM_NULL_CHECK);
    T_ASSERT(t, d != NULL);
    T_ASSERT_EQ_INT(t, d->fixit_count, 1);
    autofix_free(&f);

    autofix_run(&f,
                "void f(void) { int *p = malloc(sizeof *p); "
                "p && (*p = 1); }\n",
                "-Wmem-strict");
    T_ASSERT(t, find_warning(&f, WARN_MEM_NULL_CHECK) == NULL);
    autofix_free(&f);

    autofix_run(&f,
                "void f(void) { int *p = malloc(sizeof *p); "
                "p != 0 && (*p = 1); }\n",
                "-Wmem-strict");
    T_ASSERT(t, find_warning(&f, WARN_MEM_NULL_CHECK) == NULL);
    autofix_free(&f);

    autofix_run(&f,
                "void f(void) { int *p = malloc(sizeof *p); "
                "if (p == 0) return; *p = 1; }\n",
                "-Wmem-strict");
    T_ASSERT(t, find_warning(&f, WARN_MEM_NULL_CHECK) == NULL);
    autofix_free(&f);
}
