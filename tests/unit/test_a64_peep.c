#include "unit.h"

#include "cg/arm64/mir.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

bool a64_branch_delta_fits(u16 op, i64 delta);

static A64Operand treg(A64Reg r)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_REG;
    o.reg = r;
    return o;
}

static A64Operand tmem(A64Reg base, i64 off, u8 size, u8 mode)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_MEM;
    o.mem.base = base;
    o.mem.offset = off;
    o.mem.size = size;
    o.mem.mode = mode;
    return o;
}

static A64Operand tlabel(u32 id)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_LABEL;
    o.id = id;
    return o;
}

static A64Operand timm(i64 imm)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_IMM;
    o.imm = imm;
    return o;
}

static A64Inst memop(u16 op, A64Reg rt, A64Reg base, i64 off, u8 size, u8 mode)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.sf = size == 8 ? A64_SF64 : A64_SF32;
    in.nops = 2;
    in.ops[0] = treg(rt);
    in.ops[1] = tmem(base, off, size, mode);
    return in;
}

static void init_func(A64Func *f, Arena *a, u32 nblocks)
{
    memset(f, 0, sizeof(*f));
    f->arena = a;
    f->blocks =
        arena_alloc(a, nblocks * sizeof(*f->blocks), _Alignof(A64Block));
    memset(f->blocks, 0, nblocks * sizeof(*f->blocks));
    f->nblocks = nblocks;
    f->cap_blocks = nblocks;
}

void test_a64_peep_pair_mem_legality(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg base = {10, 0}, r1 = {11, 0}, r2 = {12, 0};

    arena_init(&a);
    init_func(&f, &a, 1);
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r1, base, -512, 8, A64_ADDR_UNSCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r2, base, -504, 8, A64_ADDR_UNSCALED));
    T_ASSERT(t, a64_peep_pair_mem(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_LDP);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].nops, 3);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[2].mem.offset, -512);

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, r1, base, 504, 8, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, r2, base, 512, 8, A64_ADDR_SCALED));
    T_ASSERT(t, a64_peep_pair_mem(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_STP);

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r1, base, 512, 8, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r2, base, 520, 8, A64_ADDR_SCALED));
    T_ASSERT(t, !a64_peep_pair_mem(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, base, base, 0, 8, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r2, base, 8, 8, A64_ADDR_SCALED));
    T_ASSERT(t, !a64_peep_pair_mem(&f));

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r1, base, 0, 8, A64_ADDR_PRE));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, r2, base, 8, 8, A64_ADDR_SCALED));
    T_ASSERT(t, !a64_peep_pair_mem(&f));

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, r1, base, 0, 8, A64_ADDR_SCALED));
    f.blocks[0].insts[0].flags = A64IF_VOLATILE;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, r2, base, 8, 8, A64_ADDR_SCALED));
    T_ASSERT(t, !a64_peep_pair_mem(&f));
    arena_free_all(&a);
}

void test_a64_relax_tbz_conservative_range(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst tb, pad, ret;
    A64Reg tested = {7, 0};
    u32 i;

    arena_init(&a);
    init_func(&f, &a, 2);
    memset(&tb, 0, sizeof(tb));
    tb.op = A64_OP_TBZ;
    tb.sf = A64_SF64;
    tb.nops = 3;
    tb.ops[0] = treg(tested);
    tb.ops[1] = timm(40);
    tb.ops[2] = tlabel(2);
    a64_block_append(&f, &f.blocks[0], tb);
    memset(&pad, 0, sizeof(pad));
    pad.op = A64_OP_ATOMIC_LLSC; /* conservatively estimated as 64 bytes */
    for (i = 0; i < 513; i++)
        a64_block_append(&f, &f.blocks[0], pad);
    memset(&ret, 0, sizeof(ret));
    ret.op = A64_OP_RET;
    a64_block_append(&f, &f.blocks[1], ret);

    T_ASSERT(t, a64_relax_branches(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_ANDS);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_BCOND);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].cond, A64_CC_EQ);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[2].imm,
                    (i64)(UINT64_C(1) << 40));
    T_ASSERT(t, !a64_relax_branches(&f));
    arena_free_all(&a);
}

void test_a64_relax_tbz_exact_boundary(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst tb, pad, ret;
    u32 i;

    arena_init(&a);
    init_func(&f, &a, 2);
    memset(&tb, 0, sizeof(tb));
    tb.op = A64_OP_TBNZ;
    tb.sf = A64_SF64;
    tb.nops = 3;
    tb.ops[0] = treg((A64Reg){7, 0});
    tb.ops[1] = timm(63);
    tb.ops[2] = tlabel(2);
    a64_block_append(&f, &f.blocks[0], tb);
    memset(&pad, 0, sizeof(pad));
    pad.op = A64_OP_MOV;
    for (i = 0; i < 8190; i++)
        a64_block_append(&f, &f.blocks[0], pad);
    memset(&ret, 0, sizeof(ret));
    ret.op = A64_OP_RET;
    a64_block_append(&f, &f.blocks[1], ret);

    T_ASSERT(t, !a64_relax_branches(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_TBNZ);
    a64_block_append(&f, &f.blocks[0], pad);
    T_ASSERT(t, a64_relax_branches(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_ANDS);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[2].imm, INT64_MIN);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_BCOND);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].cond, A64_CC_NE);
    arena_free_all(&a);
}

void test_a64_relax_two_edge_layout_accounting(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst tb, cond, pad, ret;
    u32 i;

    arena_init(&a);
    init_func(&f, &a, 3);
    memset(&tb, 0, sizeof(tb));
    tb.op = A64_OP_TBZ;
    tb.sf = A64_SF64;
    tb.nops = 3;
    tb.ops[0] = treg((A64Reg){7, 0});
    tb.ops[1] = timm(1);
    tb.ops[2] = tlabel(3);
    a64_block_append(&f, &f.blocks[0], tb);
    memset(&cond, 0, sizeof(cond));
    cond.op = A64_OP_BCOND;
    cond.cond = A64_CC_EQ;
    cond.nops = 2;
    cond.ops[0] = tlabel(2);
    cond.ops[1] = tlabel(3);
    a64_block_append(&f, &f.blocks[0], cond);
    memset(&pad, 0, sizeof(pad));
    pad.op = A64_OP_MOV;
    for (i = 0; i < 8189; i++)
        a64_block_append(&f, &f.blocks[0], pad);
    memset(&ret, 0, sizeof(ret));
    ret.op = A64_OP_RET;
    a64_block_append(&f, &f.blocks[1], ret);
    a64_block_append(&f, &f.blocks[2], ret);

    /* 4-byte TB + 8-byte two-edge conditional + 8189*4 = 32768.  Counting
     * the conditional as one instruction would incorrectly retain TBZ at
     * the architectural +32764 limit. */
    T_ASSERT(t, a64_relax_branches(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_ANDS);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_BCOND);
    arena_free_all(&a);
}

void test_a64_branch_exact_ranges(TestCtx *t)
{
    T_ASSERT(t, a64_branch_delta_fits(A64_OP_B, -134217728));
    T_ASSERT(t, a64_branch_delta_fits(A64_OP_B, 134217724));
    T_ASSERT(t, !a64_branch_delta_fits(A64_OP_B, -134217732));
    T_ASSERT(t, !a64_branch_delta_fits(A64_OP_B, 134217728));
    T_ASSERT(t, !a64_branch_delta_fits(A64_OP_B, 2));
    T_ASSERT(t, a64_branch_delta_fits(A64_OP_BCOND, -1048576));
    T_ASSERT(t, a64_branch_delta_fits(A64_OP_BCOND, 1048572));
    T_ASSERT(t, a64_branch_delta_fits(A64_OP_TBZ, -32768));
    T_ASSERT(t, a64_branch_delta_fits(A64_OP_TBZ, 32764));
}

static void external_b_child(bool false_edge)
{
    Arena a;
    A64Func f;
    A64Inst branch;

    arena_init(&a);
    init_func(&f, &a, 1);
    memset(&branch, 0, sizeof(branch));
    branch.op = false_edge ? A64_OP_BCOND : A64_OP_B;
    branch.nops = false_edge ? 2 : 1;
    branch.ops[0] = false_edge ? tlabel(1) : (A64Operand){0};
    if (!false_edge)
        branch.ops[0].kind = A64O_SYM;
    branch.ops[false_edge ? 1 : 0].kind = A64O_SYM;
    branch.ops[false_edge ? 1 : 0].id = 1;
    a64_block_append(&f, &f.blocks[0], branch);
    (void)a64_relax_branches(&f);
    _exit(0);
}

void test_a64_relax_rejects_external_b(TestCtx *t)
{
    bool false_edge;

    for (false_edge = false;; false_edge = true) {
        int fd[2];
        pid_t pid;
        int status = 0;
        char err[256];
        size_t used = 0;

        if (pipe(fd) != 0) {
            T_ASSERT(t, false);
            return;
        }
        fflush(NULL);
        pid = fork();
        T_ASSERT(t, pid >= 0);
        if (pid == 0) {
            close(fd[0]);
            if (dup2(fd[1], STDERR_FILENO) < 0)
                _exit(99);
            close(fd[1]);
            external_b_child(false_edge);
        }
        close(fd[1]);
        if (pid < 0) {
            close(fd[0]);
            return;
        }
        while (used + 1 < sizeof(err)) {
            ssize_t n = read(fd[0], err + used, sizeof(err) - used - 1);

            if (n > 0) {
                used += (size_t)n;
                continue;
            }
            if (n < 0 && errno == EINTR)
                continue;
            break;
        }
        err[used] = '\0';
        close(fd[0]);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
            ;
        T_ASSERT(t, WIFEXITED(status));
        if (WIFEXITED(status))
            T_ASSERT_EQ_INT(t, WEXITSTATUS(status), 4);
        T_ASSERT(t, strstr(err, false_edge
                                    ? "false-edge branch target"
                                    : "external unconditional branch") != NULL);
        if (false_edge)
            break;
    }
}

void test_a64_relax_conditional_fixpoint(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst br, pad, ret;
    u32 i;

    arena_init(&a);
    init_func(&f, &a, 3);
    memset(&br, 0, sizeof(br));
    br.op = A64_OP_BCOND;
    br.cond = A64_CC_LT;
    br.flags = A64IF_USES_NZCV;
    br.nops = 2;
    br.ops[0] = tlabel(3);
    br.ops[1] = tlabel(2);
    a64_block_append(&f, &f.blocks[0], br);
    memset(&pad, 0, sizeof(pad));
    pad.op = A64_OP_ATOMIC_LLSC;
    for (i = 0; i < 16385; i++)
        a64_block_append(&f, &f.blocks[1], pad);
    memset(&ret, 0, sizeof(ret));
    ret.op = A64_OP_RET;
    a64_block_append(&f, &f.blocks[2], ret);

    T_ASSERT(t, a64_relax_branches(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 3);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_BCOND);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].cond, A64_CC_GE);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[0].kind, A64O_IMM);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[0].imm, 8);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_B);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].ops[0].id, 3);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[2].op, A64_OP_B);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[2].ops[0].id, 2);
    T_ASSERT(t, !a64_relax_branches(&f));
    arena_free_all(&a);
}
