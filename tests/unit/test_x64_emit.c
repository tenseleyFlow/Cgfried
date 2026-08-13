#include <stdlib.h>
#include <string.h>

#include "cg/cg.h"
#include "unit.h"
#include "util/arena.h"

/* Sprint 24 emission units: the operand printer across widths and
 * addressing shapes (the src,dst discipline), the unreachable tail
 * (ud2, no trailing ret), and label determinism (two emissions of the
 * same module are byte-identical). The gas/afs-as agreement itself is
 * the objdiff lane's job — these pin the TEXT. */

typedef struct EmitFix {
    int errors;
} EmitFix;

static void e_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        ((EmitFix *)user)->errors++;
}

static bool has(const Buf *b, const char *needle)
{
    size_t nl = strlen(needle);
    size_t i;

    if (nl > b->len)
        return false;
    for (i = 0; i + nl <= b->len; i++)
        if (memcmp(b->data + i, needle, nl) == 0)
            return true;
    return false;
}

/* Hand-built ALLOCATED function exercising the printer matrix. */
void test_x64_emit_operand_forms(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    X64Func f;
    X64Block blk;
    X64Inst insts[12];
    Buf out;
    u32 n = 0;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    memset(&f, 0, sizeof(f));
    memset(&blk, 0, sizeof(blk));
    memset(insts, 0, sizeof(insts));
    f.name = "probe";
    f.arena = &a;
    f.allocated = true;
    f.m = m;
    f.blocks = &blk;
    f.nblocks = 1;
    blk.insts = insts;

    /* movl $-1, %r8d */
    insts[n].op = X64_OP_MOV;
    insts[n].width = X64_L;
    insts[n].def.v = X64_R8 + 1;
    insts[n].a.kind = X64O_IMM;
    insts[n].a.imm = -1;
    n++;
    /* movq -8(%rbp), %rax */
    insts[n].op = X64_OP_LOAD;
    insts[n].width = X64_Q;
    insts[n].def.v = X64_RAX + 1;
    insts[n].a.kind = X64O_MEM;
    insts[n].a.mem.base.v = X64_RBP + 1;
    insts[n].a.mem.scale = 1;
    insts[n].a.mem.disp = -8;
    n++;
    /* movb %al, (%r13) — the r13 no-disp base form */
    insts[n].op = X64_OP_STORE;
    insts[n].width = X64_B;
    insts[n].a.kind = X64O_VREG;
    insts[n].a.r.v = X64_RAX + 1;
    insts[n].b.kind = X64O_MEM;
    insts[n].b.mem.base.v = X64_R13 + 1;
    insts[n].b.mem.scale = 1;
    n++;
    /* movslq %ecx, %rdx */
    insts[n].op = X64_OP_MOVSX;
    insts[n].width = X64_Q;
    insts[n].src_width = X64_L;
    insts[n].def.v = X64_RDX + 1;
    insts[n].a.kind = X64O_VREG;
    insts[n].a.r.v = X64_RCX + 1;
    n++;
    /* sarl %cl, %edx (variable count) */
    insts[n].op = X64_OP_SAR;
    insts[n].width = X64_L;
    insts[n].flags = X64IF_TWO_ADDR;
    insts[n].def.v = X64_RDX + 1;
    insts[n].a.kind = X64O_VREG;
    insts[n].a.r.v = X64_RDX + 1;
    insts[n].b.kind = X64O_VREG;
    insts[n].b.r.v = X64_RCX + 1;
    n++;
    /* imulq $8, %rsi, %rsi (the 3-operand immediate form) */
    insts[n].op = X64_OP_IMUL;
    insts[n].width = X64_Q;
    insts[n].flags = X64IF_TWO_ADDR;
    insts[n].def.v = X64_RSI + 1;
    insts[n].a.kind = X64O_VREG;
    insts[n].a.r.v = X64_RSI + 1;
    insts[n].b.kind = X64O_IMM;
    insts[n].b.imm = 8;
    n++;
    /* movsd (%rax,%rcx,8), %xmm5 */
    insts[n].op = X64_OP_FLOAD;
    insts[n].width = X64_Q;
    insts[n].def.v = X64_XMM5 + 1;
    insts[n].a.kind = X64O_MEM;
    insts[n].a.mem.base.v = X64_RAX + 1;
    insts[n].a.mem.index.v = X64_RCX + 1;
    insts[n].a.mem.scale = 8;
    n++;
    /* ud2 tail: no trailing ret after it */
    insts[n].op = X64_OP_UD2;
    insts[n].width = X64_Q;
    n++;
    blk.n = n;
    f.nvregs = 0;

    buf_init(&out);
    x64_emit_function(&f, m, 0, IRLINK_EXTERNAL, &out);
    T_ASSERT(t, has(&out, "\tmovl\t$-1, %r8d\n"));
    T_ASSERT(t, has(&out, "\tmovq\t-8(%rbp), %rax\n"));
    T_ASSERT(t, has(&out, "\tmovb\t%al, (%r13)\n"));
    T_ASSERT(t, has(&out, "\tmovslq\t%ecx, %rdx\n"));
    T_ASSERT(t, has(&out, "\tsarl\t%cl, %edx\n"));
    T_ASSERT(t, has(&out, "\timulq\t$8, %rsi, %rsi\n"));
    T_ASSERT(t, has(&out, "\tmovsd\t(%rax,%rcx,8), %xmm5\n"));
    T_ASSERT(t, has(&out, "\tud2\n"));
    T_ASSERT(t, !has(&out, "\tret\n")); /* nothing after the ud2 */
    buf_free(&out);
    arena_free_all(&a);
}

/* Determinism: the whole pipeline twice on one module; byte-equal .s. */
static void fill_two(IrBuilder *b)
{
    ValueId v = ir_build2(b, IR_IADD, IRT_I32, ir_op_iconst(IRT_I32, 1),
                          ir_op_iconst(IRT_I32, 2));
    IrOperand rv = ir_op_value(b->f, v);

    ir_build_ret(b, &rv);
}

static void emit_once(Arena *a, DiagCtx *dc, Buf *out)
{
    IrModule *m = ir_module_new(a, dc);
    IrFunc *f = ir_func_new(m, "t", IRT_I32, NULL, 0);
    BlockId e = ir_block_new(m, f, "entry");
    IrBuilder b;
    X64Func *xf;

    ir_builder_at(&b, m, f, e);
    fill_two(&b);
    xf = x64_isel_function(m, f, a, X64_PIC_NONE);
    x64_regalloc(xf);
    x64_emit_function(xf, m, 0, IRLINK_EXTERNAL, out);
    x64_emit_globals(m, out, false);
}

void test_x64_emit_determinism(TestCtx *t)
{
    Arena a1, a2;
    EmitFix fx = {0};
    DiagCtx *d1, *d2;
    DiagSink sink;
    Buf o1, o2;

    arena_init(&a1);
    arena_init(&a2);
    sink.handle = e_sink;
    sink.user = &fx;
    d1 = diag_ctx_new(&a1);
    diag_set_sink(d1, sink);
    d2 = diag_ctx_new(&a2);
    diag_set_sink(d2, sink);
    buf_init(&o1);
    buf_init(&o2);
    emit_once(&a1, d1, &o1);
    emit_once(&a2, d2, &o2);
    T_ASSERT(t, o1.len > 0);
    T_ASSERT(t, o1.len == o2.len && memcmp(o1.data, o2.data, o1.len) == 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    buf_free(&o1);
    buf_free(&o2);
    arena_free_all(&a1);
    arena_free_all(&a2);
}

/* unreachable through the REAL pipeline: ud2, no ret in that block. */
static void fill_unreachable(IrBuilder *b)
{
    ir_build_unreachable(b);
}

void test_x64_emit_unreachable_ud2(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    BlockId e;
    IrBuilder b;
    X64Func *xf;
    Buf out;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    f = ir_func_new(m, "dead", IRT_VOID, NULL, 0);
    e = ir_block_new(m, f, "entry");
    ir_builder_at(&b, m, f, e);
    fill_unreachable(&b);
    xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
    x64_regalloc(xf);
    buf_init(&out);
    x64_emit_function(xf, m, 0, IRLINK_EXTERNAL, &out);
    T_ASSERT(t, has(&out, "\tud2\n"));
    T_ASSERT(t, !has(&out, "\tret\n"));
    buf_free(&out);
    arena_free_all(&a);
}

/* Source-level calls are not bounded by the number of physical argument
 * registers. Keep the per-argument placement plan arena-sized so large
 * variadic forwarding calls reach the ordinary outgoing-stack path. */
void test_x64_isel_accepts_more_than_sixty_four_call_arguments(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    IrBuilder b;
    IrOperand args[80];
    X64Func *xf;
    u32 i;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    (void)ir_sym(m, "sink");
    f = ir_func_new(m, "caller", IRT_VOID, NULL, 0);
    ir_builder_at(&b, m, f, ir_block_new(m, f, "entry"));
    for (i = 0; i < CGF_ARRAY_LEN(args); i++)
        args[i] = ir_op_iconst(IRT_I64, (i64)i);
    (void)ir_build_call(&b, IRT_VOID, FUNCREF_EXTERNAL, 0, args,
                        CGF_ARRAY_LEN(args));
    ir_build_ret(&b, NULL);

    xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
    T_ASSERT_EQ_INT(t, (long long)xf->out_args, (80 - 6) * 8);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    x64_regalloc(xf);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    arena_free_all(&a);
}

/* LCSSA may place one block parameter on an edge for every value live out of
 * a loop.  Edge copies therefore follow IR cardinality rather than a backend
 * convenience limit: dropping even the last copy leaves its destination
 * vreg undefined on that path. */
void test_x64_isel_preserves_more_than_thirty_two_edge_arguments(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    BlockId entry, join;
    IrBuilder b;
    IrOperand args[33];
    ValueId params[33];
    IrOperand result;
    X64Func *xf;
    u32 i, moves = 0;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    f = ir_func_new(m, "wide_edge", IRT_I64, NULL, 0);
    entry = ir_block_new(m, f, "entry");
    join = ir_block_new(m, f, "join");
    for (i = 0; i < CGF_ARRAY_LEN(params); i++) {
        params[i] = ir_block_param(m, f, join, IRT_I64);
        args[i] = ir_op_iconst(IRT_I64, (i64)i + 1);
    }

    ir_builder_at(&b, m, f, entry);
    ir_build_br(&b, join, args, CGF_ARRAY_LEN(args));
    ir_builder_at(&b, m, f, join);
    result = ir_op_value(f, params[CGF_ARRAY_LEN(params) - 1]);
    ir_build_ret(&b, &result);

    T_ASSERT(t, ir_verify(dc, m));
    xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
    for (i = 0; i < xf->blocks[0].n; i++)
        if (xf->blocks[0].insts[i].op == X64_OP_MOV)
            moves++;
    T_ASSERT_EQ_INT(t, moves, CGF_ARRAY_LEN(args));
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    x64_regalloc(xf);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    arena_free_all(&a);
}

void test_x64_isel_dynamic_alloca_breaks_compare_fusion(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    IrType params[1] = {IRT_I64};
    BlockId entry, yes, no;
    IrBuilder b;
    ValueId condition;
    X64Func *xf;
    const X64Block *xb;
    u32 i, alloca_i = UINT32_MAX, jcc_i = UINT32_MAX;
    bool materialized = false;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    f = ir_func_new(m, "alloca_flags", IRT_I32, params, 1);
    entry = ir_block_new(m, f, "entry");
    yes = ir_block_new(m, f, "yes");
    no = ir_block_new(m, f, "no");
    ir_builder_at(&b, m, f, entry);
    condition = ir_build_icmp(&b, ICMP_NE, ir_op_value(f, f->param_vals[0]),
                              ir_op_iconst(IRT_I64, 0));
    (void)ir_build_alloca(&b, ir_op_value(f, f->param_vals[0]), 64);
    ir_build_condbr(&b, ir_op_value(f, condition), yes, NULL, 0, no, NULL, 0);
    ir_builder_at(&b, m, f, yes);
    {
        IrOperand one = ir_op_iconst(IRT_I32, 1);

        ir_build_ret(&b, &one);
    }
    ir_builder_at(&b, m, f, no);
    {
        IrOperand zero = ir_op_iconst(IRT_I32, 0);

        ir_build_ret(&b, &zero);
    }

    xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
    xb = &xf->blocks[0];
    for (i = 0; i < xb->n; i++) {
        const X64Inst *x = &xb->insts[i];

        if (x->op == X64_OP_SETCC)
            materialized = true;
        if (x->op == X64_OP_ALLOCA_DYN) {
            alloca_i = i;
            T_ASSERT(t, (x->flags & X64IF_DEFS_FLAGS) != 0);
        }
        if (x->op == X64_OP_JCC)
            jcc_i = i;
    }
    T_ASSERT(t, materialized);
    T_ASSERT(t, alloca_i != UINT32_MAX && jcc_i != UINT32_MAX);
    T_ASSERT(t, xb->insts[jcc_i].flags_src > alloca_i);
    T_ASSERT_EQ_INT(t, xb->insts[xb->insts[jcc_i].flags_src].op, X64_OP_TEST);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    arena_free_all(&a);
}

void test_x64_isel_arithmetic_clobber_materializes_pending_compare(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    IrType params[1] = {IRT_I32};
    BlockId entry, yes, no;
    IrBuilder b;
    ValueId condition;
    X64Func *xf;
    const X64Block *xb;
    u32 i, add_i = UINT32_MAX, test_i = UINT32_MAX, jcc_i = UINT32_MAX;
    bool materialized = false;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    f = ir_func_new(m, "arithmetic_flags", IRT_I32, params, 1);
    entry = ir_block_new(m, f, "entry");
    yes = ir_block_new(m, f, "yes");
    no = ir_block_new(m, f, "no");
    ir_builder_at(&b, m, f, entry);
    condition = ir_build_icmp(&b, ICMP_SGT, ir_op_value(f, f->param_vals[0]),
                              ir_op_iconst(IRT_I32, 0));
    (void)ir_build2(&b, IR_IADD, IRT_I32, ir_op_value(f, f->param_vals[0]),
                    ir_op_iconst(IRT_I32, 1));
    ir_build_condbr(&b, ir_op_value(f, condition), yes, NULL, 0, no, NULL, 0);
    ir_builder_at(&b, m, f, yes);
    {
        IrOperand one = ir_op_iconst(IRT_I32, 1);

        ir_build_ret(&b, &one);
    }
    ir_builder_at(&b, m, f, no);
    {
        IrOperand zero = ir_op_iconst(IRT_I32, 0);

        ir_build_ret(&b, &zero);
    }

    xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
    xb = &xf->blocks[0];
    for (i = 0; i < xb->n; i++) {
        const X64Inst *x = &xb->insts[i];

        if (x->op == X64_OP_SETCC)
            materialized = true;
        if (x->op == X64_OP_ADD)
            add_i = i;
        if (x->op == X64_OP_TEST)
            test_i = i;
        if (x->op == X64_OP_JCC)
            jcc_i = i;
    }
    T_ASSERT(t, materialized);
    T_ASSERT(t, add_i != UINT32_MAX && test_i > add_i && jcc_i > test_i);
    T_ASSERT_EQ_INT(t, xb->insts[jcc_i].flags_src, test_i);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    arena_free_all(&a);
}

void test_x64_isel_opaque_flag_clobbers_materialize_pending_compare(TestCtx *t)
{
    enum ClobberKind { CLOBBER_XADD, CLOBBER_CMPXCHG, CLOBBER_ASM_CC };
    static const struct {
        enum ClobberKind kind;
        X64Op op;
    } cases[] = {
        {CLOBBER_XADD, X64_OP_XADD},
        {CLOBBER_CMPXCHG, X64_OP_CMPXCHG},
        {CLOBBER_ASM_CC, X64_OP_ASM},
    };
    u32 c;

    for (c = 0; c < CGF_ARRAY_LEN(cases); c++) {
        Arena a;
        EmitFix fx = {0};
        DiagCtx *dc;
        DiagSink sink;
        IrModule *m;
        IrFunc *f;
        IrType params[2] = {IRT_I32, IRT_PTR};
        BlockId entry, yes, no;
        IrBuilder b;
        IrOperand value, ptr;
        ValueId condition;
        X64Func *xf;
        const X64Block *xb;
        u32 i, effect_i = UINT32_MAX, test_i = UINT32_MAX;
        u32 jcc_i = UINT32_MAX;
        bool materialized = false;

        arena_init(&a);
        dc = diag_ctx_new(&a);
        sink.handle = e_sink;
        sink.user = &fx;
        diag_set_sink(dc, sink);
        m = ir_module_new(&a, dc);
        f = ir_func_new(m, "opaque_flags", IRT_I32, params, 2);
        entry = ir_block_new(m, f, "entry");
        yes = ir_block_new(m, f, "yes");
        no = ir_block_new(m, f, "no");
        ir_builder_at(&b, m, f, entry);
        value = ir_op_value(f, f->param_vals[0]);
        ptr = ir_op_value(f, f->param_vals[1]);
        condition =
            ir_build_icmp(&b, ICMP_SGT, value, ir_op_iconst(IRT_I32, 0));
        switch (cases[c].kind) {
        case CLOBBER_XADD:
            (void)ir_build_atomicrmw(&b, RMW_ADD, IRT_I32, ptr, value);
            break;
        case CLOBBER_CMPXCHG:
            (void)ir_build_cmpxchg(&b, IRT_I32, ptr, value,
                                   ir_op_iconst(IRT_I32, 1));
            break;
        case CLOBBER_ASM_CC: {
            IrAsm as = {0};

            as.tmpl = "";
            as.is_volatile = true;
            as.clobbers_cc = true;
            ir_build_asm(&b, ir_asm_new(m, &as), NULL, 0);
            break;
        }
        }
        ir_build_condbr(&b, ir_op_value(f, condition), yes, NULL, 0, no, NULL,
                        0);
        ir_builder_at(&b, m, f, yes);
        {
            IrOperand one = ir_op_iconst(IRT_I32, 1);

            ir_build_ret(&b, &one);
        }
        ir_builder_at(&b, m, f, no);
        {
            IrOperand zero = ir_op_iconst(IRT_I32, 0);

            ir_build_ret(&b, &zero);
        }

        xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
        xb = &xf->blocks[0];
        for (i = 0; i < xb->n; i++) {
            const X64Inst *x = &xb->insts[i];

            if (x->op == X64_OP_SETCC)
                materialized = true;
            if (x->op == cases[c].op) {
                effect_i = i;
                T_ASSERT(t, (x->flags & X64IF_DEFS_FLAGS) != 0);
            }
            if (x->op == X64_OP_TEST)
                test_i = i;
            if (x->op == X64_OP_JCC)
                jcc_i = i;
        }
        T_ASSERT(t, materialized);
        T_ASSERT(t,
                 effect_i != UINT32_MAX && test_i > effect_i && jcc_i > test_i);
        T_ASSERT_EQ_INT(t, xb->insts[jcc_i].flags_src, test_i);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
        T_ASSERT_EQ_INT(t, fx.errors, 0);
        arena_free_all(&a);
    }
}

void test_x64_isel_retry_loop_preserves_pending_compare_in_origin(TestCtx *t)
{
    static const IrAtomicRmw cases[] = {RMW_AND, RMW_OR, RMW_XOR};
    u32 c;

    for (c = 0; c < CGF_ARRAY_LEN(cases); c++) {
        Arena a;
        EmitFix fx = {0};
        DiagCtx *dc;
        DiagSink sink;
        IrModule *m;
        IrFunc *f;
        IrType params[2] = {IRT_I32, IRT_PTR};
        BlockId entry, yes, no;
        IrBuilder b;
        IrOperand value, ptr;
        ValueId condition;
        X64Func *xf;
        const X64Block *origin, *loop, *done;
        u32 i, origin_test_i = UINT32_MAX, setcc_i = UINT32_MAX;
        u32 cmpxchg_i = UINT32_MAX;
        u32 test_i = UINT32_MAX, jcc_i = UINT32_MAX;

        arena_init(&a);
        dc = diag_ctx_new(&a);
        sink.handle = e_sink;
        sink.user = &fx;
        diag_set_sink(dc, sink);
        m = ir_module_new(&a, dc);
        f = ir_func_new(m, "retry_flags", IRT_I32, params, 2);
        entry = ir_block_new(m, f, "entry");
        yes = ir_block_new(m, f, "yes");
        no = ir_block_new(m, f, "no");
        ir_builder_at(&b, m, f, entry);
        value = ir_op_value(f, f->param_vals[0]);
        ptr = ir_op_value(f, f->param_vals[1]);
        condition =
            ir_build_icmp(&b, ICMP_SGT, value, ir_op_iconst(IRT_I32, 0));
        (void)ir_build_atomicrmw(&b, cases[c], IRT_I32, ptr, value);
        ir_build_condbr(&b, ir_op_value(f, condition), yes, NULL, 0, no, NULL,
                        0);
        ir_builder_at(&b, m, f, yes);
        {
            IrOperand one = ir_op_iconst(IRT_I32, 1);

            ir_build_ret(&b, &one);
        }
        ir_builder_at(&b, m, f, no);
        {
            IrOperand zero = ir_op_iconst(IRT_I32, 0);

            ir_build_ret(&b, &zero);
        }

        xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
        T_ASSERT_EQ_INT(t, xf->nblocks, 5);
        origin = &xf->blocks[0];
        loop = &xf->blocks[3];
        done = &xf->blocks[4];
        for (i = 0; i < origin->n; i++) {
            if (origin->insts[i].op == X64_OP_TEST)
                origin_test_i = i;
            if (origin->insts[i].op == X64_OP_SETCC)
                setcc_i = i;
        }
        for (i = 0; i < loop->n; i++)
            if (loop->insts[i].op == X64_OP_CMPXCHG) {
                cmpxchg_i = i;
                T_ASSERT(t, (loop->insts[i].flags & X64IF_DEFS_FLAGS) != 0);
            }
        for (i = 0; i < done->n; i++) {
            if (done->insts[i].op == X64_OP_TEST)
                test_i = i;
            if (done->insts[i].op == X64_OP_JCC)
                jcc_i = i;
        }
        T_ASSERT(t, origin_test_i != UINT32_MAX && setcc_i > origin_test_i);
        T_ASSERT(t, cmpxchg_i != UINT32_MAX);
        T_ASSERT(t, test_i != UINT32_MAX && jcc_i > test_i);
        T_ASSERT_EQ_INT(t, origin->insts[setcc_i].flags_src, origin_test_i);
        T_ASSERT_EQ_INT(t, done->insts[jcc_i].flags_src, test_i);
        T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
        T_ASSERT_EQ_INT(t, fx.errors, 0);
        arena_free_all(&a);
    }
}

/* Block layout need not follow dominance order. A PTRADD in an earlier
 * layout block must reserve the stable vreg for an offset defined later,
 * rather than silently dropping the LEA index as vreg zero. */
void test_x64_isel_ptradd_reserves_forward_offset_vreg(TestCtx *t)
{
    Arena a;
    EmitFix fx = {0};
    DiagCtx *dc;
    DiagSink sink;
    IrModule *m;
    IrFunc *f;
    IrType params[2] = {IRT_PTR, IRT_I64};
    BlockId entry, use, def;
    IrBuilder b;
    ValueId offset, ptr;
    IrOperand result;
    X64Func *xf;
    X64VReg index;
    bool forward_def_found = false;
    u32 i;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = e_sink;
    sink.user = &fx;
    diag_set_sink(dc, sink);
    m = ir_module_new(&a, dc);
    f = ir_func_new(m, "forward_ptradd", IRT_PTR, params, 2);
    entry = ir_block_new(m, f, "entry");
    use = ir_block_new(m, f, "use");
    def = ir_block_new(m, f, "def");

    ir_builder_at(&b, m, f, entry);
    ir_build_br(&b, def, NULL, 0);
    ir_builder_at(&b, m, f, def);
    offset = ir_build2(&b, IR_IADD, IRT_I64, ir_op_value(f, f->param_vals[1]),
                       ir_op_iconst(IRT_I64, 1));
    ir_build_br(&b, use, NULL, 0);
    ir_builder_at(&b, m, f, use);
    ptr = ir_build_ptradd(&b, ir_op_value(f, f->param_vals[0]),
                          ir_op_value(f, offset));
    result = ir_op_value(f, ptr);
    ir_build_ret(&b, &result);

    T_ASSERT(t, ir_verify(dc, m));
    xf = x64_isel_function(m, f, &a, X64_PIC_NONE);
    T_ASSERT_EQ_INT(t, xf->blocks[1].insts[0].op, X64_OP_LEA);
    index = xf->blocks[1].insts[0].a.mem.index;
    T_ASSERT(t, index.v != 0);
    for (i = 0; i < xf->blocks[2].n; i++)
        if (xf->blocks[2].insts[i].def.v == index.v)
            forward_def_found = true;
    T_ASSERT(t, forward_def_found);
    T_ASSERT_EQ_INT(t, x64_mir_verify(xf, dc), 0);
    T_ASSERT_EQ_INT(t, fx.errors, 0);
    arena_free_all(&a);
}
