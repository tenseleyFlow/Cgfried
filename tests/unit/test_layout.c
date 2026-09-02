#include <string.h>

#include "parse/parse.h"
#include "sema/sema.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* Layout, bitfields and SysV classification. The truth tables here are
 * per-target where the answer differs — `long double` is the row that
 * actually varies, and it varies by TARGET rather than by architecture
 * (Apple's arm64 makes it double while arm64-linux uses binary128). */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    DiagCtx *dc;
    int errors;
    int warnings;
} LayFix;

static void lay_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    LayFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
    else if (d->level == DIAG_WARNING)
        f->warnings++;
}

VEC_DECL(PpVecY, PpToken);

static AstNode *run_lay(LayFix *f, const char *src, TargetKind target)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecY pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec spec;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = lay_sink;
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
        PpVecY_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, spec, &f->arena);
    PpVecY_free(&pv);

    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, spec);
    sema_run(&f->sema, tu);
    return tu;
}

static void lay_free(LayFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

/* Declares `struct S { ... };` and returns its layout. */
static void record_layout(LayFix *f, const char *body, TargetKind target,
                          u64 *size, u64 *align)
{
    char src[512];
    Symbol *sym;

    snprintf(src, sizeof(src), "%s\n", body);
    (void)run_lay(f, src, target);
    sym = scope_lookup(f->sema.file_scope,
                       intern_str(&f->in, intern_cstr(&f->in, "S")), NS_TAG);
    if (sym && sym->tag) {
        TypeLayout l = layout_of(&f->sema, sym->tag->type);

        *size = l.size;
        *align = l.align;
    } else {
        *size = 0;
        *align = 0;
    }
}

static void rec_is(TestCtx *t, const char *body, u64 want_size, u64 want_align)
{
    LayFix f;
    u64 size, align;

    record_layout(&f, body, CGF_TARGET_X86_64_LINUX_GNU, &size, &align);
    if (f.errors != 0)
        t_fail(t, __FILE__, __LINE__, "%s: unexpected error", body);
    else if (size != want_size || align != want_align)
        t_fail(t, __FILE__, __LINE__,
               "%s: size=%llu align=%llu, want %llu/%llu", body,
               (unsigned long long)size, (unsigned long long)align,
               (unsigned long long)want_size, (unsigned long long)want_align);
    t->assertions++;
    lay_free(&f);
}

static void rec_target_is(TestCtx *t, const char *body, TargetKind target,
                          u64 want_size, u64 want_align)
{
    LayFix f;
    u64 size, align;

    record_layout(&f, body, target, &size, &align);
    if (f.errors != 0)
        t_fail(t, __FILE__, __LINE__, "%s: %s: unexpected error",
               cgf_target_names[target], body);
    else if (size != want_size || align != want_align)
        t_fail(t, __FILE__, __LINE__,
               "%s: %s: size=%llu align=%llu, want %llu/%llu",
               cgf_target_names[target], body, (unsigned long long)size,
               (unsigned long long)align, (unsigned long long)want_size,
               (unsigned long long)want_align);
    t->assertions++;
    lay_free(&f);
}

/* --- scalar sizes -------------------------------------------------------- */

void test_layout_scalars(TestCtx *t)
{
    LayFix f;
    struct {
        TypeKind k;
        u64 size;
        u64 align;
    } rows[] = {
        {TY_BOOL, 1, 1},    {TY_CHAR, 1, 1},     {TY_SCHAR, 1, 1},
        {TY_UCHAR, 1, 1},   {TY_SHORT, 2, 2},    {TY_USHORT, 2, 2},
        {TY_INT, 4, 4},     {TY_UINT, 4, 4},     {TY_LONG, 8, 8},
        {TY_ULONG, 8, 8},   {TY_LLONG, 8, 8},    {TY_ULLONG, 8, 8},
        {TY_FLOAT, 4, 4},   {TY_DOUBLE, 8, 8},   {TY_FLOAT32, 4, 4},
        {TY_FLOAT64, 8, 8}, {TY_FLOAT32X, 8, 8},
    };
    u32 i;

    (void)run_lay(&f, "int x;\n", CGF_TARGET_X86_64_LINUX_GNU);
    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        TypeLayout l = layout_of(&f.sema, type_basic(rows[i].k));

        T_ASSERT_EQ_INT(t, (int)l.size, (int)rows[i].size);
        T_ASSERT_EQ_INT(t, (int)l.align, (int)rows[i].align);
    }
    /* A pointer is 8/8 on every target we have (all LP64). */
    {
        TypeLayout l =
            layout_of(&f.sema, type_ptr(&f.arena, type_basic(TY_INT)));

        T_ASSERT_EQ_INT(t, (int)l.size, 8);
        T_ASSERT_EQ_INT(t, (int)l.align, 8);
    }
    lay_free(&f);
}

/* THE cross-target table. This is the row that actually differs, and it
 * differs per TARGET rather than per architecture — which is exactly why
 * layout takes a TargetSpec. */
void test_layout_long_double_per_target(TestCtx *t)
{
    struct {
        TargetKind target;
        u64 size;
        u64 align;
    } rows[] = {
        {CGF_TARGET_X86_64_LINUX_GNU, 16, 16},  /* x87 80-bit, 16 stored */
        {CGF_TARGET_X86_64_LINUX_MUSL, 16, 16}, /* same psABI */
        {CGF_TARGET_X86_64_FREEBSD, 16, 16},    /* same psABI */
        {CGF_TARGET_ARM64_LINUX, 16, 16},       /* IEEE binary128, soft */
        {CGF_TARGET_ARM64_MACOS, 8, 8},         /* Apple: == double */
    };
    u32 i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        LayFix f;
        TypeLayout l;

        (void)run_lay(&f, "int x;\n", rows[i].target);
        {
            TypeKind kinds[] = {TY_LDOUBLE, TY_FLOAT64X};
            const char *names[] = {"long double", "_Float64x"};
            u32 j;

            for (j = 0; j < sizeof(kinds) / sizeof(kinds[0]); j++) {
                l = layout_of(&f.sema, type_basic(kinds[j]));
                if (l.size != rows[i].size || l.align != rows[i].align)
                    t_fail(t, __FILE__, __LINE__,
                           "%s: %s %llu/%llu, want %llu/%llu",
                           cgf_target_names[rows[i].target], names[j],
                           (unsigned long long)l.size,
                           (unsigned long long)l.align,
                           (unsigned long long)rows[i].size,
                           (unsigned long long)rows[i].align);
                t->assertions++;
            }
        }
        lay_free(&f);
    }
}

/* --- struct and union layout --------------------------------------------- */

void test_layout_records(TestCtx *t)
{
    /* Padding between members, and TAIL padding — which is part of
     * sizeof, so the array stride and sizeof can never diverge. */
    rec_is(t, "struct S { char c; };", 1, 1);
    rec_is(t, "struct S { int i; };", 4, 4);
    rec_is(t, "struct S { char c; int i; };", 8, 4);
    rec_is(t, "struct S { int i; char c; };", 8, 4);
    rec_is(t, "struct S { long l; char c; };", 16, 8);
    rec_is(t, "struct S { char a; char b; char c; };", 3, 1);
    rec_is(t, "struct S { char a; short b; char c; };", 6, 2);
    rec_is(t, "struct S { double d; int i; };", 16, 8);
    rec_is(t, "struct S { char c; double d; };", 16, 8);
    rec_is(t, "struct S { long double ld; char c; };", 32, 16);

    /* Arrays: stride is exactly sizeof(element). */
    rec_is(t, "struct S { char a[3]; };", 3, 1);
    rec_is(t, "struct S { int a[3]; };", 12, 4);
    rec_is(t, "struct S { char c; int a[2]; };", 12, 4);
    rec_is(t, "struct S { struct { long l; char c; } inner[2]; };", 32, 8);

    /* Nested records keep their own alignment. */
    rec_is(t, "struct Inner { int i; }; struct S { char c; struct Inner n; };",
           8, 4);
    rec_is(t,
           "struct Inner { double d; }; struct S { char c; struct Inner n; };",
           16, 8);

    /* Unions: the largest member, aligned to the strictest. */
    rec_is(t, "union S { char c; int i; };", 4, 4);
    rec_is(t, "union S { char c[7]; int i; };", 8, 4);
    rec_is(t, "union S { double d; long l; };", 8, 8);
    rec_is(t, "union S { char c; long double ld; };", 16, 16);

    /* Pointers. */
    rec_is(t, "struct S { char c; void *p; };", 16, 8);
    rec_is(t, "struct S { void *p; char c; };", 16, 8);

    /* _Alignas raises the record's alignment, and therefore its size. */
    rec_is(t, "struct S { _Alignas(16) int i; };", 16, 16);
    rec_is(t, "struct S { _Alignas(8) char c; int i; };", 8, 8);
}

void test_layout_zero_length_array_records(TestCtx *t)
{
    static const TargetKind targets[] = {
        CGF_TARGET_X86_64_LINUX_GNU, CGF_TARGET_ARM64_LINUX,
        CGF_TARGET_ARM64_MACOS,      CGF_TARGET_X86_64_LINUX_MUSL,
        CGF_TARGET_X86_64_FREEBSD,
    };
    u32 i;

    /* SEMA-H-06: complete GNU zero-length arrays impose alignment but no
     * storage. This is distinct from both a refused empty record and an
     * incomplete flexible array member. Keep the answer target-independent
     * across the closed target set. */
    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        rec_target_is(t, "struct S { int x[0]; };", targets[i], 0, 4);
        rec_target_is(t, "union S { int x[0]; };", targets[i], 0, 4);
    }

    /* Multiple zero-sized members retain the strictest alignment. */
    rec_is(t, "struct S { int x[0]; double y[0]; };", 0, 8);
    rec_is(t, "union S { int x[0]; double y[0]; };", 0, 8);

    /* Zero extent propagates through an enclosing member and through arrays:
     * the following int remains at offset zero, and an array's stride is the
     * element sizeof (zero here). */
    rec_is(t, "struct Z { int x[0]; }; struct S { struct Z z; int value; };", 4,
           4);
    rec_is(t, "struct Z { int x[0]; }; struct S { struct Z z[2]; };", 0, 4);

    /* Ordinary leading/trailing zero-length idioms already had the correct
     * nonzero extent and must not change. */
    rec_is(t, "struct S { int value; char tail[0]; };", 4, 4);
    rec_is(t, "struct S { char lead[0]; int value; };", 4, 4);
}

void test_layout_bitfields(TestCtx *t)
{
    /* The five worked examples, by size and alignment. */
    rec_is(t, "struct S { int a:3; int b:29; };", 4, 4);
    rec_is(t, "struct S { int a:3; int b:30; };", 8, 4);
    rec_is(t, "struct S { char a:7; int b:25; };", 4, 4);
    rec_is(t, "struct S { char a:2; char :0; char b:2; };", 2, 1);
    rec_is(t, "struct S { long a:3; char b; int c:20; };", 8, 8);

    /* A single bitfield takes its declared type's alignment when named. */
    rec_is(t, "struct S { int a:1; };", 4, 4);
    rec_is(t, "struct S { char a:1; };", 1, 1);
    rec_is(t, "struct S { long a:1; };", 8, 8);
    /* An UNNAMED bitfield contributes no alignment, so the record stays
     * byte-aligned and the field simply packs after the char. */
    rec_is(t, "struct S { char c; int :1; };", 2, 1);
    /* ...but a NAMED one imposes its declared type's alignment, which
     * changes the record's size through tail padding. */
    rec_is(t, "struct S { char c; int b:1; };", 4, 4);

    /* Consecutive fields pack into one unit until they would straddle. */
    rec_is(t, "struct S { int a:8; int b:8; int c:8; int d:8; };", 4, 4);
    rec_is(t, "struct S { int a:8; int b:8; int c:8; int d:8; int e:8; };", 8,
           4);
    rec_is(t, "struct S { char a:4; char b:4; };", 1, 1);
    rec_is(t, "struct S { char a:5; char b:5; };", 2, 1);

    /* On SysV x86-64 a zero-width union field occupies nothing and imposes
     * no alignment. Linux AAPCS64's target-specific rule is covered below. */
    rec_is(t, "union S { int :0; char c; };", 1, 1);
    rec_is(t, "union S { unsigned long :7; int x; };", 4, 4);

    /* Mixing a bitfield with an ordinary member. */
    rec_is(t, "struct S { int a:4; char c; };", 4, 4);
    rec_is(t, "struct S { char c; char a:4; };", 2, 1);
}

void test_layout_zero_width_bitfields_per_target(TestCtx *t)
{
    struct {
        TargetKind target;
        u64 size;
        u64 align;
        u64 union_size;
        u64 union_align;
    } targets[] = {
        {CGF_TARGET_X86_64_LINUX_GNU, 4, 4, 1, 1},
        {CGF_TARGET_ARM64_LINUX, 8, 8, 8, 8},
        {CGF_TARGET_ARM64_MACOS, 4, 4, 1, 1},
        {CGF_TARGET_X86_64_LINUX_MUSL, 4, 4, 1, 1},
        {CGF_TARGET_X86_64_FREEBSD, 4, 4, 1, 1},
    };
    struct {
        const char *base;
        u64 size;
        u64 align;
    } bases[] = {
        {"_Bool", 1, 1},     {"char", 1, 1},
        {"short", 2, 2},     {"unsigned short", 2, 2},
        {"int", 4, 4},       {"unsigned int", 4, 4},
        {"long", 8, 8},      {"unsigned long", 8, 8},
        {"long long", 8, 8}, {"unsigned long long", 8, 8},
    };
    char src[160];
    u32 i;

    /* SEMA-C-02 is an AAPCS64-Linux rule, not a generic ARM64 rule: Apple
     * follows the SysV-like answer for this record. Exercise the closed
     * five-target set so a future target cannot inherit either side by
     * accident. */
    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        rec_target_is(t, "struct S { long :0; int value; };", targets[i].target,
                      targets[i].size, targets[i].align);
        rec_target_is(t, "union S { long :0; char value; };", targets[i].target,
                      targets[i].union_size, targets[i].union_align);
        /* Stronger member and record alignment must still win on either
         * side of the target split. */
        rec_target_is(t, "struct S { long :0; _Alignas(16) int value; };",
                      targets[i].target, 16, 16);
        rec_target_is(t,
                      "struct S { long :0; int value; } "
                      "__attribute__((aligned(16)));", /* check_bans allow:
                                                          compiler input,
                                                          not host C */
                      targets[i].target, 16, 16);
    }

    /* Every integer base-type alignment participates in the Linux AAPCS64
     * rule, including implementation-defined non-int bitfield bases. */
    for (i = 0; i < sizeof(bases) / sizeof(bases[0]); i++) {
        snprintf(src, sizeof(src), "struct S { %s :0; char value; };",
                 bases[i].base);
        rec_target_is(t, src, CGF_TARGET_ARM64_LINUX, bases[i].size,
                      bases[i].align);
    }

    /* Start, middle, trailing, and consecutive barriers distinguish record
     * alignment from the already-correct next-member placement rule. */
    rec_target_is(t, "struct S { char lead; long :0; char value; };",
                  CGF_TARGET_ARM64_LINUX, 16, 8);
    rec_target_is(t, "struct S { char lead; long :0; char value; };",
                  CGF_TARGET_X86_64_LINUX_GNU, 9, 1);
    rec_target_is(t, "struct S { char lead; long :0; };",
                  CGF_TARGET_ARM64_LINUX, 8, 8);
    rec_target_is(t, "struct S { char lead; short :0; long :0; char value; };",
                  CGF_TARGET_ARM64_LINUX, 16, 8);
}

void test_layout_inner_pointer_aligned_attribute(TestCtx *t)
{
    LayFix f;
    Symbol *p;
    Symbol *q;
    Symbol *r;
    Symbol *rec;
    TypeLayout l;

    (void)run_lay(&f,
                  "typedef int * "
                  "__attribute__" /* check_bans allow */
                  "((aligned(16))) aligned_ptr;\n"
                  "int *"
                  "__attribute__" /* check_bans allow */
                  "((aligned(16))) *p;\n"
                  "extern aligned_ptr q;\n"
                  "extern int *q;\n"
                  "extern int *r;\n"
                  "extern aligned_ptr r;\n"
                  "struct S { __typeof__(*p) value; char tail; };\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);

    p = scope_lookup(f.sema.file_scope,
                     intern_str(&f.in, intern_cstr(&f.in, "p")), NS_ORDINARY);
    T_ASSERT(t, p != NULL);
    l = layout_of(&f.sema, p->type);
    T_ASSERT_EQ_INT(t, (int)l.size, 8);
    T_ASSERT_EQ_INT(t, (int)l.align, 8);
    T_ASSERT(t, p->type->base != NULL);
    l = layout_of(&f.sema, p->type->base);
    T_ASSERT_EQ_INT(t, (int)l.size, 8);
    T_ASSERT_EQ_INT(t, (int)l.align, 16);
    T_ASSERT(t, type_compatible(p->type->base,
                                type_ptr(&f.arena, type_basic(TY_INT))));

    q = scope_lookup(f.sema.file_scope,
                     intern_str(&f.in, intern_cstr(&f.in, "q")), NS_ORDINARY);
    r = scope_lookup(f.sema.file_scope,
                     intern_str(&f.in, intern_cstr(&f.in, "r")), NS_ORDINARY);
    T_ASSERT(t, q != NULL);
    T_ASSERT(t, r != NULL);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, q->type).align, 16);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, r->type).align, 16);

    rec = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
    T_ASSERT(t, rec != NULL);
    T_ASSERT(t, rec->tag != NULL);
    l = layout_of(&f.sema, rec->tag->type);
    T_ASSERT_EQ_INT(t, (int)l.size, 16);
    T_ASSERT_EQ_INT(t, (int)l.align, 16);
    lay_free(&f);
}

void test_layout_aligned_typedef_is_an_exact_type_property(TestCtx *t)
{
    LayFix f;
    Symbol *sym;
    TypeLayout l;

    (void)run_lay(
        &f,
        "typedef struct Tagged { char c[8]; } V "
        "__attribute__((aligned(8)));\n" /* check_bans allow */
        "typedef V W; V object;\n"
        "struct Holder { char lead; W value; char tail; };\n"
        "typedef long long Low __attribute__" /* check_bans allow */
        "((aligned(1)));\n"
        "typedef Low Four __attribute__" /* check_bans allow */
        "((aligned(4)));\n"
        "typedef Four Two __attribute__" /* check_bans allow */
        "((aligned(2)));\n"
        "typedef long long KeepLow __attribute__" /* check_bans allow */
        "((aligned(1)));\n"
        "typedef long long KeepLow;\n"
        "typedef long long Grow __attribute__" /* check_bans allow */
        "((aligned(1)));\n"
        "typedef long long Grow __attribute__" /* check_bans allow */
        "((aligned(8)));\n"
        "typedef long long KeepHigh __attribute__" /* check_bans allow */
        "((aligned(8)));\n"
        "typedef long long KeepHigh __attribute__" /* check_bans allow */
        "((aligned(1)));\n"
        "struct Tight { char lead; Low value; char tail; };\n"
        "int *__attribute__" /* check_bans allow */
        "((aligned(1))) *p;\n"
        "extern Low merged; extern long long merged;\n"
        "long long direct __attribute__" /* check_bans allow */
        "((aligned(1)));\n"
        "typedef long long High __attribute__" /* check_bans allow */
        "((aligned(16)));\n"
        "High lowered __attribute__" /* check_bans allow */
        "((aligned(1)));\n"
        "_Alignas(16) Low raised;\n"
        "extern long long redecl_a __attribute__" /* check_bans allow */
        "((aligned(1))); extern long long redecl_a;\n"
        "extern long long redecl_b; extern long long redecl_b "
        "__attribute__" /* check_bans allow */
        "((aligned(1)));\n",
        CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "V")), NS_ORDINARY);
    T_ASSERT(t, sym != NULL && sym->kind == SYM_TYPEDEF);
    l = layout_of(&f.sema, sym->type);
    T_ASSERT_EQ_INT(t, (int)l.size, 8);
    T_ASSERT_EQ_INT(t, (int)l.align, 8);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Tagged")), NS_TAG);
    T_ASSERT(t, sym != NULL && sym->tag != NULL);
    l = layout_of(&f.sema, sym->tag->type);
    T_ASSERT_EQ_INT(t, (int)l.size, 8);
    T_ASSERT_EQ_INT(t, (int)l.align, 1);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Holder")), NS_TAG);
    T_ASSERT(t, sym != NULL && sym->tag != NULL);
    l = layout_of(&f.sema, sym->tag->type);
    T_ASSERT_EQ_INT(t, (int)l.size, 24);
    T_ASSERT_EQ_INT(t, (int)l.align, 8);

    sym =
        scope_lookup(f.sema.file_scope,
                     intern_str(&f.in, intern_cstr(&f.in, "Low")), NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 1);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Four")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 4);
    sym =
        scope_lookup(f.sema.file_scope,
                     intern_str(&f.in, intern_cstr(&f.in, "Two")), NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 2);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "KeepLow")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 1);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Grow")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 8);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "KeepHigh")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 8);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "Tight")), NS_TAG);
    l = layout_of(&f.sema, sym->tag->type);
    T_ASSERT_EQ_INT(t, (int)l.size, 10);
    T_ASSERT_EQ_INT(t, (int)l.align, 1);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "p")), NS_ORDINARY);
    T_ASSERT(t, sym != NULL && sym->type != NULL && sym->type->base != NULL);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type->base).align, 1);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "merged")),
                       NS_ORDINARY);
    T_ASSERT(t, sym != NULL);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 8);
    T_ASSERT_EQ_INT(t, (int)sym->align_override, 8);

    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "direct")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 8);
    T_ASSERT_EQ_INT(t, (int)sym->align_override, 1);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "lowered")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)layout_of(&f.sema, sym->type).align, 16);
    T_ASSERT_EQ_INT(t, (int)sym->align_override, 1);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "raised")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)sym->align_override, 16);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "redecl_a")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)sym->align_override, 8);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "redecl_b")),
                       NS_ORDINARY);
    T_ASSERT_EQ_INT(t, (int)sym->align_override, 8);
    lay_free(&f);
}

void test_layout_unnamed_nonzero_bitfields_per_target(TestCtx *t)
{
    struct {
        TargetKind target;
        u64 anon_int_struct_size;
        u64 anon_int_struct_align;
        u64 anon_long_struct_size;
        u64 anon_long_struct_align;
        u64 anon_int_union_size;
        u64 anon_int_union_align;
        u64 anon_long_union_size;
        u64 anon_long_union_align;
    } rows[] = {
        {CGF_TARGET_X86_64_LINUX_GNU, 2, 1, 3, 1, 1, 1, 1, 1},
        {CGF_TARGET_ARM64_LINUX, 4, 4, 8, 8, 4, 4, 8, 8},
        {CGF_TARGET_ARM64_MACOS, 2, 1, 3, 1, 1, 1, 1, 1},
        {CGF_TARGET_X86_64_LINUX_MUSL, 2, 1, 3, 1, 1, 1, 1, 1},
        {CGF_TARGET_X86_64_FREEBSD, 2, 1, 3, 1, 1, 1, 1, 1},
    };
    u32 i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        /* SEMA-C-08 target split: only Linux AAPCS64 lets an unnamed
         * nonzero bitfield's base type raise aggregate alignment. */
        rec_target_is(t, "struct S { int :1; char value; };", rows[i].target,
                      rows[i].anon_int_struct_size,
                      rows[i].anon_int_struct_align);
        rec_target_is(t, "struct S { long :16; char value; };", rows[i].target,
                      rows[i].anon_long_struct_size,
                      rows[i].anon_long_struct_align);
        rec_target_is(t, "union S { int :1; char value; };", rows[i].target,
                      rows[i].anon_int_union_size,
                      rows[i].anon_int_union_align);
        rec_target_is(t, "union S { long :7; char value; };", rows[i].target,
                      rows[i].anon_long_union_size,
                      rows[i].anon_long_union_align);

        /* Named controls already impose their base-type alignment on every
         * target; the AAPCS64 exception must not perturb that path. */
        rec_target_is(t, "struct S { int field:1; char value; };",
                      rows[i].target, 4, 4);
        rec_target_is(t, "struct S { long field:16; char value; };",
                      rows[i].target, 8, 8);
        rec_target_is(t, "union S { int field:1; char value; };",
                      rows[i].target, 4, 4);
        rec_target_is(t, "union S { long field:7; char value; };",
                      rows[i].target, 8, 8);
    }
}

/* Bit POSITIONS, not just sizes: two of the worked examples have the same
 * size either way and differ only in where the field lands. */
void test_layout_bit_positions(TestCtx *t)
{
    LayFix f;
    Symbol *sym;
    Member *m;

    (void)run_lay(&f, "struct S { char a:7; int b:25; };\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
    T_ASSERT(t, sym != NULL);
    layout_record(&f.sema, sym->tag->type);
    m = sym->tag->members;
    T_ASSERT_EQ_INT(t, (int)m->bit_offset, 0);
    /* 7 + 25 == 32 fits the int window exactly, so b does NOT advance. */
    T_ASSERT_EQ_INT(t, (int)m->next->bit_offset, 7);
    lay_free(&f);

    (void)run_lay(&f, "struct S { long a:3; char b; int c:20; };\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
    layout_record(&f.sema, sym->tag->type);
    m = sym->tag->members;
    T_ASSERT_EQ_INT(t, (int)m->bit_offset, 0);   /* a */
    T_ASSERT_EQ_INT(t, (int)m->next->offset, 1); /* b at byte 1 */
    /* The running offset is bit 16 and 16 + 20 == 36 overflows the int
     * window, so c DOES advance — to bit 32, not bit 16. */
    T_ASSERT_EQ_INT(t, (int)m->next->next->bit_offset, 32);
    lay_free(&f);

    /* `:0` forces the next field to the next unit boundary. */
    (void)run_lay(&f, "struct S { char a:2; char :0; char b:2; };\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    sym = scope_lookup(f.sema.file_scope,
                       intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
    layout_record(&f.sema, sym->tag->type);
    m = sym->tag->members;
    T_ASSERT_EQ_INT(t, (int)m->bit_offset, 0);
    T_ASSERT_EQ_INT(t, (int)m->next->next->bit_offset, 8);
    lay_free(&f);
}

void test_layout_packed_bitfields(TestCtx *t)
{
    struct {
        TargetKind target;
        u64 zero_size;
        u64 zero_align;
    } targets[] = {
        {CGF_TARGET_X86_64_LINUX_GNU, 5, 1},
        {CGF_TARGET_ARM64_LINUX, 8, 4},
        {CGF_TARGET_ARM64_MACOS, 5, 1},
        {CGF_TARGET_X86_64_LINUX_MUSL, 5, 1},
        {CGF_TARGET_X86_64_FREEBSD, 5, 1},
    };
    u32 i;

    for (i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
        TargetKind target = targets[i].target;

        rec_target_is(t,
                      "struct S { char a; unsigned b:3; unsigned c:30; } "
                      "__attribute__((packed));", /* check_bans allow */
                      target, 6, 1);
        rec_target_is(t,
                      "struct S { unsigned half:16; unsigned long whole:32 "
                      "__attribute__((packed)); };", /* check_bans allow */
                      target, 8, 4);
        rec_target_is(t,
                      "struct S { unsigned a:31; unsigned b:31 "
                      "__attribute__((packed)); };", /* check_bans allow */
                      target, 8, 4);
        rec_target_is(t,
                      "struct S { unsigned char a:7; unsigned long b:64 "
                      "__attribute__((packed)); };", /* check_bans allow */
                      target, 9, 1);
        rec_target_is(
            t,
            "struct S { unsigned char a:7; unsigned b:30 "
            "__attribute__((packed, aligned(4))); };", /* check_bans allow */
            target, 8, 4);
        rec_target_is(t,
                      "struct S { unsigned char a:7; unsigned b:16 "
                      "__attribute__((aligned(1))); };", /* check_bans allow */
                      target, 4, 4);
        rec_target_is(
            t,
            "struct S { unsigned char a:7; unsigned b:16 "
            "__attribute__((packed, aligned(1))); };", /* check_bans allow */
            target, 3, 1);
        rec_target_is(
            t,
            "struct S { unsigned a:3; unsigned :0 "
            "__attribute__((packed)); unsigned b:3; } " /* check_bans allow */
            "__attribute__((packed));",                 /* check_bans allow */
            target, targets[i].zero_size, targets[i].zero_align);
    }

    {
        LayFix f;
        Symbol *sym;
        Member *m;

        (void)run_lay(&f,
                      "struct S { unsigned a:31; unsigned b:31 "
                      "__attribute__((packed)); };\n", /* check_bans allow */
                      CGF_TARGET_X86_64_LINUX_GNU);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        sym = scope_lookup(f.sema.file_scope,
                           intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
        T_ASSERT(t, sym != NULL);
        T_ASSERT(t, sym->tag != NULL);
        layout_record(&f.sema, sym->tag->type);
        m = sym->tag->members;
        T_ASSERT(t, m != NULL);
        T_ASSERT(t, m->next != NULL);
        T_ASSERT_EQ_INT(t, (int)m->bit_offset, 0);
        T_ASSERT_EQ_INT(t, (int)m->next->bit_offset, 31);
        T_ASSERT(t, m->next->packed);
        lay_free(&f);

        (void)run_lay(
            &f,
            "struct S { unsigned char a:7; unsigned b:16 "
            "__attribute__((aligned(1))); };\n", /* check_bans allow */
            CGF_TARGET_X86_64_LINUX_GNU);
        T_ASSERT_EQ_INT(t, f.errors, 0);
        sym = scope_lookup(f.sema.file_scope,
                           intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
        T_ASSERT(t, sym != NULL);
        T_ASSERT(t, sym->tag != NULL);
        layout_record(&f.sema, sym->tag->type);
        m = sym->tag->members;
        T_ASSERT(t, m != NULL);
        T_ASSERT(t, m->next != NULL);
        T_ASSERT_EQ_INT(t, (int)m->next->bit_offset, 8);
        lay_free(&f);
    }
}

/* --- SysV x86-64 classification ------------------------------------------ */

static int classify_src(LayFix *f, const char *body, AbiClass out[2])
{
    char src[512];
    Symbol *sym;

    snprintf(src, sizeof(src), "%s\n", body);
    (void)run_lay(f, src, CGF_TARGET_X86_64_LINUX_GNU);
    sym = scope_lookup(f->sema.file_scope,
                       intern_str(&f->in, intern_cstr(&f->in, "S")), NS_TAG);
    if (!sym || !sym->tag)
        return -2;
    return layout_classify_sysv(&f->sema, sym->tag->type, out);
}

static void classify_is(TestCtx *t, const char *body, int want_n,
                        AbiClass want0, AbiClass want1, const char *why)
{
    LayFix f;
    AbiClass cls[2];
    int n = classify_src(&f, body, cls);

    if (n != want_n || (n >= 1 && cls[0] != want0) ||
        (n >= 2 && cls[1] != want1))
        t_fail(t, __FILE__, __LINE__,
               "%s: n=%d cls=(%d,%d), want n=%d (%d,%d) [%s]", body, n,
               (int)cls[0], (int)cls[1], want_n, (int)want0, (int)want1, why);
    t->assertions++;
    lay_free(&f);
}

void test_layout_classify_sysv(TestCtx *t)
{
    /* The worked table from the sprint file. */
    classify_is(t, "struct S { double d; int i; };", 2, ABI_SSE, ABI_INTEGER,
                "double then int: xmm0 + rdi");
    classify_is(t, "struct S { long a, b, c; };", -1, ABI_NO_CLASS,
                ABI_NO_CLASS, "rule 2: over two eightbytes -> MEMORY");
    classify_is(t, "union S { double d; long l; };", 1, ABI_INTEGER,
                ABI_NO_CLASS,
                "a UNION merges every member into the SAME eightbyte, and "
                "INTEGER dominates SSE");
    classify_is(t, "struct S { float x, y; };", 1, ABI_SSE, ABI_NO_CLASS,
                "SSE merged with SSE stays SSE");
    classify_is(t, "struct S { char c[16]; };", 2, ABI_INTEGER, ABI_INTEGER,
                "an array classifies element-wise into both eightbytes");
    /* Classification is direction-neutral: this exact aggregate and a bare
     * long double are X87/X87UP. ABI lowering sends arguments to memory and
     * returns through st0. */
    classify_is(t, "struct S { long double ld; };", 2, ABI_X87, ABI_X87UP,
                "sole f80 aggregate preserves X87/X87UP");
    classify_is(t, "union S { long double ld; };", 2, ABI_X87, ABI_X87UP,
                "sole f80 union preserves X87/X87UP");
    classify_is(t, "struct S { long double ld[1]; };", 2, ABI_X87, ABI_X87UP,
                "one-element f80 array preserves X87/X87UP");
    classify_is(t,
                "struct I { long double ld; }; struct S { struct I inner; };",
                2, ABI_X87, ABI_X87UP, "nested sole f80 preserves X87/X87UP");
    classify_is(t, "struct S { int :0; long double ld; };", 2, ABI_X87,
                ABI_X87UP, "zero-width struct bitfield occupies no class");
    /* GCC 16 and Clang 22 disagree on this union: preserve GCC compatibility
     * as a separately documented, unverified psABI observation. */
    classify_is(t, "union S { int :0; long double ld; };", -1, ABI_NO_CLASS,
                ABI_NO_CLASS, "zero-width union bitfield follows GCC MEMORY");
    classify_is(t, "struct S { int a:24; int b:8; };", 1, ABI_INTEGER,
                ABI_NO_CLASS, "bitfields class as their storage unit");

    /* Scalars. */
    classify_is(t, "struct S { int i; };", 1, ABI_INTEGER, ABI_NO_CLASS,
                "integer");
    classify_is(t, "struct S { double d; };", 1, ABI_SSE, ABI_NO_CLASS,
                "double");
    classify_is(t, "struct S { float f; };", 1, ABI_SSE, ABI_NO_CLASS, "float");
    classify_is(t, "struct S { _Float32 f; };", 1, ABI_SSE, ABI_NO_CLASS,
                "_Float32 uses the binary32 SSE class");
    classify_is(t, "struct S { _Float64 f; };", 1, ABI_SSE, ABI_NO_CLASS,
                "_Float64 uses the binary64 SSE class");
    classify_is(t, "struct S { _Float32x f; };", 1, ABI_SSE, ABI_NO_CLASS,
                "_Float32x uses the binary64 SSE class");
    classify_is(t, "struct S { _Float64x f; };", 2, ABI_X87, ABI_X87UP,
                "x86 _Float64x has the x87 long-double representation");
    classify_is(t, "struct S { void *p; };", 1, ABI_INTEGER, ABI_NO_CLASS,
                "pointer is INTEGER");
    classify_is(t, "struct S { char c; };", 1, ABI_INTEGER, ABI_NO_CLASS,
                "char");

    /* Merge rules: NO_CLASS + X -> X, INTEGER + SSE -> INTEGER. */
    classify_is(t, "struct S { int i; double d; };", 2, ABI_INTEGER, ABI_SSE,
                "each eightbyte classes independently");
    classify_is(t, "struct S { int i; float f; };", 1, ABI_INTEGER,
                ABI_NO_CLASS,
                "both land in eightbyte 0: INTEGER dominates SSE");
    classify_is(t, "struct S { float f; int i; };", 1, ABI_INTEGER,
                ABI_NO_CLASS, "and the merge is order-independent");
    classify_is(t, "struct S { double a; double b; };", 2, ABI_SSE, ABI_SSE,
                "two doubles: two SSE eightbytes");
    classify_is(t, "struct S { long a; long b; };", 2, ABI_INTEGER, ABI_INTEGER,
                "two longs");
    classify_is(t, "struct S { double d; char c; };", 2, ABI_SSE, ABI_INTEGER,
                "trailing char makes eightbyte 1 INTEGER");

    /* Unions merge into the same eightbytes, which is the rule most often
     * missed. */
    classify_is(t, "union S { float f; int i; };", 1, ABI_INTEGER, ABI_NO_CLASS,
                "union: SSE merged with INTEGER -> INTEGER");
    classify_is(t, "union S { double a; double b; };", 1, ABI_SSE, ABI_NO_CLASS,
                "union of doubles stays SSE");
    classify_is(t, "union S { char c[16]; double d; };", 2, ABI_INTEGER,
                ABI_INTEGER, "the char array poisons both eightbytes");

    /* Over two eightbytes is always MEMORY on our surface. */
    classify_is(t, "struct S { char c[17]; };", -1, ABI_NO_CLASS, ABI_NO_CLASS,
                "17 bytes > 2 eightbytes");
    classify_is(t, "struct S { double a, b, c; };", -1, ABI_NO_CLASS,
                ABI_NO_CLASS, "24 bytes -> MEMORY even though all SSE");

    /* Nested aggregates classify by their FLATTENED byte ranges. */
    classify_is(t, "struct I { double d; }; struct S { struct I i; long l; };",
                2, ABI_SSE, ABI_INTEGER, "nested struct flattens");
    classify_is(t, "struct I { int a; int b; }; struct S { struct I i; };", 1,
                ABI_INTEGER, ABI_NO_CLASS, "nested ints in one eightbyte");
    classify_is(t, "struct S { float a[4]; };", 2, ABI_SSE, ABI_SSE,
                "float[4] is two SSE eightbytes");
    classify_is(t, "struct S { double d; long double ld; };", -1, ABI_NO_CLASS,
                ABI_NO_CLASS, "X87 anywhere -> MEMORY");
}

/* AAPCS64 homogeneous float aggregates: the PREDICATE lands now so Sprint
 * 19 can shape calls target-parameterized once; Sprint 48 consumes it. */
void test_layout_hfa(TestCtx *t)
{
    LayFix f;
    Symbol *sym;
    Type *base;
    int count;

    struct {
        const char *body;
        bool is_hfa;
        int count;
    } rows[] = {
        {"struct S { float a, b; };", true, 2},
        {"struct S { float a, b, c, d; };", true, 4},
        {"struct S { float a, b, c, d, e; };", false, 0}, /* > 4 members */
        {"struct S { double a, b; };", true, 2},
        {"struct S { _Float32 a, b; };", true, 2},
        {"struct S { _Float64 a, b; };", true, 2},
        {"struct S { _Float32x a, b; };", true, 2},
        {"struct S { _Float64x a, b; };", true, 2},
        {"struct S { _Float64 a; _Float32x b; };", false, 0},
        {"struct S { float a; double b; };", false, 0}, /* not homogeneous */
        {"struct S { float a; int b; };", false, 0},    /* not all float */
        {"struct S { int a, b; };", false, 0},
        {"struct S { float a[3]; };", true, 3}, /* arrays count */
        {"struct S { float a[5]; };", false, 0},
        {"struct I { float x, y; }; struct S { struct I i; float z; };", true,
         3},
        {"struct S { float a:1; float b; };", false, 0}, /* bitfields exclude */
        /* UNION members OVERLAY, so the leaf count is the MAX over members,
         * never the sum. Verified against clang --target=aarch64-linux-gnu:
         * this union is passed in s0-s2, i.e. a 3-leaf HFA. Summing gives 6
         * and wrongly rejects it — the Sprint 14 predicate had no union row
         * at all, which is how that survived. */
        {"union S { float f[3]; struct { float x, y, z; } v; };", true, 3},
        {"union S { float a; float b; };", true, 1},
        {"union S { float f[2]; double d; };", false, 0}, /* not homogeneous */
        {"union S { float f[5]; };", false, 0},           /* > 4 leaves */
        /* A union inside a struct still overlays. */
        {"struct S { union { float a; float b; } u; float c; };", true, 2},
        /* Zero-length arrays contribute no leaves (GNU extension shape). */
        {"struct S { float a, b; float z[0]; };", true, 2},
        /* An over-aligned member pads beyond the natural layout, so the
         * aggregate stops being an HFA. clang --target=aarch64-linux-gnu
         * passes this one in x0/x1 while the natural pair goes to s0/s1. */
        {"struct S { _Alignas(16) float a; float b; };", false, 0},
    };
    u32 i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        char src[512];
        bool got;

        snprintf(src, sizeof(src), "%s\n", rows[i].body);
        (void)run_lay(&f, src, CGF_TARGET_ARM64_LINUX);
        sym = scope_lookup(f.sema.file_scope,
                           intern_str(&f.in, intern_cstr(&f.in, "S")), NS_TAG);
        got = sym && sym->tag &&
              layout_is_hfa(&f.sema, sym->tag->type, &base, &count);
        if (got != rows[i].is_hfa || (rows[i].is_hfa && count != rows[i].count))
            t_fail(t, __FILE__, __LINE__, "%s: hfa=%d count=%d, want %d/%d",
                   rows[i].body, (int)got, count, (int)rows[i].is_hfa,
                   rows[i].count);
        t->assertions++;
        lay_free(&f);
    }

    /* _Float64x remains its own homogeneous base type on both AArch64
     * targets even though its representation changes from binary128 to
     * binary64. */
    {
        TargetKind targets[] = {CGF_TARGET_ARM64_LINUX, CGF_TARGET_ARM64_MACOS};

        for (i = 0; i < sizeof(targets) / sizeof(targets[0]); i++) {
            bool got;

            (void)run_lay(&f, "struct S { _Float64x a, b; };\n", targets[i]);
            sym = scope_lookup(f.sema.file_scope,
                               intern_str(&f.in, intern_cstr(&f.in, "S")),
                               NS_TAG);
            got = sym && sym->tag &&
                  layout_is_hfa(&f.sema, sym->tag->type, &base, &count);
            if (!got || count != 2 || !base || base->kind != TY_FLOAT64X)
                t_fail(t, __FILE__, __LINE__,
                       "%s: _Float64x hfa=%d count=%d base=%d, want 1/2/%d",
                       cgf_target_names[targets[i]], (int)got, count,
                       base ? (int)base->kind : -1, (int)TY_FLOAT64X);
            t->assertions++;
            lay_free(&f);
        }
    }
}

/* Layout is a PURE function of (Type, TargetSpec): laying the same source
 * out twice must give identical answers, and laying it out for a
 * different target must not inherit the first one's memo. */
void test_layout_is_deterministic_and_per_target(TestCtx *t)
{
    const char *src = "struct S { char c; long double ld; int i; };";
    LayFix f;
    u64 s1, a1, s2, a2, s3, a3;

    record_layout(&f, src, CGF_TARGET_X86_64_LINUX_GNU, &s1, &a1);
    lay_free(&f);
    record_layout(&f, src, CGF_TARGET_X86_64_LINUX_GNU, &s2, &a2);
    lay_free(&f);
    T_ASSERT_EQ_INT(t, (int)s1, (int)s2);
    T_ASSERT_EQ_INT(t, (int)a1, (int)a2);

    /* arm64-macos makes long double 8 bytes, so the SAME source lays out
     * differently — the memo must be keyed on the target, not just the
     * tag. */
    record_layout(&f, src, CGF_TARGET_ARM64_MACOS, &s3, &a3);
    lay_free(&f);
    T_ASSERT(t, s3 != s1);
    T_ASSERT_EQ_INT(t, (int)s1, 48);
    T_ASSERT_EQ_INT(t, (int)a1, 16);
    T_ASSERT_EQ_INT(t, (int)s3, 24);
    T_ASSERT_EQ_INT(t, (int)a3, 8);
}

void test_layout_incomplete_and_errors(TestCtx *t)
{
    LayFix f;

    /* sizeof an incomplete type is an error, never a zero size. */
    (void)run_lay(&f, "struct I; int n = sizeof(struct I);\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    lay_free(&f);

    /* GNU gives void a size of one.  Sprint 38 accepts that extension and
     * diagnoses it only when -Wpointer-arith/-pedantic requests a warning. */
    (void)run_lay(&f, "int n = sizeof(void);\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    lay_free(&f);

    /* A bitfield wider than its declared type is a constraint violation;
     * one merely wider than the value range (int : 31) is fine. */
    (void)run_lay(&f, "struct S { int a:33; };\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    lay_free(&f);

    (void)run_lay(&f, "struct S { int a:31; };\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    lay_free(&f);

    /* A NAMED zero-width bitfield is meaningless and rejected. */
    (void)run_lay(&f, "struct S { int a:0; };\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    lay_free(&f);

    /* A struct with no NAMED member is the GNU size-0 extension, which we
     * decline rather than inventing a size for. */
    (void)run_lay(&f, "struct S { int :0; };\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    lay_free(&f);

    /* SEMA-H-06 does not enable the broader GNU no-named-member or FAM-only
     * extensions: both remain explicitly refused. */
    (void)run_lay(&f, "struct S {};\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    lay_free(&f);

    (void)run_lay(&f, "struct S { int data[]; };\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT(t, f.errors >= 1);
    lay_free(&f);

    /* A flexible array member has no size, and the struct's size stops
     * before it — but the member's OFFSET is still legal to take. */
    (void)run_lay(&f, "struct S { int n; char data[]; };\n",
                  CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, f.errors, 0);
    lay_free(&f);
}
