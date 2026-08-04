#include <stdio.h>
#include <string.h>

#include "lower/lower.h"
#include "parse/parse.h"
#include "sema/sema.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/warn.h"

/* AAPCS64 argument/return classification (Sprint 48).
 *
 * Every expectation here was checked against
 * `clang --target=aarch64-linux-gnu -O1 -S`, which is an authoritative
 * oracle for the ABI itself: AAPCS64 is a published contract, not a
 * compiler preference, so the register a composite lands in is not a
 * matter of opinion. The rows that matter most are the ones where SysV
 * and AAPCS64 DISAGREE, because those are the porting bugs:
 *
 *   - a composite over 16 bytes becomes a caller-made copy whose ADDRESS
 *     travels in ONE general register (SysV copies the pointee onto the
 *     stack instead);
 *   - a homogeneous float aggregate travels in consecutive v-regs even
 *     when SysV would have split it into INTEGER/SSE eightbytes;
 *   - a large return goes through x8, which is NOT an argument register,
 *     so x0-x7 keep carrying the real arguments unshifted. */

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
    Parser ps;
    Sema sema;
    Lower lo;
    DiagCtx *dc;
    int errors;
} AbiFix;

static void abi_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    AbiFix *f = user;

    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        f->errors++;
}

VEC_DECL(PpVecA, PpToken);

/* Parses `src` for `target` and leaves a Lower whose only live field is
 * `sema` — the classifier reads layout and the target through it and
 * touches nothing else. */
static void abi_open(AbiFix *f, const char *src, TargetKind target)
{
    DiagSink sink;
    SourceFile *sf;
    PpVecA pv = {NULL, 0, 0};
    static LangOpts lang;
    PpToken t;
    TokenList tl;
    TargetSpec spec;
    AstNode *tu;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = abi_sink;
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
        PpVecA_push(&pv, t);
    tl = lex_convert(&f->pp, pv.data, (u32)pv.len, &lang, spec, &f->arena);
    parse_init(&f->ps, &tl, &f->pp, f->dc, &f->arena, &lang);
    tu = parse_translation_unit(&f->ps);
    sema_init(&f->sema, &f->arena, f->dc, &f->in, &lang, spec);
    sema_run(&f->sema, tu);
    PpVecA_free(&pv);
    f->lo.sema = &f->sema;
}

static void abi_close(AbiFix *f)
{
    pp_end(&f->pp);
    intern_free(&f->in);
    pp_loc_free(&f->pp.loc);
    strmap_free(&f->pp.macros);
    arena_free_all(&f->arena);
}

static Type *tag_type(AbiFix *f, const char *name)
{
    Symbol *sym =
        scope_lookup(f->sema.file_scope,
                     intern_str(&f->in, intern_cstr(&f->in, name)), NS_TAG);

    return sym && sym->tag ? sym->tag->type : NULL;
}

static const char *kind_name(u8 k)
{
    switch (k) {
    case ABI_ARG_SCALAR:
        return "SCALAR";
    case ABI_ARG_EIGHTBYTES:
        return "EIGHTBYTES";
    case ABI_ARG_BYVAL:
        return "BYVAL";
    case ABI_ARG_HFA:
        return "HFA";
    }
    return "?";
}

typedef struct {
    const char *body; /* declares `struct S` or `union S` */
    u8 kind;
    u8 n;
    IrType leaf; /* HFA/EIGHTBYTES: the type of leaf 0 */
} ArgRow;

static void check_arg_rows(TestCtx *t, const ArgRow *rows, u32 nrows)
{
    u32 i;

    for (i = 0; i < nrows; i++) {
        AbiFix f;
        char src[512];
        AbiArg got;
        Type *ty;

        snprintf(src, sizeof(src), "%s\n", rows[i].body);
        abi_open(&f, src, CGF_TARGET_ARM64_LINUX);
        ty = tag_type(&f, "S");
        if (!ty) {
            t_fail(t, __FILE__, __LINE__, "%s: no tag S", rows[i].body);
            abi_close(&f);
            continue;
        }
        abi_classify_arg(&f.lo, ty, &got);
        if (got.kind != rows[i].kind || got.n != rows[i].n)
            t_fail(t, __FILE__, __LINE__, "%s: arg %s n=%u, want %s n=%u",
                   rows[i].body, kind_name(got.kind), got.n,
                   kind_name(rows[i].kind), rows[i].n);
        else if (rows[i].n && got.t[0] != rows[i].leaf)
            t_fail(t, __FILE__, __LINE__, "%s: leaf type %d, want %d",
                   rows[i].body, (int)got.t[0], (int)rows[i].leaf);
        t->assertions++;
        abi_close(&f);
    }
}

/* The six rows from the sprint's HFA table, each with the register
 * assignment clang produces spelled in the comment. */
void test_abi_aapcs64_hfa_table(TestCtx *t)
{
    static const ArgRow rows[] = {
        /* s0, s1 */
        {"struct S { float x, y; };", ABI_ARG_HFA, 2, IRT_F32},
        /* d0-d3 */
        {"struct S { double d[4]; };", ABI_ARG_HFA, 4, IRT_F64},
        /* >16B, so a pointer to a caller copy in one GPR */
        {"struct S { double d[5]; };", ABI_ARG_BYVAL, 0, 0},
        /* mixed float/double is not homogeneous; 16B, so an x-reg pair */
        {"struct S { float f; double d; };", ABI_ARG_EIGHTBYTES, 2, IRT_I64},
        /* s0-s2: a nested struct flattens */
        {"struct I { float a, b; }; struct S { struct I p; float c; };",
         ABI_ARG_HFA, 3, IRT_F32},
        /* s0-s2: union members OVERLAY, so this is three leaves not six */
        {"union S { float f[3]; struct { float x, y, z; } v; };", ABI_ARG_HFA,
         3, IRT_F32},
    };

    check_arg_rows(t, rows, (u32)CGF_ARRAY_LEN(rows));
}

/* The size ladder, where AAPCS64 and SysV part company. */
void test_abi_aapcs64_composite_sizes(TestCtx *t)
{
    static const ArgRow rows[] = {
        {"struct S { char c; };", ABI_ARG_EIGHTBYTES, 1, IRT_I64},
        {"struct S { int a; };", ABI_ARG_EIGHTBYTES, 1, IRT_I64},
        {"struct S { long a; };", ABI_ARG_EIGHTBYTES, 1, IRT_I64},
        {"struct S { long a, b; };", ABI_ARG_EIGHTBYTES, 2, IRT_I64},
        {"struct S { int a, b, c, d; };", ABI_ARG_EIGHTBYTES, 2, IRT_I64},
        /* 17 bytes: over the line, so pointer-to-copy in ONE GPR. SysV
         * would copy 24 bytes onto the stack instead. */
        {"struct S { long a, b; char c; };", ABI_ARG_BYVAL, 0, 0},
        {"struct S { char big[64]; };", ABI_ARG_BYVAL, 0, 0},
        /* An integer member anywhere kills HFA-ness. This one is 8 bytes
         * total, so it rides in ONE x-reg — clang emits a single
         * `mov x0` + `movk x0, lsl #32` packing both members. */
        {"struct S { float a; int b; };", ABI_ARG_EIGHTBYTES, 1, IRT_I64},
        /* Arrays of non-float behave as ordinary composites. */
        {"struct S { int a[4]; };", ABI_ARG_EIGHTBYTES, 2, IRT_I64},
        {"struct S { int a[8]; };", ABI_ARG_BYVAL, 0, 0},
        /* A single-member float struct is still an HFA (one leaf). */
        {"struct S { float only; };", ABI_ARG_HFA, 1, IRT_F32},
        {"struct S { double only; };", ABI_ARG_HFA, 1, IRT_F64},
        /* Bitfields exclude. */
        {"struct S { float a : 1; float b; };", ABI_ARG_EIGHTBYTES, 1, IRT_I64},
        /* Over-aligned member: padded beyond natural, so not an HFA. */
        {"struct S { _Alignas(16) float a; float b; };", ABI_ARG_EIGHTBYTES, 2,
         IRT_I64},
    };

    check_arg_rows(t, rows, (u32)CGF_ARRAY_LEN(rows));
}

/* Scalars never become plans: the IR type says everything. */
void test_abi_aapcs64_scalars(TestCtx *t)
{
    static const char *const bodies[] = {
        "struct S { int x; };", /* placeholder tag; scalars probed below */
    };
    AbiFix f;
    AbiArg got;
    static const struct {
        TypeKind k;
    } scalars[] = {{TY_CHAR},  {TY_SHORT},  {TY_INT},  {TY_LONG}, {TY_LLONG},
                   {TY_FLOAT}, {TY_DOUBLE}, {TY_BOOL}, {TY_UINT}, {TY_ULLONG}};
    u32 i;

    abi_open(&f, bodies[0], CGF_TARGET_ARM64_LINUX);
    for (i = 0; i < CGF_ARRAY_LEN(scalars); i++) {
        abi_classify_arg(&f.lo, type_basic(scalars[i].k), &got);
        T_ASSERT_EQ_INT(t, got.kind, ABI_ARG_SCALAR);
    }
    /* A pointer is a scalar too, however big the pointee. */
    abi_classify_arg(&f.lo, type_ptr(&f.arena, type_basic(TY_VOID)), &got);
    T_ASSERT_EQ_INT(t, got.kind, ABI_ARG_SCALAR);
    abi_close(&f);
}

typedef struct {
    const char *body;
    u8 kind;
    IrType leaf;
} RetRow;

/* Returns: <=8B in x0, <=16B in x0:x1, HFA in v0-v3, anything else
 * through the x8 indirect-result pointer. */
void test_abi_aapcs64_returns(TestCtx *t)
{
    static const RetRow rows[] = {
        {"struct S { char c; };", ABI_RET_SMALL, IRT_I64},
        {"struct S { long a; };", ABI_RET_SMALL, IRT_I64},
        {"struct S { long a, b; };", ABI_RET_PAIR, 0},
        {"struct S { int a, b, c, d; };", ABI_RET_PAIR, 0},
        {"struct S { long a, b; char c; };", ABI_RET_SRET, 0},
        {"struct S { char big[64]; };", ABI_RET_SRET, 0},
        {"struct S { float x, y; };", ABI_RET_HFA, IRT_F32},
        {"struct S { double d[4]; };", ABI_RET_HFA, IRT_F64},
        /* >4 leaves is not an HFA, and 40 bytes is indirect. */
        {"struct S { double d[5]; };", ABI_RET_SRET, 0},
        {"union S { float f[3]; struct { float x, y, z; } v; };", ABI_RET_HFA,
         IRT_F32},
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        AbiFix f;
        char src[512];
        AbiRet got;
        Type *ty;

        snprintf(src, sizeof(src), "%s\n", rows[i].body);
        abi_open(&f, src, CGF_TARGET_ARM64_LINUX);
        ty = tag_type(&f, "S");
        if (!ty) {
            t_fail(t, __FILE__, __LINE__, "%s: no tag S", rows[i].body);
            abi_close(&f);
            continue;
        }
        abi_classify_ret(&f.lo, ty, &got);
        if (got.kind != rows[i].kind)
            t_fail(t, __FILE__, __LINE__, "%s: ret kind %d, want %d",
                   rows[i].body, (int)got.kind, (int)rows[i].kind);
        else if (rows[i].leaf && got.small_t != rows[i].leaf)
            t_fail(t, __FILE__, __LINE__, "%s: ret leaf %d, want %d",
                   rows[i].body, (int)got.small_t, (int)rows[i].leaf);
        t->assertions++;
        abi_close(&f);
    }
    /* void and scalars. */
    {
        AbiFix f;
        AbiRet got;

        abi_open(&f, "struct S { int x; };\n", CGF_TARGET_ARM64_LINUX);
        abi_classify_ret(&f.lo, type_basic(TY_VOID), &got);
        T_ASSERT_EQ_INT(t, got.kind, ABI_RET_VOID);
        abi_classify_ret(&f.lo, NULL, &got);
        T_ASSERT_EQ_INT(t, got.kind, ABI_RET_VOID);
        abi_classify_ret(&f.lo, type_basic(TY_DOUBLE), &got);
        T_ASSERT_EQ_INT(t, got.kind, ABI_RET_SCALAR);
        abi_close(&f);
    }
}

/* The same C types classify DIFFERENTLY on the two ABIs. Asserting the
 * contrast directly is what stops a future edit from quietly making one
 * classifier serve both. */
void test_abi_aapcs64_vs_sysv_contrast(TestCtx *t)
{
    static const char *const src =
        "struct S { long a, b; char c; };\n" /* 24 bytes */
        "struct H { float x, y; };\n";       /* HFA on arm64 only */
    AbiFix f;
    AbiArg got;
    Type *ty;

    /* 24-byte composite: BYVAL on both, but the MEANING differs — arm64
     * spends one GPR on the pointer, SysV copies the pointee to stack.
     * The plan shape is shared; the size/align ride along for both. */
    abi_open(&f, src, CGF_TARGET_ARM64_LINUX);
    ty = tag_type(&f, "S");
    abi_classify_arg(&f.lo, ty, &got);
    T_ASSERT_EQ_INT(t, got.kind, ABI_ARG_BYVAL);
    T_ASSERT_EQ_INT(t, (int)got.size, 24);
    ty = tag_type(&f, "H");
    abi_classify_arg(&f.lo, ty, &got);
    T_ASSERT_EQ_INT(t, got.kind, ABI_ARG_HFA);
    T_ASSERT_EQ_INT(t, got.n, 2);
    abi_close(&f);

    /* Same sources, x86_64: the float pair becomes ONE SSE eightbyte
     * (two f32 packed into 8 bytes), never a two-leaf HFA. */
    abi_open(&f, src, CGF_TARGET_X86_64_LINUX_GNU);
    ty = tag_type(&f, "H");
    abi_classify_arg(&f.lo, ty, &got);
    T_ASSERT_EQ_INT(t, got.kind, ABI_ARG_EIGHTBYTES);
    T_ASSERT_EQ_INT(t, got.n, 1);
    T_ASSERT_EQ_INT(t, (int)got.t[0], IRT_F64);
    abi_close(&f);
}

/* abi_arg_regs is what va_start's constants and the register caps are
 * computed from, so its accounting must match the plans exactly. */
void test_abi_aapcs64_reg_accounting(TestCtx *t)
{
    AbiFix f;
    AbiArg got;
    u32 gp, fp;
    Type *ty;

    abi_open(&f,
             "struct H4 { double d[4]; };\n"
             "struct P { long a, b; };\n"
             "struct S { char big[64]; };\n",
             CGF_TARGET_ARM64_LINUX);

    ty = tag_type(&f, "H4");
    abi_classify_arg(&f.lo, ty, &got);
    gp = fp = 0;
    abi_arg_regs(&got, &gp, &fp);
    T_ASSERT_EQ_INT(t, (int)gp, 0);
    T_ASSERT_EQ_INT(t, (int)fp, 4); /* four v-regs, one per leaf */

    ty = tag_type(&f, "P");
    abi_classify_arg(&f.lo, ty, &got);
    gp = fp = 0;
    abi_arg_regs(&got, &gp, &fp);
    T_ASSERT_EQ_INT(t, (int)gp, 2);
    T_ASSERT_EQ_INT(t, (int)fp, 0);

    abi_close(&f);
}

/* The Linux AAPCS64 va_list, field for field against gcc's aarch64
 * `build_va_list`. The offsets are load-bearing: lowering hard-codes them,
 * and va_copy copies exactly this many bytes. */
void test_abi_aapcs64_va_list_shape(TestCtx *t)
{
    static const struct {
        const char *name;
        u32 offset;
    } expect[] = {
        {"__stack", 0},    {"__gr_top", 8},   {"__vr_top", 16},
        {"__gr_offs", 24}, {"__vr_offs", 28},
    };
    AbiFix f;
    Type *va, *rec;
    TypeLayout l;
    Member *m;
    u32 i = 0;

    abi_open(&f, "int x;\n", CGF_TARGET_ARM64_LINUX);
    va = sema_va_list_type(&f.sema);
    T_ASSERT(t, va != NULL);
    /* a one-element array, so every use decays to a pointer and a callee
     * advances its caller's cursor */
    T_ASSERT_EQ_INT(t, va->kind, TY_ARRAY);
    T_ASSERT_EQ_INT(t, (long long)va->size, 1);
    rec = va->base;
    T_ASSERT_EQ_INT(t, rec->kind, TY_STRUCT);
    T_ASSERT_EQ_INT(t, rec->tag->nmembers, 5);

    l = layout_of(&f.sema, rec);
    T_ASSERT_EQ_INT(t, (long long)l.size, 32);
    T_ASSERT_EQ_INT(t, (long long)l.align, 8);
    for (m = rec->tag->members; m; m = m->next, i++) {
        T_ASSERT(t, i < 5);
        T_ASSERT_EQ_STR(t, m->name, expect[i].name);
        T_ASSERT_EQ_INT(t, (long long)m->offset, (long long)expect[i].offset);
        /* the two offsets are SIGNED: negative is the whole design */
        if (i >= 3)
            T_ASSERT_EQ_INT(t, m->type->kind, TY_INT);
        else
            T_ASSERT_EQ_INT(t, m->type->kind, TY_PTR);
    }
    T_ASSERT_EQ_INT(t, (long long)i, 5);
    abi_close(&f);
}

/* x86-64 keeps its own four-field shape; the two must not converge. */
void test_abi_sysv_va_list_shape_is_unchanged(TestCtx *t)
{
    AbiFix f;
    Type *va, *rec;
    TypeLayout l;

    abi_open(&f, "int x;\n", CGF_TARGET_X86_64_LINUX_GNU);
    va = sema_va_list_type(&f.sema);
    rec = va->base;
    T_ASSERT_EQ_INT(t, rec->tag->nmembers, 4);
    T_ASSERT_EQ_STR(t, rec->tag->members->name, "gp_offset");
    l = layout_of(&f.sema, rec);
    T_ASSERT_EQ_INT(t, (long long)l.size, 24);
    abi_close(&f);
}
