#include <stdio.h>
#include <string.h>

#include "ir/ir.h"
#include "unit.h"
#include "util/arena.h"

/* Verifier units: every one of the ten checks driven NEGATIVE at least
 * once (builder misuse where the builder permits it, parsed text where it
 * does not), each pinned to its check number via the "ir verify [N]"
 * message prefix — so a regression cannot silently reroute a failure
 * through a different check and still pass. */

typedef struct {
    Arena arena;
    DiagCtx *dc;
    int errors;
    char msgs[8][256]; /* first 8 error messages */
} VFix;

static void v_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    VFix *f = user;

    (void)dc;
    if (d->level >= DIAG_ERROR) {
        if (f->errors < 8) {
            strncpy(f->msgs[f->errors], d->message, sizeof(f->msgs[0]) - 1);
            f->msgs[f->errors][sizeof(f->msgs[0]) - 1] = '\0';
        }
        f->errors++;
    }
}

static void vfix_init(VFix *f)
{
    DiagSink sink;

    memset(f, 0, sizeof(*f));
    arena_init(&f->arena);
    f->dc = diag_ctx_new(&f->arena);
    sink.handle = v_sink;
    sink.user = f;
    diag_set_sink(f->dc, sink);
}

/* True iff some captured message names check N. */
static bool fired(const VFix *f, int check)
{
    char tag[32];
    int i;
    int n = f->errors < 8 ? f->errors : 8;

    snprintf(tag, sizeof(tag), "ir verify [%d]", check);
    for (i = 0; i < n; i++)
        if (strstr(f->msgs[i], tag))
            return true;
    return false;
}

/* Minimal one-block void function ready for misuse. */
static IrFunc *scaffold(VFix *f, IrModule **mp, IrBuilder *b)
{
    IrModule *m = ir_module_new(&f->arena, f->dc);
    IrFunc *fn = ir_func_new(m, "f", IRT_VOID, NULL, 0);
    BlockId entry = ir_block_new(m, fn, "entry");

    ir_builder_at(b, m, fn, entry);
    *mp = m;
    return fn;
}

void test_ir_verify_clean_module_passes(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;
    ValueId p;

    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    p = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 8), 8);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 5), ir_op_value(fn, p), 4, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, ir_verify(f.dc, m));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    arena_free_all(&f.arena);
}

void test_ir_verify_check1_dominance(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;
    BlockId aa, bb, cc;
    ValueId x;
    IrOperand xo;

    /* entry: condbr 1, a(), b();  a: %x = iadd, br c();  b: br c();
     * c: use %x  — a does NOT dominate c. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    aa = ir_block_new(m, fn, "a");
    bb = ir_block_new(m, fn, "b");
    cc = ir_block_new(m, fn, "c");
    ir_build_condbr(&b, ir_op_iconst(IRT_I32, 1), aa, NULL, 0, bb, NULL, 0);
    ir_builder_at(&b, m, fn, aa);
    x = ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I32, 1),
                  ir_op_iconst(IRT_I32, 2));
    ir_build_br(&b, cc, NULL, 0);
    ir_builder_at(&b, m, fn, bb);
    ir_build_br(&b, cc, NULL, 0);
    ir_builder_at(&b, m, fn, cc);
    xo = ir_op_value(fn, x);
    ir_build2(&b, IR_IADD, IRT_I32, xo, xo);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 1));
    arena_free_all(&f.arena);

    /* Same-block use BEFORE def: only expressible as text (the builder
     * appends in order), which is exactly why the parser exists for
     * verifier tests. */
    vfix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    %a = iadd i32 %b, 1\n"
                        "    %b = iadd i32 1, 2\n"
                        "    ret\n"
                        "}\n",
                        "<v>");
    T_ASSERT(t, m != NULL);
    if (m) {
        T_ASSERT(t, !ir_verify(f.dc, m));
        T_ASSERT(t, fired(&f, 1));
    }
    arena_free_all(&f.arena);
}

void test_ir_verify_check2_edge_args(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;
    BlockId tgt;
    IrOperand arg;

    /* Arity mismatch. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    tgt = ir_block_new(m, fn, "tgt");
    ir_block_param(m, fn, tgt, IRT_I32);
    ir_build_br(&b, tgt, NULL, 0); /* 0 args to a 1-param block */
    ir_builder_at(&b, m, fn, tgt);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 2));
    arena_free_all(&f.arena);

    /* Type mismatch. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    tgt = ir_block_new(m, fn, "tgt");
    ir_block_param(m, fn, tgt, IRT_I64);
    arg = ir_op_iconst(IRT_I32, 0); /* i32 into an i64 param */
    ir_build_br(&b, tgt, &arg, 1);
    ir_builder_at(&b, m, fn, tgt);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 2));
    arena_free_all(&f.arena);
}

void test_ir_verify_check3_terminators(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;

    /* No terminator at all. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I32, 1),
              ir_op_iconst(IRT_I32, 2));
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 3));
    arena_free_all(&f.arena);

    /* Terminator not last / two terminators: the builder ICEs on that
     * misuse, so it arrives as text. */
    vfix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @f() {\n"
                        "entry():\n"
                        "    ret\n"
                        "    ret\n"
                        "}\n",
                        "<v>");
    T_ASSERT(t, m != NULL);
    if (m) {
        T_ASSERT(t, !ir_verify(f.dc, m));
        T_ASSERT(t, fired(&f, 3));
    }
    arena_free_all(&f.arena);
}

void test_ir_verify_check4_type_discipline(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;
    ValueId p;

    /* Operand type != result type on iadd. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I64, 1),
              ir_op_iconst(IRT_I32, 2));
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 4));
    arena_free_all(&f.arena);

    /* iadd on a float type. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build2(&b, IR_IADD, IRT_F64, ir_op_undef(IRT_F64), ir_op_undef(IRT_F64));
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 4));
    arena_free_all(&f.arena);

    /* sext that does not widen. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build1(&b, IR_SEXT, IRT_I32, ir_op_iconst(IRT_I32, 1));
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 4));
    arena_free_all(&f.arena);

    /* bitcast between different widths. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build1(&b, IR_BITCAST, IRT_F64, ir_op_iconst(IRT_I32, 1));
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 4));
    arena_free_all(&f.arena);

    /* load through a non-ptr operand. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build_load(&b, IRT_I32, ir_op_iconst(IRT_I64, 0), 4, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 4));
    arena_free_all(&f.arena);

    /* ret with a value in a void function. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    p = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 4), 4);
    {
        IrOperand rv = ir_op_value(fn, p);

        ir_build_ret(&b, &rv);
    }
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 4));
    arena_free_all(&f.arena);
}

void test_ir_verify_check5_entry_shape(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;

    /* Entry with a parameter. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    ir_block_param(m, fn, (BlockId){1}, IRT_I32);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 5));
    arena_free_all(&f.arena);

    /* Entry with a predecessor (a self-loop back to block 1). */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build_br(&b, (BlockId){1}, NULL, 0);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 5));
    arena_free_all(&f.arena);
}

void test_ir_verify_check6_orphans(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;

    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    ir_build_ret(&b, NULL);
    ir_builder_at(&b, m, fn, ir_block_new(m, fn, "dead"));
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 6));
    arena_free_all(&f.arena);
}

void test_ir_verify_check7_flags(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;

    /* volatile on an iadd: set after the fact (no builder path takes
     * flags there, which is the point of the structural hook). */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I32, 1),
              ir_op_iconst(IRT_I32, 2));
    fn->blocks[0].last->flags = IRF_VOLATILE;
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 7));
    arena_free_all(&f.arena);

    /* seq_cst on a memcpy: only load/store may be atomic. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    {
        ValueId p = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 8), 8);

        ir_build_memcpy(&b, ir_op_value(fn, p), ir_op_value(fn, p),
                        ir_op_iconst(IRT_I64, 8), 1, IRF_SEQ_CST);
    }
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 7));
    arena_free_all(&f.arena);
}

void test_ir_verify_check8_alignment(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;
    ValueId p;

    /* Non-power-of-two. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build_alloca(&b, ir_op_iconst(IRT_I64, 8), 3);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 8));
    arena_free_all(&f.arena);

    /* Zero alignment. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    p = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 8), 8);
    ir_build_store(&b, ir_op_iconst(IRT_I32, 0), ir_op_value(fn, p), 0, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 8));
    arena_free_all(&f.arena);

    /* Over-aligned load: claims more than the type's natural alignment.
     * (UNDER-aligned is legal — packed structs — and must pass.) */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    p = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 8), 8);
    ir_build_load(&b, IRT_I32, ir_op_value(fn, p), 16, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 8));
    arena_free_all(&f.arena);

    /* Under-aligned load passes. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    p = ir_build_alloca(&b, ir_op_iconst(IRT_I64, 8), 8);
    ir_build_load(&b, IRT_I64, ir_op_value(fn, p), 1, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, ir_verify(f.dc, m));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    arena_free_all(&f.arena);

    /* Bad global alignment. */
    vfix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    ir_global_new(m, "g")->align = 6;
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 8));
    arena_free_all(&f.arena);
}

void test_ir_verify_check9_refs(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;
    IrType pt[1] = {IRT_I64};
    IrOperand arg;

    /* External call to an out-of-range symbol index. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build_call(&b, IRT_VOID, FUNCREF_EXTERNAL, 999, NULL, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 9));
    arena_free_all(&f.arena);

    /* Internal call with wrong arity, then wrong arg type. */
    vfix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    ir_func_new(m, "callee", IRT_VOID, pt, 1);
    {
        BlockId e;

        fn = &m->funcs[0];
        e = ir_block_new(m, fn, "entry");
        ir_builder_at(&b, m, fn, e);
        ir_build_ret(&b, NULL);
        fn = ir_func_new(m, "caller", IRT_VOID, NULL, 0);
        e = ir_block_new(m, fn, "entry");
        ir_builder_at(&b, m, fn, e);
        ir_build_call(&b, IRT_VOID, FUNCREF_INTERNAL, 0, NULL, 0);
        ir_build_ret(&b, NULL);
    }
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 9));
    arena_free_all(&f.arena);

    /* An old-style definition has concrete body parameters but no
     * prototype at call sites, so arity and promoted argument types are
     * intentionally loose. */
    vfix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    fn = ir_func_new(m, "callee", IRT_VOID, pt, 1);
    fn->unprototyped = true;
    {
        BlockId e = ir_block_new(m, fn, "entry");

        ir_builder_at(&b, m, fn, e);
        ir_build_ret(&b, NULL);
        fn = ir_func_new(m, "caller", IRT_VOID, NULL, 0);
        e = ir_block_new(m, fn, "entry");
        ir_builder_at(&b, m, fn, e);
        ir_build_call(&b, IRT_VOID, FUNCREF_INTERNAL, 0, NULL, 0);
        ir_build_ret(&b, NULL);
    }
    T_ASSERT(t, ir_verify(f.dc, m));
    T_ASSERT_EQ_INT(t, f.errors, 0);
    arena_free_all(&f.arena);

    vfix_init(&f);
    m = ir_module_new(&f.arena, f.dc);
    ir_func_new(m, "callee", IRT_VOID, pt, 1);
    {
        BlockId e;

        fn = &m->funcs[0];
        e = ir_block_new(m, fn, "entry");
        ir_builder_at(&b, m, fn, e);
        ir_build_ret(&b, NULL);
        fn = ir_func_new(m, "caller", IRT_VOID, NULL, 0);
        e = ir_block_new(m, fn, "entry");
        ir_builder_at(&b, m, fn, e);
        arg = ir_op_iconst(IRT_I32, 0); /* i64 wanted */
        ir_build_call(&b, IRT_VOID, FUNCREF_INTERNAL, 0, &arg, 1);
        ir_build_ret(&b, NULL);
    }
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 9));
    arena_free_all(&f.arena);

    /* Wide floating constants use operand.b for their high bits. Those bits
     * are not a call-ABI annotation (only values and symbols carry one). */
    vfix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @callee(f128 %x) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n"
                        "func void @caller() {\n"
                        "entry():\n"
                        "    call void @callee(f128 "
                        "0x3FFF000700000000:0x0000000000000001)\n"
                        "    ret\n"
                        "}\n",
                        "<v>");
    T_ASSERT(t, m != NULL);
    if (m) {
        T_ASSERT(t, ir_verify(f.dc, m));
        T_ASSERT_EQ_INT(t, f.errors, 0);
    }
    arena_free_all(&f.arena);

    /* A direct call's ABI annotation is part of its signature, not optional
     * decoration.  The inliner and IPO both rely on this boundary check. */
    vfix_init(&f);
    m = ir_parse_module(&f.arena, f.dc,
                        "func void @callee(ptr byval(16) %p) {\n"
                        "entry():\n"
                        "    ret\n"
                        "}\n"
                        "func void @caller(ptr %p) {\n"
                        "entry():\n"
                        "    call void @callee(ptr %p)\n"
                        "    ret\n"
                        "}\n",
                        "<v>");
    T_ASSERT(t, m != NULL);
    if (m) {
        T_ASSERT(t, !ir_verify(f.dc, m));
        T_ASSERT(t, fired(&f, 9));
    }
    arena_free_all(&f.arena);

    /* Symbol operand out of range. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    ir_build_load(&b, IRT_I32, ir_op_symbol(IRT_PTR, 42, 0), 4, 0);
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 9));
    arena_free_all(&f.arena);
}

void test_ir_verify_check10_reserved(TestCtx *t)
{
    VFix f;
    IrModule *m;
    IrBuilder b;
    IrFunc *fn;

    /* The builder refuses reserved ops (ICE), so plant one by hand. */
    vfix_init(&f);
    fn = scaffold(&f, &m, &b);
    ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I32, 1),
              ir_op_iconst(IRT_I32, 2));
    fn->blocks[0].first->op = IR_VA_ARG;
    fn->blocks[0].first->nops = 0;
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    T_ASSERT(t, fired(&f, 10));
    arena_free_all(&f.arena);
}

void test_ir_verify_bad_ir_dump_is_parseable(TestCtx *t)
{
    VFix f;
    IrModule *m, *m2;
    IrBuilder b;
    Buf buf;

    /* The CGF_DUMP_BAD_IR contract: whatever the verifier rejects, the
     * printer's dump of it must still PARSE (or the dump is useless for
     * bug reports). Verify-failing != print-failing. */
    vfix_init(&f);
    scaffold(&f, &m, &b);
    ir_build2(&b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I64, 1),
              ir_op_iconst(IRT_I32, 2)); /* check-4 violation */
    ir_build_ret(&b, NULL);
    T_ASSERT(t, !ir_verify(f.dc, m));
    buf_init(&buf);
    ir_print_module_buf(&buf, m);
    buf_push_u8(&buf, 0);
    m2 = ir_parse_module(&f.arena, f.dc, (const char *)buf.data, "<dump>");
    T_ASSERT(t, m2 != NULL);
    buf_free(&buf);
    arena_free_all(&f.arena);
}
