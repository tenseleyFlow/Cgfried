#include <string.h>

#include "parse/parse.h"
#include "sema/sema.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Conversions (C11 6.3). The UAC table is the centerpiece and runs on
 * ALL FIVE TargetSpecs — the first target-parameterized unit suite in the
 * repo, and the reason sema takes a TargetSpec instead of asking the
 * host. `char` is signed on x86_64 and UNSIGNED on arm64-linux, so a
 * cross-compile gets a different answer for the same source. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    int warnings;
} ConvFix;

static void conv_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    ConvFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

/* A Sema with no source behind it: enough for the type-level tables. */
static void conv_fix_init(ConvFix *f, TargetKind target)
{
    DiagSink sink;
    static LangOpts lang;
    TargetSpec spec;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = conv_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    memset(&lang, 0, sizeof(lang));
    lang.std = STD_C17;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    spec.kind = target;
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, spec);
}

static void conv_fix_free(ConvFix *f)
{
    intern_free(&f->in);
    arena_free_all(&f->arena);
}

static const char *tname(Type *t)
{
    static char buf[64];
    Arena tmp;
    char *s;

    arena_init(&tmp);
    s = type_to_str(&tmp, t);
    snprintf(buf, sizeof(buf), "%s", s);
    arena_free_all(&tmp);
    return buf;
}

/* --- the UAC truth table ------------------------------------------------- */

typedef struct {
    TypeKind a;
    TypeKind b;
    TypeKind want;
    const char *why;
} UacRow;

/* Every branch of 6.3.1.8. The two trap rows from the sprint file are
 * called out: on LP64 `long` vs `unsigned int` is LONG (rule d, because a
 * 64-bit long represents every unsigned int) and `long long` vs
 * `unsigned long` is UNSIGNED LONG LONG (rule e, because they are the
 * same width). "Unsigned wins" gets both of these wrong. */
static const UacRow uac_rows[] = {
    /* 1-3: floating operands, widest first. */
    {TY_LDOUBLE, TY_INT, TY_LDOUBLE, "long double pulls anything up"},
    {TY_INT, TY_LDOUBLE, TY_LDOUBLE, "and it is symmetric"},
    {TY_LDOUBLE, TY_DOUBLE, TY_LDOUBLE, "long double over double"},
    {TY_DOUBLE, TY_FLOAT, TY_DOUBLE, "double over float"},
    {TY_DOUBLE, TY_ULLONG, TY_DOUBLE, "double over any integer"},
    {TY_FLOAT, TY_INT, TY_FLOAT, "float over integer"},
    {TY_FLOAT, TY_ULONG, TY_FLOAT, "float over unsigned long too"},
    {TY_FLOAT32, TY_FLOAT, TY_FLOAT32, "interchange type wins equal format"},
    {TY_FLOAT64, TY_DOUBLE, TY_FLOAT64, "interchange type wins equal format"},
    {TY_FLOAT32X, TY_DOUBLE, TY_DOUBLE, "standard wins over extended"},
    {TY_FLOAT32X, TY_FLOAT64, TY_FLOAT64, "interchange wins over extended"},
    {TY_FLOAT32, TY_FLOAT32X, TY_FLOAT32X, "wider representation wins"},
    {TY_FLOAT64X, TY_LDOUBLE, TY_LDOUBLE, "standard wins equal format"},
    {TY_FLOAT128, TY_LDOUBLE, TY_FLOAT128, "interchange wins equal format"},
    {TY_FLOAT128, TY_FLOAT64X, TY_FLOAT128, "binary128 interchange wins"},
    {TY_FLOAT64X, TY_FLOAT32X, TY_FLOAT64X, "larger extended format wins"},

    /* 4(a): same type after promotion. */
    {TY_INT, TY_INT, TY_INT, "identical"},
    {TY_CHAR, TY_SCHAR, TY_INT, "both promote to int first"},
    {TY_SHORT, TY_SHORT, TY_INT, "promotion happens before comparison"},
    {TY_USHORT, TY_USHORT, TY_INT, "THE sign surprise: unsigned short -> int"},
    {TY_BOOL, TY_BOOL, TY_INT, "_Bool promotes to int"},
    {TY_UCHAR, TY_USHORT, TY_INT, "both fit in int"},

    /* 4(b): same signedness, higher rank. */
    {TY_INT, TY_LONG, TY_LONG, "same signedness: higher rank"},
    {TY_LONG, TY_LLONG, TY_LLONG, "same signedness: higher rank"},
    {TY_INT, TY_LLONG, TY_LLONG, "same signedness: higher rank"},
    {TY_UINT, TY_ULONG, TY_ULONG, "same signedness (unsigned): higher rank"},
    {TY_UINT, TY_ULLONG, TY_ULLONG, "same signedness (unsigned): higher rank"},
    {TY_ULONG, TY_ULLONG, TY_ULLONG, "same signedness (unsigned): higher rank"},

    /* 4(c): unsigned rank >= signed rank -> the unsigned type. */
    {TY_INT, TY_UINT, TY_UINT, "rank tie -> unsigned"},
    {TY_UINT, TY_INT, TY_UINT, "and symmetric"},
    {TY_LONG, TY_ULONG, TY_ULONG, "rank tie -> unsigned"},
    {TY_LLONG, TY_ULLONG, TY_ULLONG, "rank tie -> unsigned"},
    {TY_INT, TY_ULONG, TY_ULONG, "unsigned outranks -> unsigned"},
    {TY_INT, TY_ULLONG, TY_ULLONG, "unsigned outranks -> unsigned"},
    {TY_LONG, TY_ULLONG, TY_ULLONG, "unsigned outranks -> unsigned"},
    {TY_SHORT, TY_UINT, TY_UINT, "short promotes to int, then rank tie"},

    /* 4(d): the signed type represents every value of the unsigned one.
     * THE LP64 trap row. */
    {TY_LONG, TY_UINT, TY_LONG, "TRAP: 64-bit long holds all unsigned int"},
    {TY_UINT, TY_LONG, TY_LONG, "TRAP: and symmetric"},
    {TY_LLONG, TY_UINT, TY_LLONG, "64-bit long long holds all unsigned int"},

    /* 4(e): otherwise the unsigned counterpart of the signed type. THE
     * second trap row: same width means the signed type cannot represent
     * the unsigned one. */
    {TY_LLONG, TY_ULONG, TY_ULLONG, "TRAP: same width -> unsigned counterpart"},
    {TY_ULONG, TY_LLONG, TY_ULLONG, "TRAP: and symmetric"},
};

void test_conv_uac_all_targets(TestCtx *t)
{
    static const TargetKind targets[] = {
        CGF_TARGET_X86_64_LINUX_GNU, CGF_TARGET_ARM64_LINUX,
        CGF_TARGET_ARM64_MACOS, CGF_TARGET_X86_64_LINUX_MUSL,
        CGF_TARGET_X86_64_FREEBSD};
    u32 ti, ri;

    /* All five targets are LP64 with the same integer widths, so the UAC
     * answers agree everywhere — running the whole table on each is what
     * PROVES that rather than assuming it, and it is what will catch the
     * first non-LP64 target the moment one is added. */
    for (ti = 0; ti < sizeof(targets) / sizeof(targets[0]); ti++) {
        ConvFix f;

        conv_fix_init(&f, targets[ti]);
        for (ri = 0; ri < sizeof(uac_rows) / sizeof(uac_rows[0]); ri++) {
            const UacRow *r = &uac_rows[ri];
            Type *got =
                conv_uac_type(&f.sema, type_basic(r->a), type_basic(r->b));

            if (got->kind != r->want)
                t_fail(t, __FILE__, __LINE__,
                       "%s: uac(%s, %s) = %s, want %s (%s)",
                       cgf_target_names[targets[ti]], tname(type_basic(r->a)),
                       tname(type_basic(r->b)), tname(got),
                       tname(type_basic(r->want)), r->why);
            t->assertions++;
        }
        conv_fix_free(&f);
    }
}

/* --- integer promotions -------------------------------------------------- */

void test_conv_promotions(TestCtx *t)
{
    ConvFix f;
    Type *u35;
    Type *u40;
    Type *s40;
    Type *u3;
    Type *u32;
    Type *mixed;

    conv_fix_init(&f, CGF_TARGET_X86_64_LINUX_GNU);
    /* Everything below int's rank promotes to int, because a 32-bit int
     * represents every value of an 8- or 16-bit type either way. */
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_BOOL)) ==
                    type_basic(TY_INT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_CHAR)) ==
                    type_basic(TY_INT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_SCHAR)) ==
                    type_basic(TY_INT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_UCHAR)) ==
                    type_basic(TY_INT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_SHORT)) ==
                    type_basic(TY_INT));
    /* THE sign surprise: unsigned short promotes to SIGNED int, so
     * `us1 - us2` is a signed subtraction that really can go negative. */
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_USHORT)) ==
                    type_basic(TY_INT));
    /* Rank >= int: unchanged. */
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_INT)) ==
                    type_basic(TY_INT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_UINT)) ==
                    type_basic(TY_UINT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_LONG)) ==
                    type_basic(TY_LONG));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_ULLONG)) ==
                    type_basic(TY_ULLONG));
    /* Floating types are untouched by the INTEGER promotions. */
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_FLOAT)) ==
                    type_basic(TY_FLOAT));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_FLOAT32)) ==
                    type_basic(TY_FLOAT32));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_FLOAT64)) ==
                    type_basic(TY_FLOAT64));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_FLOAT32X)) ==
                    type_basic(TY_FLOAT32X));
    T_ASSERT(t, conv_promote_type(&f.sema, type_basic(TY_FLOAT64X)) ==
                    type_basic(TY_FLOAT64X));

    /* A GNU bit-field's effective precision, not the rank of its declared
     * base, decides the integer promotion. This is why narrow long and long
     * long fields still become signed int. At exactly int width signedness
     * selects int versus unsigned int. A wider field has a GNU extended
     * integer type whose precision is the field width, not the carrier's. */
    T_ASSERT(t, conv_promote_bitfield_type(&f.sema, type_basic(TY_ULLONG), 3,
                                           false) == type_basic(TY_INT));
    T_ASSERT(t, conv_promote_bitfield_type(&f.sema, type_basic(TY_ULONG), 31,
                                           false) == type_basic(TY_INT));
    T_ASSERT(t, conv_promote_bitfield_type(&f.sema, type_basic(TY_LONG), 32,
                                           true) == type_basic(TY_INT));
    T_ASSERT(t, conv_promote_bitfield_type(&f.sema, type_basic(TY_ULONG), 32,
                                           false) == type_basic(TY_UINT));
    u35 = conv_promote_bitfield_type(&f.sema, type_basic(TY_ULLONG), 35, false);
    u40 = conv_promote_bitfield_type(&f.sema, type_basic(TY_ULONG), 40, false);
    s40 = conv_promote_bitfield_type(&f.sema, type_basic(TY_LLONG), 40, true);
    T_ASSERT(t, u35 != type_basic(TY_ULLONG));
    T_ASSERT_EQ_INT(t, conv_int_bits(&f.sema, u35), 35);
    T_ASSERT(t, !conv_is_signed(&f.sema, u35));
    T_ASSERT(t, !type_compatible(u35, type_basic(TY_ULLONG)));
    T_ASSERT(t, type_compatible(
                    u40, conv_promote_bitfield_type(
                             &f.sema, type_basic(TY_ULLONG), 40, false)));

    mixed = conv_uac_type(&f.sema, u35, u40);
    T_ASSERT_EQ_INT(t, conv_int_bits(&f.sema, mixed), 40);
    T_ASSERT(t, !conv_is_signed(&f.sema, mixed));
    T_ASSERT(t, type_compatible(
                    conv_uac_type(&f.sema, u35, type_basic(TY_UINT)), u35));
    T_ASSERT(t, conv_uac_type(&f.sema, u40, type_basic(TY_LONG)) ==
                    type_basic(TY_LONG));
    T_ASSERT(t, type_compatible(
                    conv_uac_type(&f.sema, s40, type_basic(TY_UINT)), s40));
    mixed = conv_uac_type(&f.sema, s40, u40);
    T_ASSERT_EQ_INT(t, conv_int_bits(&f.sema, mixed), 40);
    T_ASSERT(t, !conv_is_signed(&f.sema, mixed));
    u3 = type_integer_with_precision(&f.arena, type_basic(TY_ULLONG), 3, false);
    u32 =
        type_integer_with_precision(&f.arena, type_basic(TY_ULLONG), 32, false);
    T_ASSERT(t, conv_promote_type(&f.sema, u3) == type_basic(TY_INT));
    T_ASSERT(t, conv_promote_type(&f.sema, u32) == type_basic(TY_UINT));
    conv_fix_free(&f);
}

void test_conv_floatn_target_ties(TestCtx *t)
{
    ConvFix f;
    Type *got;

    conv_fix_init(&f, CGF_TARGET_X86_64_LINUX_GNU);
    got =
        conv_uac_type(&f.sema, type_basic(TY_FLOAT64X), type_basic(TY_DOUBLE));
    T_ASSERT_EQ_INT(t, got->kind, TY_FLOAT64X);
    conv_fix_free(&f);

    conv_fix_init(&f, CGF_TARGET_ARM64_LINUX);
    got =
        conv_uac_type(&f.sema, type_basic(TY_FLOAT64X), type_basic(TY_DOUBLE));
    T_ASSERT_EQ_INT(t, got->kind, TY_FLOAT64X);
    conv_fix_free(&f);

    conv_fix_init(&f, CGF_TARGET_ARM64_MACOS);
    got =
        conv_uac_type(&f.sema, type_basic(TY_FLOAT64X), type_basic(TY_DOUBLE));
    T_ASSERT_EQ_INT(t, got->kind, TY_DOUBLE);
    conv_fix_free(&f);

    T_ASSERT(t, !type_compatible(type_basic(TY_FLOAT32), type_basic(TY_FLOAT)));
    T_ASSERT(t,
             !type_compatible(type_basic(TY_FLOAT64), type_basic(TY_DOUBLE)));
    T_ASSERT(t,
             !type_compatible(type_basic(TY_FLOAT32X), type_basic(TY_DOUBLE)));
}

/* The canonical reason sema threads a TargetSpec instead of asking the
 * host: `char` is a distinct type from both signed and unsigned char, and
 * WHICH it behaves as is the target's choice. */
void test_conv_char_signedness_per_target(TestCtx *t)
{
    ConvFix f;

    conv_fix_init(&f, CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, conv_is_signed(&f.sema, type_basic(TY_CHAR)));
    conv_fix_free(&f);

    conv_fix_init(&f, CGF_TARGET_X86_64_LINUX_MUSL);
    T_ASSERT(t, conv_is_signed(&f.sema, type_basic(TY_CHAR)));
    conv_fix_free(&f);

    conv_fix_init(&f, CGF_TARGET_X86_64_FREEBSD);
    T_ASSERT(t, conv_is_signed(&f.sema, type_basic(TY_CHAR)));
    conv_fix_free(&f);

    /* AAPCS64 makes plain char UNSIGNED on arm64-linux. Cross-compiling
     * this target from an x86 host must get the target's answer. */
    conv_fix_init(&f, CGF_TARGET_ARM64_LINUX);
    T_ASSERT(t, !conv_is_signed(&f.sema, type_basic(TY_CHAR)));
    conv_fix_free(&f);

    /* ...and Apple diverges from AAPCS64 here, which is exactly why this
     * is a per-target table and not a per-architecture one. */
    conv_fix_init(&f, CGF_TARGET_ARM64_MACOS);
    T_ASSERT(t, conv_is_signed(&f.sema, type_basic(TY_CHAR)));
    conv_fix_free(&f);

    /* signed char and unsigned char never vary, on any target. */
    conv_fix_init(&f, CGF_TARGET_ARM64_LINUX);
    T_ASSERT(t, conv_is_signed(&f.sema, type_basic(TY_SCHAR)));
    T_ASSERT(t, !conv_is_signed(&f.sema, type_basic(TY_UCHAR)));
    conv_fix_free(&f);
}

void test_conv_rank_order(TestCtx *t)
{
    /* Each signed type and its unsigned counterpart share a rank — the
     * UAC's rules (c) and (d) both depend on that being true. */
    T_ASSERT_EQ_INT(t, conv_rank(type_basic(TY_INT)),
                    conv_rank(type_basic(TY_UINT)));
    T_ASSERT_EQ_INT(t, conv_rank(type_basic(TY_LONG)),
                    conv_rank(type_basic(TY_ULONG)));
    T_ASSERT_EQ_INT(t, conv_rank(type_basic(TY_CHAR)),
                    conv_rank(type_basic(TY_SCHAR)));
    T_ASSERT(t,
             conv_rank(type_basic(TY_BOOL)) < conv_rank(type_basic(TY_CHAR)));
    T_ASSERT(t,
             conv_rank(type_basic(TY_CHAR)) < conv_rank(type_basic(TY_SHORT)));
    T_ASSERT(t,
             conv_rank(type_basic(TY_SHORT)) < conv_rank(type_basic(TY_INT)));
    T_ASSERT(t, conv_rank(type_basic(TY_INT)) < conv_rank(type_basic(TY_LONG)));
    T_ASSERT(t,
             conv_rank(type_basic(TY_LONG)) < conv_rank(type_basic(TY_LLONG)));
}

/* --- source-level behaviour ---------------------------------------------- */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    int warnings;
    Buf out;
} SrcFix;

static void src_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    SrcFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

VEC_DECL(PpVecC, PpToken);

static AstNode *run_src(SrcFix *f, const char *src, TargetKind target)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecC pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec spec;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = src_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
    intern_init(&f->in, &f->arena);
    pp_init(&f->pp, &f->arena, f->dc, &f->in);

    memset(&lang, 0, sizeof(lang));
    lang.std = STD_C17;
    lang.warnings = warn_ctx_new(&f->arena, f->dc);
    f->pp.warn = lang.warnings;
    spec.kind = target;

    sf = pp_source_add_buffer(&f->pp, "t.c", src, strlen(src));
    pp_begin(&f->pp, sf, NULL);
    while (pp_next(&f->pp, &t))
        PpVecC_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, spec, &f->arena);
    PpVecC_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, spec);
    sema_run(&f->sema, tu);
    return tu;
}

static void src_fix_free(SrcFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* The first statement of the last function, which every case below wraps
 * its expression in. */
static AstNode *first_expr(AstNode *tu)
{
    AstNode *fd = tu->decls[tu->ndecls - 1];

    if (!fd->body || fd->body->nitems == 0)
        return NULL;
    return fd->body->items[0]->lhs;
}

static void expr_type_is(TestCtx *t, const char *stmt, const char *want)
{
    SrcFix f;
    char src[512];
    AstNode *tu;
    AstNode *e;

    snprintf(src, sizeof(src),
             "int gi; long gl; unsigned int gu; unsigned long gul;\n"
             "short gs; unsigned short gus; char gc; signed char gsc;\n"
             "unsigned char guc; float gf; double gd; long long gll;\n"
             "const int gci; int *const gcip;\n"
             "_Bool gb; int *gip; const char *gcp; char *gp; void *gvp;\n"
             "int garr[4]; int gfn(int);\n"
             "void wrapper(void) { %s; }\n",
             stmt);
    tu = run_src(&f, src, CGF_TARGET_X86_64_LINUX_GNU);
    e = first_expr(tu);
    if (f.errors != 0) {
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", stmt);
    } else if (!e || !e->sem_type) {
        t_fail(t, __FILE__, __LINE__, "%s: no typed expression", stmt);
    } else {
        char *got = type_to_str(&f.arena, e->sem_type);

        if (strcmp(got, want) != 0)
            t_fail(t, __FILE__, __LINE__, "%s: type '%s', want '%s'", stmt, got,
                   want);
    }
    t->assertions++;
    src_fix_free(&f);
}

void test_conv_expression_types(TestCtx *t)
{
    /* Promotions and the UAC, observed through real expressions. */
    expr_type_is(t, "gus - gus", "int"); /* the sign surprise */
    expr_type_is(t, "gc + gc", "int");
    expr_type_is(t, "gl + gu", "long"); /* the LP64 trap row */
    expr_type_is(t, "gi + gu", "unsigned int");
    expr_type_is(t, "gll + gul", "unsigned long long"); /* the other trap */
    expr_type_is(t, "gf + gi", "float");
    expr_type_is(t, "gd + gf", "double");

    /* THE shift rule: the result is the promoted LEFT operand alone, so a
     * `long` count does not widen the result. */
    expr_type_is(t, "1 << 40L", "int");
    expr_type_is(t, "gl << 1", "long");
    expr_type_is(t, "gc << 1", "int");
    expr_type_is(t, "gus >> gul", "int");

    /* Comparisons and logical operators yield int, not _Bool — C, not
     * C++. */
    expr_type_is(t, "gi < gl", "int");
    expr_type_is(t, "gi && gl", "int");
    expr_type_is(t, "!gd", "int");
    expr_type_is(t, "gi == gu", "int");

    /* Decay is materialized at USE SITES — where an operator demands a
     * value — rather than blanket-applied. A bare `garr;` consumes
     * nothing, so it keeps its array type; that is also what makes
     * `sizeof garr` and `&garr` work without special-casing them. */
    expr_type_is(t, "garr", "int [4]");
    expr_type_is(t, "garr + 1", "int *");
    expr_type_is(t, "garr + 0", "int *");
    expr_type_is(t, "*garr", "int");
    expr_type_is(t, "garr[1]", "int");
    /* `&arr` is a pointer to the ARRAY, not to its first element — the
     * single most common confusion in this area. */
    expr_type_is(t, "&garr", "int [4] *");
    expr_type_is(t, "gfn + 0", "int (int) *"); /* decays at the use site */
    expr_type_is(t, "&gfn", "int (int) *");
    expr_type_is(t, "gfn(1)", "int");

    /* A character constant has type int in C (6.4.4.4p10). */
    expr_type_is(t, "'a'", "int");
    /* A string literal is an ARRAY of char, terminator included. */
    expr_type_is(t, "\"abc\"", "char [4]");

    /* Pointer arithmetic and difference. */
    expr_type_is(t, "gip + 1", "int *");
    expr_type_is(t, "gip - gip", "long");
    expr_type_is(t, "gip == gip", "int");

    /* Assignment yields the lhs type with qualifiers dropped. */
    expr_type_is(t, "gi = gl", "int");
    expr_type_is(t, "gvp = gip", "void *");
    /* The comma operator yields its RIGHT operand. */
    expr_type_is(t, "(gi, gl)", "long");
    /* sizeof has type size_t even though its VALUE waits for Sprint 14. */
    expr_type_is(t, "sizeof gi", "unsigned long");

    /* Conditional operator: arithmetic operands take the UAC. */
    expr_type_is(t, "gi ? gl : gi", "long");
    expr_type_is(t, "gi ? gip : gip", "int *");
    /* One side a null pointer constant: the other side's type. */
    expr_type_is(t, "gi ? gip : 0", "int *");
    /* One side void *: void * with unioned qualifiers. */
    expr_type_is(t, "gi ? gvp : gip", "void *");
}

/* --- assignment compatibility and its SEVERITIES ------------------------- */

/* The severity column is the deliverable. Several of these are constraint
 * violations that a fresh implementation would reject outright; gcc 8
 * warns, and real-world code depends on that. */
static void assign_severity(TestCtx *t, const char *stmt, int want_errors,
                            int want_warnings, const char *label)
{
    SrcFix f;
    char src[512];

    snprintf(src, sizeof(src),
             "struct A { int x; }; struct B { int x; };\n"
             "int gi; long gl; int *gip; long *glp; char *gp;\n"
             "const char *gcp; void *gvp; char gc; _Bool gb;\n"
             "struct A ga; struct B gbv; int garr[4]; const int gci = 0;\n"
             "void (*gfp)(void); void wrapper(void) { %s; }\n",
             stmt);
    (void)run_src(&f, src, CGF_TARGET_X86_64_LINUX_GNU);
    if (f.errors != want_errors || f.warnings != want_warnings)
        t_fail(t, __FILE__, __LINE__,
               "%s [%s]: %d errors / %d warnings, want %d / %d", stmt, label,
               f.errors, f.warnings, want_errors, want_warnings);
    t->assertions++;
    src_fix_free(&f);
}

void test_conv_assignment_severities(TestCtx *t)
{
    /* WARNINGS in gcc 8 — every one of these must NOT be an error. */
    assign_severity(t, "gip = glp", 0, 1, "incompatible pointer types");
    assign_severity(t, "gip = gi", 0, 1, "pointer from integer");
    assign_severity(t, "gi = gip", 0, 1, "integer from pointer");
    assign_severity(t, "gp = gcp", 0, 1, "discarded qualifier");
    assign_severity(t, "gfp = gip", 0, 1, "function ptr from object ptr");

    /* ERRORS — no plausible reinterpretation to warn about. */
    assign_severity(t, "gi = ga", 1, 0, "arithmetic from struct");
    assign_severity(t, "ga = gbv", 1, 0, "struct from incompatible struct");
    assign_severity(t, "garr = gip", 1, 0, "assignment to an array");
    assign_severity(t, "gci = 1", 1, 0, "assignment to const");

    /* ACCEPTED with no diagnostic at all. */
    assign_severity(t, "gvp = gip", 0, 0, "void* from object pointer");
    assign_severity(t, "gip = gvp", 0, 0, "object pointer from void*");
    assign_severity(t, "gip = 0", 0, 0, "null pointer constant");
    assign_severity(t, "gb = gip", 0, 0, "_Bool from a pointer");
    assign_severity(t, "gb = gl", 0, 0, "_Bool from an integer");
    assign_severity(t, "gcp = gp", 0, 0, "adding a pointee qualifier");
    assign_severity(t, "ga = ga", 0, 0, "struct from the same struct");
    assign_severity(t, "gi = gc", 0, 0, "narrowing is not a constraint");
}

/* The C FAQ program: `T **` -> `const T **` is a constraint violation,
 * because allowing it would let a const object be written through a
 * non-const path. `T **` -> `T *const *` IS legal. */
void test_conv_pointer_qualifier_depth(TestCtx *t)
{
    SrcFix f;

    (void)run_src(&f,
                  "void g(void) { const char c = 'x'; char *p;\n"
                  "  const char **cpp = &p; (void)cpp; (void)c; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    /* gcc warns rather than erroring; the point is that it DIAGNOSES. */
    T_ASSERT(t, f.errors + f.warnings >= 1);
    src_fix_free(&f);

    /* Only the TOP pointee level may gain qualifiers, and this form does
     * exactly that — it must be silent. */
    (void)run_src(&f,
                  "void g(void) { char *p; char *const *q = &p; (void)q; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    T_ASSERT_EQ_INT(t, f.warnings, 0);
    src_fix_free(&f);
}

/* Implicit conversions must be MATERIALIZED in the tree — later passes
 * read the AST and must never re-derive the rules. */
void test_conv_casts_are_materialized(TestCtx *t)
{
    SrcFix f;
    AstNode *tu;
    AstNode *e;

    tu = run_src(&f, "void g(void) { char c = 0; int i; i = c; }\n",
                 CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    e = tu->decls[0]->body->items[2]->lhs; /* the `i = c` statement */
    T_ASSERT(t, e->kind == AST_EXPR_BINARY);
    T_ASSERT(t, e->rhs->kind == AST_EXPR_CAST);
    T_ASSERT(t, e->rhs->implicit);
    T_ASSERT(t, e->rhs->sem_type == type_basic(TY_INT));
    /* ...and a conversion result is never an lvalue. */
    T_ASSERT(t, !e->rhs->is_lvalue);
    src_fix_free(&f);

    /* Array decay is materialized too. */
    tu = run_src(&f, "void g(void) { int a[4]; int *p; p = a; }\n",
                 CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    e = tu->decls[0]->body->items[2]->lhs;
    T_ASSERT(t, e->rhs->kind == AST_EXPR_CAST && e->rhs->implicit);
    T_ASSERT(t, e->rhs->sem_type->kind == TY_PTR);
    src_fix_free(&f);
}

void test_conv_lvalue_rules(TestCtx *t)
{
    SrcFix f;
    AstNode *tu;

    /* A function CALL is not an lvalue even when it returns a struct, so
     * `f().m` cannot be assigned to. */
    (void)run_src(&f,
                  "struct S { int m; }; struct S mk(void);\n"
                  "void g(void) { mk().m = 1; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    src_fix_free(&f);

    /* ...but reading it is fine. */
    (void)run_src(&f,
                  "struct S { int m; }; struct S mk(void);\n"
                  "void g(void) { int i; i = mk().m; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    src_fix_free(&f);

    /* A member of a const struct is const. */
    (void)run_src(&f,
                  "struct S { int m; };\n"
                  "void g(const struct S *p) { p->m = 1; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    src_fix_free(&f);

    /* An enum constant is a value, not an object. */
    (void)run_src(&f, "enum E { A }; void g(void) { A = 1; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    src_fix_free(&f);

    /* The result of an assignment is not an lvalue: `(a = b) = c` is an
     * error in C, unlike C++. */
    (void)run_src(&f, "void g(void) { int a, b, c; (a = b) = c; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    src_fix_free(&f);

    tu = run_src(&f, "void g(void) { int a[4]; a[1] = 1; }\n",
                 CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    (void)tu;
    src_fix_free(&f);
}

void test_conv_undeclared_suggestion(TestCtx *t)
{
    SrcFix f;

    /* "did you mean" on an undeclared IDENTIFIER — the case Sprint 12
     * could not reach, because identifiers only resolve now. */
    (void)run_src(&f, "int counter; void g(void) { countr = 1; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    src_fix_free(&f);

    /* A tag must never be suggested for a value position. */
    (void)run_src(&f, "struct blah { int x; }; void g(void) { blah = 1; }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    src_fix_free(&f);
}

void test_conv_generic_selection(TestCtx *t)
{
    /* The controlling type is taken AFTER lvalue conversion, so a
     * `const int` lvalue selects the `int` association. */
    expr_type_is(t, "_Generic(gi, int: gl, default: gi)", "long");
    /* The result is the SELECTED expression's type, lvalue-converted —
     * no promotion, so `char` stays char. */
    expr_type_is(t, "_Generic(gi, long: gl, default: gc)", "char");
    expr_type_is(t, "_Generic(gd, double: gip, default: gi)", "int *");
    /* An array decays before matching, so `int *` is what matches. */
    expr_type_is(t, "_Generic(garr, int *: gl, default: gi)", "long");
}

void test_conv_generic_qualified_associations(TestCtx *t)
{
    SrcFix f;

    /* Association types retain their declared top-level qualifiers.  The
     * controlling expression alone undergoes lvalue conversion, so these
     * qualified associations are distinct but cannot match the converted
     * unqualified controlling type. */
    expr_type_is(t, "_Generic(gci, const int: gc, int: gl)", "long");
    expr_type_is(t, "_Generic(gcip, int *const: gc, int *: gl)", "long");

    /* Actually compatible association types remain a constraint error. */
    (void)run_src(&f, "int f(void) { return _Generic(0, int: 1, int: 2); }\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    src_fix_free(&f);
}

void test_conv_generic_malformed_type_is_poison(TestCtx *t)
{
    SrcFix f;
    AstNode *tu;

    /* FE-M-05: the parser's one syntax diagnostic must not become a false
     * duplicate-int diagnostic in sema, and the following declaration must
     * remain available after the malformed association. */
    tu = run_src(&f,
                 "int f(void) { return _Generic(1, int: 1, : 2); }\n"
                 "int after;\n",
                 CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT_EQ_INT(t, tu->ndecls, 2);
    src_fix_free(&f);

    /* A poisoned earlier association must not compare compatible with a
     * later valid one merely because TY_ERROR is broadly compatible. */
    tu = run_src(&f,
                 "int f(void) { return _Generic(1, : 2, int: 1); }\n"
                 "int after;\n",
                 CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT_EQ_INT(t, tu->ndecls, 2);
    src_fix_free(&f);

    /* Nor should a missing-match diagnostic cascade from the association
     * type that the parser already rejected. */
    tu = run_src(&f,
                 "int f(void) { return _Generic(1.0, : 2, int: 1); }\n"
                 "int after;\n",
                 CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 1);
    T_ASSERT_EQ_INT(t, tu->ndecls, 2);
    src_fix_free(&f);
}
