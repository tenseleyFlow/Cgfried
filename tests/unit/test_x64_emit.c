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
    x64_emit_globals(m, out);
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
