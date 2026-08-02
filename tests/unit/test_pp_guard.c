#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pp/pp.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Include-guard shape detector (Sprint 6) + #pragma once identity. */
typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    int errors;
} GFix;

static void quiet(void *user, const Diag *d, const DiagCtx *dc)
{
    GFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
}

static void gfix_init(GFix *f)
{
    DiagCtx *dc;
    DiagSink s;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    dc = diag_ctx_new(&f->arena);
    s.handle = quiet;
    s.user = f;
    diag_set_sink(dc, s);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, dc, &f->in);
    f->pp.warn = warn_ctx_new(&f->arena, dc);
}

static void gfix_free(GFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* Runs src as the main file; returns its detected guard macro (or NULL). */
static const char *guard_of(GFix *f, const char *src)
{
    SourceFile *sf;
    PpToken t;

    gfix_init(f);
    sf = pp_source_add_buffer(&f->pp, "t.h", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        ;
    pp_end(&f->pp); /* pop_frame finalizes guard_macro */
    return sf->guard_macro;
}

void test_pp_guard_positive(TestCtx *t)
{
    GFix f;
    const char *g;

    g = guard_of(&f, "#ifndef H_H\n#define H_H\nbody\n#endif\n");
    T_ASSERT(t, g != NULL);
    if (g)
        T_ASSERT_EQ_STR(t, g, "H_H");
    gfix_free(&f);

    /* gcc also recognizes #if !defined X and #if !defined(X). */
    g = guard_of(&f, "#if !defined H_H\n#define H_H\nbody\n#endif\n");
    T_ASSERT(t, g != NULL);
    gfix_free(&f);
    g = guard_of(&f, "#if !defined(H_H)\n#define H_H\nbody\n#endif\n");
    T_ASSERT(t, g != NULL);
    gfix_free(&f);

    /* Comments and blank lines before the guard are fine. */
    g = guard_of(&f, "/* c */\n\n#ifndef H_H\n#define H_H\nx\n#endif\n\n");
    T_ASSERT(t, g != NULL);
    gfix_free(&f);

    /* #undef inside the body still QUALIFIES for shape — Sprint 7's fast
     * path must re-check that the macro is still defined at reuse. */
    g = guard_of(&f, "#ifndef H_H\n#define H_H\n#undef H_H\n#endif\n");
    T_ASSERT(t, g != NULL);
    gfix_free(&f);
}

void test_pp_guard_negative(TestCtx *t)
{
    GFix f;

    /* 1: a token before the #ifndef */
    T_ASSERT(t, guard_of(&f, "int x;\n#ifndef H\n#define H\n#endif\n") == NULL);
    gfix_free(&f);
    /* 2: #define names a DIFFERENT macro */
    T_ASSERT(t, guard_of(&f, "#ifndef H\n#define OTHER\nx\n#endif\n") == NULL);
    gfix_free(&f);
    /* 3: no #define at all */
    T_ASSERT(t, guard_of(&f, "#ifndef H\nx\n#endif\n") == NULL);
    gfix_free(&f);
    /* 4: code after the #endif */
    T_ASSERT(t, guard_of(&f, "#ifndef H\n#define H\n#endif\nint after;\n") ==
                    NULL);
    gfix_free(&f);
    /* 5: #else on the guard conditional */
    T_ASSERT(t,
             guard_of(&f, "#ifndef H\n#define H\n#else\ny\n#endif\n") == NULL);
    gfix_free(&f);
    /* 6: a second top-level conditional after the guard closes */
    T_ASSERT(t,
             guard_of(&f, "#ifndef H\n#define H\n#endif\n#if 1\nz\n#endif\n") ==
                 NULL);
    gfix_free(&f);
    /* 7: #ifdef (not #ifndef) */
    T_ASSERT(t, guard_of(&f, "#ifdef H\n#define H\nx\n#endif\n") == NULL);
    gfix_free(&f);
    /* 8: unterminated (no #endif) */
    T_ASSERT(t, guard_of(&f, "#ifndef H\n#define H\nx\n") == NULL);
    gfix_free(&f);
}

/* #pragma once identity: the same file reached through a symlink and a
 * hardlink must be included ONCE; a same-named copy must not dedupe. */
void test_pp_once_identity(TestCtx *t)
{
    GFix f;
    const char *dir = "build/test-work/once-unit";
    char real[256], sym[256], hard[256], copy[256], main_c[256];
    FILE *fp;
    PpToken tok;
    int bodies = 0;
    SourceFile *sf;

    if (mkdir("build", 0777) != 0 && errno != EEXIST)
        return;
    if (mkdir("build/test-work", 0777) != 0 && errno != EEXIST)
        return;
    if (mkdir(dir, 0777) != 0 && errno != EEXIST)
        return;

    snprintf(real, sizeof(real), "%s/h.h", dir);
    snprintf(sym, sizeof(sym), "%s/h_sym.h", dir);
    snprintf(hard, sizeof(hard), "%s/h_hard.h", dir);
    snprintf(copy, sizeof(copy), "%s/h_copy.h", dir);
    snprintf(main_c, sizeof(main_c), "%s/m.c", dir);

    fp = fopen(real, "w");
    T_ASSERT(t, fp != NULL);
    if (!fp)
        return;
    fputs("#pragma once\nHDRBODY\n", fp);
    fclose(fp);
    fp = fopen(copy, "w");
    if (fp) {
        fputs("#pragma once\nHDRBODY\n", fp);
        fclose(fp);
    }
    unlink(sym);
    unlink(hard);
    if (symlink("h.h", sym) != 0 && errno != EEXIST)
        return; /* no symlink support: skip (EPERM on some filesystems) */
    if (link(real, hard) != 0 && errno != EEXIST)
        return;

    fp = fopen(main_c, "w");
    T_ASSERT(t, fp != NULL);
    if (!fp)
        return;
    /* Same file 4 ways (2x direct, symlink, hardlink) = ONE body; the
     * separate copy has its own inode = a SECOND body. */
    fputs("#include \"h.h\"\n#include \"h.h\"\n#include \"h_sym.h\"\n"
          "#include \"h_hard.h\"\n#include \"h_copy.h\"\n",
          fp);
    fclose(fp);

    gfix_init(&f);
    sf = pp_source_load(&f.pp, main_c);
    T_ASSERT(t, sf != NULL);
    if (sf) {
        pp_begin(&f.pp, sf, NULL);
        while (pp_next(&f.pp, &tok))
            if (strcmp(tok.spelling, "HDRBODY") == 0)
                bodies++;
    }
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, bodies, 2); /* deduped four-into-one, plus the copy */
    gfix_free(&f);
}

/* _Pragma destringization: ISO names exactly two escapes (\" and \\). */
void test_pp_pragma_destringize(TestCtx *t)
{
    GFix f;
    PpToken tok;
    SourceFile *sf;
    struct {
        const char *src;
        const char *want;
    } cases[] = {
        {"_Pragma(\"message x\")\n", "message"},
        {"_Pragma(L\"message y\")\n", "message"}, /* prefix stripped */
        {"_Pragma(u8\"message z\")\n", "message"},
    };
    size_t i;

    for (i = 0; i < CGF_ARRAY_LEN(cases); i++) {
        bool saw_pragma = false;

        gfix_init(&f);
        f.pp.emit_pragmas = true; /* this test exercises -E passthrough */
        sf = pp_source_add_buffer(&f.pp, "p.c", cases[i].src,
                                  strlen(cases[i].src));
        pp_begin(&f.pp, sf, NULL);
        while (pp_next(&f.pp, &tok))
            if (strcmp(tok.spelling, "pragma") == 0)
                saw_pragma = true;
        T_ASSERT(t, saw_pragma);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        gfix_free(&f);
    }

    /* Escapes: \" becomes a quote, \\ becomes one backslash. */
    {
        int quotes = 0;
        gfix_init(&f);
        f.pp.emit_pragmas = true; /* this test exercises -E passthrough */
        sf = pp_source_add_buffer(&f.pp, "p.c",
                                  "_Pragma(\"message \\\"hi\\\"\")\n", 27);
        pp_begin(&f.pp, sf, NULL);
        while (pp_next(&f.pp, &tok))
            if (tok.kind == PPTOK_STRLIT)
                quotes++;
        T_ASSERT_EQ_INT(t, f.errors, 0);
        T_ASSERT(t, quotes >= 1); /* "hi" survived as a string literal */
        gfix_free(&f);
    }

    /* Bad operands. */
    gfix_init(&f);
    sf = pp_source_add_buffer(&f.pp, "p.c", "_Pragma(42)\n", 12);
    pp_begin(&f.pp, sf, NULL);
    while (pp_next(&f.pp, &tok))
        ;
    T_ASSERT(t, f.errors >= 1);
    gfix_free(&f);

    gfix_init(&f);
    sf = pp_source_add_buffer(&f.pp, "p.c", "_Pragma \"x\"\n", 12);
    pp_begin(&f.pp, sf, NULL);
    while (pp_next(&f.pp, &tok))
        ;
    T_ASSERT(t, f.errors >= 1);
    gfix_free(&f);
}
