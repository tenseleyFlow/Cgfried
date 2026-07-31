#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "cg/cg.h"
#include "unit.h"
#include "util/arena.h"
#include "x64sim.h"

/* Sprint 23 FP units: every fcmp predicate through the REAL pipeline
 * (builder IR -> isel -> verify -> regalloc -> verify -> interpreter)
 * against C semantics computed on the host — NaN operands included for
 * every predicate (DoD 1); the u64<->f64 edge set with the sticky-bit
 * case (DoD 2); the f80 20-op chain whose stack balance is proven by
 * execution; -0.0 through the xor-mask negate. */

typedef struct FpFix {
    int errors;
} FpFix;

static void fp_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        ((FpFix *)user)->errors++;
}

typedef void (*FillFn)(IrBuilder *b, const void *u);

/* Build func `t` (no params), run the whole backend, interpret. Returns
 * rax in *rax and xmm0 bits in *xmm0. */
static void run_func(TestCtx *t, IrType ret, FillFn fill, const void *u,
                     u64 *rax, u64 *xmm0)
{
    Arena a;
    FpFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    BlockId e;
    IrBuilder b;
    X64Func *xf;
    Sim s;
    u8 *mem = cgf_xmalloc(SIM_MEM);

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = fp_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    f = ir_func_new(m, "t", ret, NULL, 0);
    e = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, e);
    fill(&b, u);
    xf = x64_isel_function(m, f, &a);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    x64_regalloc(xf);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    sim_init(&s, mem, xf);
    T_ASSERT(t, sim_run(t, xf, &s));
    if (rax)
        *rax = s.val[X64_RAX + 1];
    if (xmm0)
        *xmm0 = s.val[X64_XMM0 + 1];
    free(mem);
    arena_free_all(&a);
}

/* --- the fcmp matrix --------------------------------------------------------
 */

typedef struct FcmpCase {
    u8 pred;
    u8 ty; /* IRT_F32 / IRT_F64 / IRT_F80 */
    double a, b;
} FcmpCase;

static void fill_fcmp(IrBuilder *b, const void *u)
{
    const FcmpCase *c = u;
    IrOperand x, y;
    ValueId r;
    IrOperand rv;

    if (c->ty == IRT_F32) {
        x = ir_op_fconst(IRT_F32, sim_f32bits((float)c->a), 0);
        y = ir_op_fconst(IRT_F32, sim_f32bits((float)c->b), 0);
    } else if (c->ty == IRT_F64) {
        x = ir_op_fconst(IRT_F64, sim_f64bits(c->a), 0);
        y = ir_op_fconst(IRT_F64, sim_f64bits(c->b), 0);
    } else {
        long double la = (long double)c->a, lb = (long double)c->b;
        u64 alo = 0, ahi = 0, blo = 0, bhi = 0;

        memcpy(&alo, &la, 8);
        memcpy(&ahi, (const char *)&la + 8, 2);
        memcpy(&blo, &lb, 8);
        memcpy(&bhi, (const char *)&lb + 8, 2);
        x = ir_op_fconst(IRT_F80, alo, ahi);
        y = ir_op_fconst(IRT_F80, blo, bhi);
    }
    r = ir_build_fcmp(b, (IrFcmp)c->pred, x, y);
    rv = ir_op_value(b->f, r);
    ir_build_ret(b, &rv);
}

/* C-semantics oracle for each predicate (host doubles). */
static int fcmp_expected(u8 pred, double a, double b)
{
    int uo = isunordered(a, b);

    switch (pred) {
    case FCMP_OEQ:
        return !uo && a == b;
    case FCMP_ONE:
        return !uo && a != b;
    case FCMP_OLT:
        return a < b;
    case FCMP_OLE:
        return a <= b;
    case FCMP_OGT:
        return a > b;
    case FCMP_OGE:
        return a >= b;
    case FCMP_ORD:
        return !uo;
    case FCMP_UEQ:
        return uo || a == b;
    case FCMP_UNE:
        return uo || a != b;
    case FCMP_ULT:
        return uo || a < b;
    case FCMP_ULE:
        return uo || a <= b;
    case FCMP_UGT:
        return uo || a > b;
    case FCMP_UGE:
        return uo || a >= b;
    default: /* FCMP_UNO */
        return uo;
    }
}

void test_x64_fcmp_recipe_matrix(TestCtx *t)
{
    static const double pairs[][2] = {
        {1.0, 2.0}, {2.0, 1.0}, {1.0, 1.0}, {0.0, -0.0},
        {NAN, 1.0}, {1.0, NAN}, {NAN, NAN},
    };
    static const u8 types[] = {IRT_F32, IRT_F64, IRT_F80};
    u8 pred;
    u32 pi, ti;

    for (pred = FCMP_OEQ; pred <= FCMP_UNO; pred++)
        for (ti = 0; ti < 3; ti++)
            for (pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); pi++) {
                FcmpCase c;
                u64 rax = 0;

                c.pred = pred;
                c.ty = types[ti];
                c.a = pairs[pi][0];
                c.b = pairs[pi][1];
                run_func(t, IRT_I32, fill_fcmp, &c, &rax, NULL);
                T_ASSERT_EQ_INT(t, (int)(u32)rax,
                                fcmp_expected(pred, c.a, c.b));
            }
}

/* Branch form: single-cc predicates fuse into jcc, pair predicates
 * branch through the materialized value — both must agree with C. */
static void fill_fcmp_branch(IrBuilder *b, const void *u)
{
    const FcmpCase *c = u;
    IrOperand x = ir_op_fconst(IRT_F64, sim_f64bits(c->a), 0);
    IrOperand y = ir_op_fconst(IRT_F64, sim_f64bits(c->b), 0);
    ValueId r = ir_build_fcmp(b, (IrFcmp)c->pred, x, y);
    BlockId bt = ir_block_new(b->m, b->f, "then");
    BlockId be = ir_block_new(b->m, b->f, "else");
    IrOperand cv = ir_op_value(b->f, r);
    IrOperand one = ir_op_iconst(IRT_I32, 1);
    IrOperand two = ir_op_iconst(IRT_I32, 2);

    ir_build_condbr(b, cv, bt, NULL, 0, be, NULL, 0);
    ir_builder_at(b, b->m, b->f, bt);
    ir_build_ret(b, &one);
    ir_builder_at(b, b->m, b->f, be);
    ir_build_ret(b, &two);
}

void test_x64_fcmp_branch_forms(TestCtx *t)
{
    static const double pairs[][2] = {
        {1.0, 2.0}, {2.0, 1.0}, {1.0, 1.0}, {NAN, 1.0}, {NAN, NAN},
    };
    u8 pred;
    u32 pi;

    for (pred = FCMP_OEQ; pred <= FCMP_UNO; pred++)
        for (pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); pi++) {
            FcmpCase c;
            u64 rax = 0;

            c.pred = pred;
            c.ty = IRT_F64;
            c.a = pairs[pi][0];
            c.b = pairs[pi][1];
            run_func(t, IRT_I32, fill_fcmp_branch, &c, &rax, NULL);
            T_ASSERT_EQ_INT(t, (int)(u32)rax,
                            fcmp_expected(pred, c.a, c.b) ? 1 : 2);
        }
}

/* --- u64 <-> f64 (DoD 2: 8+ edges incl. the sticky bit) --------------------
 */

static void fill_uitofp(IrBuilder *b, const void *u)
{
    u64 v = *(const u64 *)u;
    ValueId r = ir_build1(b, IR_UITOFP, IRT_F64, ir_op_iconst(IRT_I64, (i64)v));
    IrOperand rv = ir_op_value(b->f, r);

    ir_build_ret(b, &rv);
}

void test_x64_u64_to_f64_edges(TestCtx *t)
{
    /* 0x8000000000000401: needs the halve-with-sticky-bit trick — a
     * naive shift-convert-double rounds TWICE and lands one ulp off. */
    static const u64 edges[] = {
        0,
        1,
        2,
        0x7fffffffffffffffull,
        0x8000000000000000ull,
        0x8000000000000001ull,
        0x8000000000000401ull,
        0xfffffffffffffc00ull,
        0xffffffffffffffffull,
        0x0010000000000001ull,
    };
    u32 i;

    for (i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        u64 xmm0 = 0;

        run_func(t, IRT_F64, fill_uitofp, &edges[i], NULL, &xmm0);
        T_ASSERT(t, xmm0 == sim_f64bits((double)edges[i]));
    }
}

static void fill_fptoui(IrBuilder *b, const void *u)
{
    double d = *(const double *)u;
    ValueId r = ir_build1(b, IR_FPTOUI, IRT_I64,
                          ir_op_fconst(IRT_F64, sim_f64bits(d), 0));
    IrOperand rv = ir_op_value(b->f, r);

    ir_build_ret(b, &rv);
}

void test_x64_f64_to_u64_edges(TestCtx *t)
{
    static const double edges[] = {
        0.0,
        1.0,
        1.5,
        4503599627370495.5,
        9223372036854775808.0,  /* 2^63 exactly */
        9223372036854777856.0,  /* 2^63 + 2048 */
        18446744073709549568.0, /* just under 2^64 */
        4294967295.0,
        2.9,
    };
    u32 i;

    for (i = 0; i < sizeof(edges) / sizeof(edges[0]); i++) {
        u64 rax = 0;

        run_func(t, IRT_I64, fill_fptoui, &edges[i], &rax, NULL);
        T_ASSERT(t, rax == (u64)edges[i]);
    }
}

/* --- f80: 20-op chain, balance proven by execution -------------------------
 */

static void fill_f80_chain(IrBuilder *b, const void *u)
{
    long double acc = 1.0L;
    u64 lo = 0, hi = 0;
    IrOperand cur;
    ValueId r = {0};
    u32 i;

    (void)u;
    memcpy(&lo, &acc, 8);
    memcpy(&hi, (const char *)&acc + 8, 2);
    cur = ir_op_fconst(IRT_F80, lo, hi);
    for (i = 0; i < 20; i++) {
        long double k = (long double)(i + 1);

        memcpy(&lo, &k, 8);
        memcpy(&hi, (const char *)&k + 8, 2);
        r = ir_build2(b, i % 3 == 2 ? IR_FMUL : IR_FADD, IRT_F80, cur,
                      ir_op_fconst(IRT_F80, lo, hi));
        cur = ir_op_value(b->f, r);
    }
    r = ir_build1(b, IR_FPTOSI, IRT_I64, cur);
    {
        IrOperand rv = ir_op_value(b->f, r);

        ir_build_ret(b, &rv);
    }
}

void test_x64_f80_chain_balance(TestCtx *t)
{
    long double acc = 1.0L;
    u32 i;
    u64 rax = 0;

    for (i = 0; i < 20; i++) {
        long double k = (long double)(i + 1);

        if (i % 3 == 2)
            acc = acc * k;
        else
            acc = acc + k;
    }
    run_func(t, IRT_I64, fill_f80_chain, NULL, &rax, NULL);
    T_ASSERT(t, (i64)rax == (i64)acc);
}

/* --- -0.0 through the xor-mask negate --------------------------------------
 */

static void fill_negzero(IrBuilder *b, const void *u)
{
    ValueId n = ir_build1(b, IR_FNEG, IRT_F64,
                          ir_op_fconst(IRT_F64, 0, 0)); /* -(+0.0) */
    ValueId r = ir_build1(b, IR_BITCAST, IRT_I64, ir_op_value(b->f, n));
    IrOperand rv = ir_op_value(b->f, r);

    (void)u;
    ir_build_ret(b, &rv);
}

void test_x64_fneg_signed_zero(TestCtx *t)
{
    u64 rax = 0;

    run_func(t, IRT_I64, fill_negzero, NULL, &rax, NULL);
    T_ASSERT(t, rax == 0x8000000000000000ull);
}

/* --- i64/i32 <-> f64 sanity -------------------------------------------------
 */

static void fill_sitofp_roundtrip(IrBuilder *b, const void *u)
{
    i64 v = *(const i64 *)u;
    ValueId d = ir_build1(b, IR_SITOFP, IRT_F64, ir_op_iconst(IRT_I64, v));
    ValueId r = ir_build1(b, IR_FPTOSI, IRT_I64, ir_op_value(b->f, d));
    IrOperand rv = ir_op_value(b->f, r);

    ir_build_ret(b, &rv);
}

void test_x64_sitofp_fptosi_roundtrip(TestCtx *t)
{
    static const i64 vals[] = {0, 1, -1, 42, -9000, 1073741824, -1073741824};
    u32 i;

    for (i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        u64 rax = 0;

        run_func(t, IRT_I64, fill_sitofp_roundtrip, &vals[i], &rax, NULL);
        T_ASSERT(t, (i64)rax == vals[i]);
    }
}