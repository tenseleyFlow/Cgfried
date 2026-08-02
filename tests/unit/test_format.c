#include <string.h>

#include "parse/parse.h"
#include "sema/sema.h"
#include "unit.h"
#include "warn/format.h"
#include "warn/warn.h"

VEC_DECL(FormatPpVec, PpToken);

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
} FormatFix;

static void format_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    FormatFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    if ((d->level == DIAG_WARNING || d->level == DIAG_ERROR) &&
        d->warn_id > WARN_NONE && d->warn_id < WARN_COUNT)
        f->warnings[d->warn_id]++;
}

static void format_run_with_pedantic(FormatFix *f, TargetKind target_kind,
                                     const char *src, bool pedantic)
{
    DiagSink sink = {format_sink, f};
    SourceFile *sf;
    FormatPpVec pv = {NULL, 0, 0};
    PpToken tok;
    TokenList tl;
    TargetSpec target = {target_kind};
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);
    f->lang.std = STD_C17;
    f->lang.pedantic = pedantic;
    f->lang.warnings = warn_ctx_new(&f->arena, f->dc);
    (void)warn_flag(f->lang.warnings, "format");
    f->pp.warn = f->lang.warnings;

    sf = pp_source_add_buffer(&f->pp, "format-unit.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &tok))
        FormatPpVec_push(&pv, tok);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &f->lang, target, &f->arena);
    FormatPpVec_free(&pv);
    parse_init(&f->parser, &tl, &f->pp, f->dc, &f->arena, &f->lang);
    tu = parse_translation_unit(&f->parser);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &f->lang, target);
    sema_run(&f->sema, tu);
}

static void format_run(FormatFix *f, TargetKind target_kind, const char *src)
{
    format_run_with_pedantic(f, target_kind, src, false);
}

static void format_free(FormatFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

typedef struct {
    const char *name;
    FmtFamily family;
    u8 format;
    u8 first;
} FormatRow;

static void assert_row(TestCtx *t, TargetSpec target, const FormatRow *row,
                       bool present)
{
    FmtSpec spec = {FMT_PRINTF, 0, 0};
    bool found = warn_format_builtin_spec(target, row->name, &spec);

    T_ASSERT_EQ_INT(t, found, present);
    if (!found)
        return;
    T_ASSERT_EQ_INT(t, spec.family, row->family);
    T_ASSERT_EQ_INT(t, spec.fmt_arg, row->format);
    T_ASSERT_EQ_INT(t, spec.first_vararg, row->first);
}

void test_format_builtin_table(TestCtx *t)
{
    static const FormatRow common[] = {
        {"printf", FMT_PRINTF, 1, 2},     {"fprintf", FMT_PRINTF, 2, 3},
        {"dprintf", FMT_PRINTF, 2, 3},    {"sprintf", FMT_PRINTF, 2, 3},
        {"snprintf", FMT_PRINTF, 3, 4},   {"vprintf", FMT_PRINTF, 1, 0},
        {"vfprintf", FMT_PRINTF, 2, 0},   {"vsprintf", FMT_PRINTF, 2, 0},
        {"vdprintf", FMT_PRINTF, 2, 0},   {"vsnprintf", FMT_PRINTF, 3, 0},
        {"scanf", FMT_SCANF, 1, 2},       {"fscanf", FMT_SCANF, 2, 3},
        {"sscanf", FMT_SCANF, 2, 3},      {"vscanf", FMT_SCANF, 1, 0},
        {"vfscanf", FMT_SCANF, 2, 0},     {"vsscanf", FMT_SCANF, 2, 0},
        {"strftime", FMT_STRFTIME, 3, 0}, {"strfmon", FMT_STRFMON, 3, 4},
    };
    static const FormatRow gnu[] = {
        {"asprintf", FMT_PRINTF, 2, 3},
        {"vasprintf", FMT_PRINTF, 2, 0},
        {"syslog", FMT_PRINTF, 2, 3},
        {"vsyslog", FMT_PRINTF, 2, 0},
        {"__isoc99_scanf", FMT_SCANF, 1, 2},
        {"__isoc99_fscanf", FMT_SCANF, 2, 3},
        {"__isoc99_sscanf", FMT_SCANF, 2, 3},
        {"__isoc99_vscanf", FMT_SCANF, 1, 0},
        {"__isoc99_vfscanf", FMT_SCANF, 2, 0},
        {"__isoc99_vsscanf", FMT_SCANF, 2, 0},
    };
    static const FormatRow freebsd[] = {
        {"err", FMT_PRINTF, 2, 3},
        {"errx", FMT_PRINTF, 2, 3},
        {"warn", FMT_PRINTF, 1, 2},
        {"warnx", FMT_PRINTF, 1, 2},
    };
    size_t i;
    int tk;

    T_ASSERT(t, sizeof(common) / sizeof(common[0]) >= 18);
    for (tk = 0; tk < CGF_TARGET_COUNT; tk++) {
        TargetSpec target = {(TargetKind)tk};
        bool is_gnu =
            tk == CGF_TARGET_X86_64_LINUX_GNU || tk == CGF_TARGET_ARM64_LINUX;
        bool is_freebsd = tk == CGF_TARGET_X86_64_FREEBSD;

        for (i = 0; i < sizeof(common) / sizeof(common[0]); i++)
            assert_row(t, target, &common[i], true);
        for (i = 0; i < sizeof(gnu) / sizeof(gnu[0]); i++)
            assert_row(t, target, &gnu[i], is_gnu);
        for (i = 0; i < sizeof(freebsd) / sizeof(freebsd[0]); i++)
            assert_row(t, target, &freebsd[i], is_freebsd);
        T_ASSERT(t, !warn_format_builtin_spec(target, "not_a_format_fn", NULL));
    }
}

void test_format_builtin_calls_all_targets(TestCtx *t)
{
    static const char source[] =
        "typedef unsigned long size_t; struct tm; "
        "int printf(const char *, ...); "
        "int fprintf(void *, const char *, ...); "
        "int dprintf(int, const char *, ...); "
        "int sprintf(char *, const char *, ...); "
        "int snprintf(char *, size_t, const char *, ...); "
        "int vprintf(const char *, void *); "
        "int vfprintf(void *, const char *, void *); "
        "int vdprintf(int, const char *, void *); "
        "int vsprintf(char *, const char *, void *); "
        "int vsnprintf(char *, size_t, const char *, void *); "
        "int scanf(const char *, ...); "
        "int fscanf(void *, const char *, ...); "
        "int sscanf(const char *, const char *, ...); "
        "int vscanf(const char *, void *); "
        "int vfscanf(void *, const char *, void *); "
        "int vsscanf(const char *, const char *, void *); "
        "size_t strftime(char *, size_t, const char *, const struct tm *); "
        "long strfmon(char *, size_t, const char *, ...); "
        "int asprintf(char **, const char *, ...); "
        "int vasprintf(char **, const char *, void *); "
        "void syslog(int, const char *, ...); "
        "void vsyslog(int, const char *, void *); "
        "int __isoc99_scanf(const char *, ...); "
        "int __isoc99_fscanf(void *, const char *, ...); "
        "int __isoc99_sscanf(const char *, const char *, ...); "
        "int __isoc99_vscanf(const char *, void *); "
        "int __isoc99_vfscanf(void *, const char *, void *); "
        "int __isoc99_vsscanf(const char *, const char *, void *); "
        "void err(int, const char *, ...); void errx(int, const char *, ...); "
        "void warn(const char *, ...); void warnx(const char *, ...); "
        "void test(void) { char *out; "
        "printf(\"%r\"); fprintf(0, \"%r\"); dprintf(1, \"%r\"); "
        "sprintf(out, \"%r\"); snprintf(out, 1, \"%r\"); "
        "vprintf(\"%r\", 0); vfprintf(0, \"%r\", 0); "
        "vdprintf(1, \"%r\", 0); vsprintf(out, \"%r\", 0); "
        "vsnprintf(out, 1, \"%r\", 0); "
        "scanf(\"%r\"); fscanf(0, \"%r\"); sscanf(\"\", \"%r\"); "
        "vscanf(\"%r\", 0); vfscanf(0, \"%r\", 0); "
        "vsscanf(\"\", \"%r\", 0); strftime(out, 1, \"%Q\", 0); "
        "strfmon(out, 1, \"%r\"); "
        "asprintf(&out, \"%r\"); vasprintf(&out, \"%r\", 0); "
        "syslog(1, \"%r\"); vsyslog(1, \"%r\", 0); "
        "__isoc99_scanf(\"%r\"); __isoc99_fscanf(0, \"%r\"); "
        "__isoc99_sscanf(\"\", \"%r\"); __isoc99_vscanf(\"%r\", 0); "
        "__isoc99_vfscanf(0, \"%r\", 0); "
        "__isoc99_vsscanf(\"\", \"%r\", 0); "
        "err(1, \"%r\"); errx(1, \"%r\"); warn(\"%r\"); warnx(\"%r\"); }";
    int tk;

    for (tk = 0; tk < CGF_TARGET_COUNT; tk++) {
        FormatFix f;
        int expected = 18;

        if (tk == CGF_TARGET_X86_64_LINUX_GNU || tk == CGF_TARGET_ARM64_LINUX)
            expected += 10;
        if (tk == CGF_TARGET_X86_64_FREEBSD)
            expected += 4;
        format_run(&f, (TargetKind)tk, source);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        T_ASSERT_EQ_INT(t, f.warnings[WARN_FORMAT], expected);
        format_free(&f);
    }
}

static int format_warning_count(TargetKind target, const char *source)
{
    FormatFix f;
    int count;

    format_run(&f, target, source);
    count = f.errors ? -1 : f.warnings[WARN_FORMAT];
    format_free(&f);
    return count;
}

static int format_pedantic_warning_count(TargetKind target, const char *source)
{
    FormatFix f;
    int count;

    format_run_with_pedantic(&f, target, source, true);
    count = f.errors ? -1 : f.warnings[WARN_FORMAT];
    format_free(&f);
    return count;
}

void test_format_target_grammar_and_wchar(TestCtx *t)
{
    static const char gnu_flag[] =
        "int printf(const char *, ...); void f(void) { printf(\"%Id\", 1); }";
    static const char linux_flag[] =
        "int printf(const char *, ...); void f(void) { printf(\"%'d\", 1); }";
    static const char linux_conv[] =
        "int printf(const char *, ...); void f(void) { printf(\"%m\"); }";
    static const char strftime_gnu[] =
        "typedef unsigned long size_t; struct tm; "
        "size_t strftime(char *, size_t, const char *, const struct tm *); "
        "void f(char *b, const struct tm *t) { strftime(b, 8, \"%P\", t); }";
    static const char printf_upper_c[] =
        "int printf(const char *, ...); void f(void) { printf(\"%C\", 1); }";
    static const char printf_upper_s_signed[] =
        "int printf(const char *, ...); "
        "void f(int *p) { printf(\"%S\", p); }";
    static const char printf_upper_s_unsigned[] =
        "int printf(const char *, ...); "
        "void f(unsigned int *p) { printf(\"%S\", p); }";
    static const char strftime_flag_width[] =
        "typedef unsigned long size_t; struct tm; "
        "size_t strftime(char *, size_t, const char *, const struct tm *); "
        "void f(char *b, const struct tm *t) { strftime(b, 8, \"%_5d\", t); }";
    static const char signed_wchar[] =
        "int printf(const char *, ...); int scanf(const char *, ...); "
        "void f(int *p) { printf(\"%ls\", p); scanf(\"%ls\", p); }";
    static const char unsigned_wchar[] =
        "int printf(const char *, ...); int scanf(const char *, ...); "
        "void f(unsigned int *p) { printf(\"%ls\", p); scanf(\"%ls\", p); }";
    int tk;

    for (tk = 0; tk < CGF_TARGET_COUNT; tk++) {
        bool gnu =
            tk == CGF_TARGET_X86_64_LINUX_GNU || tk == CGF_TARGET_ARM64_LINUX;
        bool linux = gnu || tk == CGF_TARGET_X86_64_LINUX_MUSL;
        bool unsigned_wc = tk == CGF_TARGET_ARM64_LINUX;

        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, gnu_flag),
                        gnu ? 0 : 1);
        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, linux_flag),
                        linux ? 0 : 1);
        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, linux_conv),
                        linux ? 0 : 1);
        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, strftime_gnu),
                        gnu ? 0 : 1);
        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, printf_upper_c),
                        gnu ? 0 : 1);
        T_ASSERT_EQ_INT(
            t, format_warning_count((TargetKind)tk, printf_upper_s_signed),
            gnu && !unsigned_wc ? 0 : 1);
        T_ASSERT_EQ_INT(
            t, format_warning_count((TargetKind)tk, printf_upper_s_unsigned),
            gnu && unsigned_wc ? 0 : 1);
        T_ASSERT_EQ_INT(
            t,
            format_pedantic_warning_count((TargetKind)tk, strftime_flag_width),
            gnu ? 0 : 1);
        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, signed_wchar),
                        unsigned_wc ? 2 : 0);
        T_ASSERT_EQ_INT(t, format_warning_count((TargetKind)tk, unsigned_wchar),
                        unsigned_wc ? 0 : 2);
    }
}
