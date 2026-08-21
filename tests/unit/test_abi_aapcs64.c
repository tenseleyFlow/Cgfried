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
        /* GNU FloatN types keep distinct C identities while using the
         * corresponding AAPCS64 register views. */
        {"struct S { _Float32 x, y; };", ABI_ARG_HFA, 2, IRT_F32},
        {"struct S { _Float64 x, y; };", ABI_ARG_HFA, 2, IRT_F64},
        {"struct S { _Float32x x, y; };", ABI_ARG_HFA, 2, IRT_F64},
        {"struct S { _Float64x x, y; };", ABI_ARG_HFA, 2, IRT_F128},
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

void test_abi_aapcs64_hfa_target_float_formats(TestCtx *t)
{
    static const struct {
        TargetKind target;
        const char *body;
        IrType leaf;
        u8 ir_abi;
    } rows[] = {
        {CGF_TARGET_ARM64_LINUX, "struct S { long double x, y; };", IRT_F128,
         IR_ABIRET_HFA_F128},
        {CGF_TARGET_ARM64_MACOS, "struct S { long double x, y; };", IRT_F64,
         IR_ABIRET_HFA_F64},
        {CGF_TARGET_ARM64_LINUX, "struct S { _Float64x x, y; };", IRT_F128,
         IR_ABIRET_HFA_F128},
        {CGF_TARGET_ARM64_MACOS, "struct S { _Float64x x, y; };", IRT_F64,
         IR_ABIRET_HFA_F64},
    };
    u32 i;

    for (i = 0; i < CGF_ARRAY_LEN(rows); i++) {
        AbiFix f;
        AbiArg arg;
        AbiRet ret;
        Type *ty;

        abi_open(&f, rows[i].body, rows[i].target);
        ty = tag_type(&f, "S");
        T_ASSERT(t, ty != NULL);
        if (ty) {
            abi_classify_arg(&f.lo, ty, &arg);
            T_ASSERT_EQ_INT(t, arg.kind, ABI_ARG_HFA);
            T_ASSERT_EQ_INT(t, arg.n, 2);
            T_ASSERT_EQ_INT(t, arg.t[0], rows[i].leaf);
            T_ASSERT_EQ_INT(t, arg.t[1], rows[i].leaf);

            abi_classify_ret(&f.lo, ty, &ret);
            T_ASSERT_EQ_INT(t, ret.kind, ABI_RET_HFA);
            T_ASSERT_EQ_INT(t, ret.n, 2);
            T_ASSERT_EQ_INT(t, ret.small_t, rows[i].leaf);
            T_ASSERT_EQ_INT(t, ret.ir_abi, rows[i].ir_abi);
        }
        abi_close(&f);
    }
}

void test_abi_floatn_ir_representations(TestCtx *t)
{
    AbiFix f;

    abi_open(&f, "int x;\n", CGF_TARGET_X86_64_LINUX_GNU);
    T_ASSERT_EQ_INT(t, lower_irtype(&f.lo, type_basic(TY_FLOAT32)), IRT_F32);
    T_ASSERT_EQ_INT(t, lower_irtype(&f.lo, type_basic(TY_FLOAT64)), IRT_F64);
    T_ASSERT_EQ_INT(t, lower_irtype(&f.lo, type_basic(TY_FLOAT32X)), IRT_F64);
    T_ASSERT_EQ_INT(t, lower_irtype(&f.lo, type_basic(TY_FLOAT64X)), IRT_F80);
    T_ASSERT_EQ_INT(t, lower_efftype(&f.lo, type_basic(TY_FLOAT32)), ETYPE_F32);
    T_ASSERT_EQ_INT(t, lower_efftype(&f.lo, type_basic(TY_FLOAT64)), ETYPE_F64);

    f.sema.target.kind = CGF_TARGET_ARM64_LINUX;
    T_ASSERT_EQ_INT(t, lower_irtype(&f.lo, type_basic(TY_FLOAT64X)), IRT_F128);
    T_ASSERT_EQ_INT(t, lower_efftype(&f.lo, type_basic(TY_FLOAT64X)),
                    ETYPE_F128);

    f.sema.target.kind = CGF_TARGET_ARM64_MACOS;
    T_ASSERT_EQ_INT(t, lower_irtype(&f.lo, type_basic(TY_FLOAT64X)), IRT_F64);
    T_ASSERT_EQ_INT(t, lower_efftype(&f.lo, type_basic(TY_FLOAT64X)),
                    ETYPE_F64);
    abi_close(&f);
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
        {"struct S { _Float128 q[2]; };", ABI_RET_HFA, IRT_F128},
        {"struct S { _Float64x q[4]; };", ABI_RET_HFA, IRT_F128},
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
        else if (got.kind == ABI_RET_HFA) {
            u8 want = rows[i].leaf == IRT_F32    ? IR_ABIRET_HFA_F32
                      : rows[i].leaf == IRT_F128 ? IR_ABIRET_HFA_F128
                                                 : IR_ABIRET_HFA_F64;

            if (got.ir_abi != want)
                t_fail(t, __FILE__, __LINE__, "%s: IR ABI %u, want %u",
                       rows[i].body, got.ir_abi, want);
        }
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

/* abi_arg_place is what va_start's constants and the register caps are
 * computed from -- it both places an argument and charges it -- so its
 * accounting must match the plans exactly. It replaced a second, parallel
 * counter that missed the general register an INDIRECT aggregate spends;
 * the S(64) row below is the one that caught it. */
void test_abi_aapcs64_reg_accounting(TestCtx *t)
{
    AbiFix f;
    AbiArg got;
    AbiBudget b;
    Type *ty;

    abi_open(&f,
             "struct H4 { double d[4]; };\n"
             "struct P { long a, b; };\n"
             "struct S { char big[64]; };\n",
             CGF_TARGET_ARM64_LINUX);

    ty = tag_type(&f, "H4");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)b.gp, 0);
    T_ASSERT_EQ_INT(t, (int)b.fp, 4); /* four v-regs, one per leaf */

    ty = tag_type(&f, "P");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)b.gp, 2);
    T_ASSERT_EQ_INT(t, (int)b.fp, 0);

    /* Over 16 bytes: AAPCS64 passes the ADDRESS of a caller-made copy, and
     * that address costs one general register. */
    ty = tag_type(&f, "S");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_BYVAL);
    T_ASSERT_EQ_INT(t, (int)b.gp, 1);
    T_ASSERT_EQ_INT(t, (int)b.fp, 0);

    /* Two four-leaf HFAs fill v0-v7 exactly; a third has nowhere to go and
     * is stacked WHOLE rather than split across the last registers and
     * memory. */
    abi_budget_init(&f.lo, &b, NULL);
    ty = tag_type(&f, "H4");
    abi_classify_arg(&f.lo, ty, &got);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_HFA);
    abi_classify_arg(&f.lo, ty, &got);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_HFA);
    T_ASSERT_EQ_INT(t, (int)b.fp, 8);
    abi_classify_arg(&f.lo, ty, &got);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_STACK);
    T_ASSERT_EQ_INT(t, (int)b.fp, 8);

    abi_close(&f);
}

void test_abi_aapcs64_linux_even_composite_registers(TestCtx *t)
{
    static const struct {
        u32 start;
        u32 end;
        u8 stacked;
        u8 padded;
    } boundaries[] = {
        {0, 2, 0, 0}, {1, 4, 0, 1}, {2, 4, 0, 0},
        {5, 8, 0, 1}, {6, 8, 0, 0}, {7, 8, 1, 1},
    };
    AbiFix f;
    AbiArg got;
    AbiBudget b;
    Type *aligned, *ordinary;
    u32 i;

    abi_open(&f,
             "struct Q { _Alignas(16) long a; long b; };\n"
             "struct P { long a; long b; };\n",
             CGF_TARGET_ARM64_LINUX);
    aligned = tag_type(&f, "Q");
    ordinary = tag_type(&f, "P");

    /* Register starts 0/1/2 and the 5/6/7 exhaustion edge pin both the
     * even-NGRN adjustment and its transition to whole-argument stacking. */
    for (i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i++) {
        abi_classify_arg(&f.lo, aligned, &got);
        abi_budget_init(&f.lo, &b, NULL);
        b.gp = boundaries[i].start;
        abi_arg_place(&f.lo, &got, &b, false);
        T_ASSERT_EQ_INT(t, (int)b.gp, (int)boundaries[i].end);
        T_ASSERT_EQ_INT(t, (int)(got.kind == ABI_ARG_STACK),
                        boundaries[i].stacked);
        T_ASSERT_EQ_INT(t, (int)got.even_gp, boundaries[i].padded);
    }

    /* Eight-byte alignment never skips a register. */
    abi_classify_arg(&f.lo, ordinary, &got);
    abi_budget_init(&f.lo, &b, NULL);
    b.gp = 1;
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)b.gp, 3);
    T_ASSERT_EQ_INT(t, (int)got.even_gp, 0);
    abi_close(&f);

    /* Apple arm64 deliberately removed the even-register rule. */
    abi_open(&f, "struct Q { _Alignas(16) long a; long b; };\n",
             CGF_TARGET_ARM64_MACOS);
    aligned = tag_type(&f, "Q");
    abi_classify_arg(&f.lo, aligned, &got);
    abi_budget_init(&f.lo, &b, NULL);
    b.gp = 1;
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)b.gp, 3);
    T_ASSERT_EQ_INT(t, (int)got.even_gp, 0);
    abi_close(&f);
}

/* ABI-004: what ANONYMITY changes on Apple, and what it does not.
 *
 * Every row here was measured against clang targeting arm64-apple-macos --
 * the ledger's summary ("Apple holds every anonymous argument by value")
 * is true only of the ones that would otherwise have travelled in
 * registers, and implementing it as written passed a 40-byte struct by
 * value where clang passes a pointer.
 *
 * The rule: anonymity removes the REGISTER, never the shape. An HFA goes by
 * value at any size, a non-HFA over 16 bytes goes indirect exactly as a
 * named one does, and the register budget is untouched either way. The
 * eightbyte re-plan exists only to fix LEAF GRANULARITY: a three-float HFA
 * occupied 24 bytes where clang uses 12 in a 16-byte slot. */
void test_abi_apple_anonymous_shape(TestCtx *t)
{
    AbiFix f;
    AbiArg got;
    AbiBudget b;
    Type *ty;

    abi_open(&f,
             "struct F3 { float a, b, c; };\n"  /* 12B HFA  */
             "struct D3 { double a, b, c; };\n" /* 24B HFA  */
             "struct C16 { char x[16]; };\n"    /* 16B      */
             "struct C17 { char x[17]; };\n",   /* 17B      */
             CGF_TARGET_ARM64_MACOS);

    /* A three-FLOAT HFA: three 4-byte leaves become two eightbytes, so it
     * occupies the 16 bytes clang gives it and not 24. This is the whole
     * defect. */
    ty = tag_type(&f, "F3");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, true);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_STACK);
    T_ASSERT_EQ_INT(t, (int)got.n, 2);
    /* Anonymous spends NO register, in either bank. */
    T_ASSERT_EQ_INT(t, (int)b.gp, 0);
    T_ASSERT_EQ_INT(t, (int)b.fp, 0);

    /* Named, the same type is an HFA in three v-regs -- proving the flag is
     * what changed the answer and not the type. */
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, false);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_HFA);
    T_ASSERT_EQ_INT(t, (int)b.fp, 3);

    /* A 24-byte HFA of DOUBLES stays by value: size does not decide it,
     * HFA-ness does. Its leaves are already 8 bytes, so the re-plan is a
     * no-op on the layout -- three eightbytes, 24 bytes, exactly clang. */
    ty = tag_type(&f, "D3");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, true);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_STACK);
    T_ASSERT_EQ_INT(t, (int)got.n, 3);

    /* Exactly 16 bytes, not an HFA: by value, two eightbytes. */
    ty = tag_type(&f, "C16");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, true);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_STACK);
    T_ASSERT_EQ_INT(t, (int)got.n, 2);

    /* One byte more and it goes INDIRECT -- a pointer in the varargs area,
     * the same shape a named one takes. Re-planning this as eightbytes is
     * the miscompile the measurement caught. */
    ty = tag_type(&f, "C17");
    abi_classify_arg(&f.lo, ty, &got);
    abi_budget_init(&f.lo, &b, NULL);
    abi_arg_place(&f.lo, &got, &b, true);
    T_ASSERT_EQ_INT(t, (int)got.kind, ABI_ARG_BYVAL);

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
