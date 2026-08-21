#include "unit.h"

#include "cg/arm64/mir.h"
#include "cg/arm64/peep.h"

#include <string.h>

static bool treg_eq(A64Reg a, A64Reg b)
{
    return a.id == b.id && a.physical == b.physical;
}

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

static A64Inst binop(u16 op, A64Sf sf, A64Reg dst, A64Reg lhs, A64Operand rhs)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.sf = sf;
    in.nops = 3;
    in.ops[0] = treg(dst);
    in.ops[1] = treg(lhs);
    in.ops[2] = rhs;
    return in;
}

static A64Inst addr_op(A64Reg dst, u32 sym, i64 off)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_ADDR;
    in.sf = A64_SF64;
    in.nops = off ? 3 : 2;
    in.ops[0] = treg(dst);
    in.ops[1].kind = A64O_SYM;
    in.ops[1].id = sym;
    if (off)
        in.ops[2] = timm(off);
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

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, a64_phys(A64_V0), a64_phys(A64_X0), 0,
                           4, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, a64_phys(A64_V1), a64_phys(A64_X0), 4,
                           4, A64_ADDR_SCALED));
    T_ASSERT(t, !a64_peep_pair_mem(&f));
    arena_free_all(&a);
}

void test_a64_peep_post_ra_mov_and_add_zero(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg x0 = a64_phys(A64_X0);
    A64Reg x1 = a64_phys(A64_X1);
    A64Reg fp = a64_phys(A64_X29);
    A64Reg sp = a64_phys(A64_SP);
    A64Inst mov;
    A64Inst adds;

    arena_init(&a);
    init_func(&f, &a, 1);
    memset(&mov, 0, sizeof(mov));
    mov.op = A64_OP_MOV;
    mov.sf = A64_SF64;
    mov.nops = 2;
    mov.ops[0] = treg(x0);
    mov.ops[1] = treg(x0);
    a64_block_append(&f, &f.blocks[0], mov);
    mov.sf = A64_SF32;
    a64_block_append(&f, &f.blocks[0], mov);
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF64, x1, x0, timm(0)));
    adds = binop(A64_OP_ADDS, A64_SF64, x0, x1, timm(0));
    adds.flags = A64IF_DEFS_NZCV;
    a64_block_append(&f, &f.blocks[0], adds);
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF64, fp, sp, timm(0)));

    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 4);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_MOV);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].sf, A64_SF32);
    T_ASSERT(t, treg_eq(f.blocks[0].insts[0].ops[0].reg, x0));
    T_ASSERT(t, treg_eq(f.blocks[0].insts[0].ops[1].reg, x0));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_MOV);
    T_ASSERT(t, treg_eq(f.blocks[0].insts[1].ops[0].reg, x1));
    T_ASSERT(t, treg_eq(f.blocks[0].insts[1].ops[1].reg, x0));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[2].op, A64_OP_ADDS);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[3].op, A64_OP_ADD);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    arena_free_all(&a);
}

void test_a64_peep_post_ra_addr_cse_and_clobber(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg x0 = a64_phys(A64_X0);
    A64Reg x1 = a64_phys(A64_X1);
    A64Reg x2 = a64_phys(A64_X2);

    arena_init(&a);
    init_func(&f, &a, 1);
    a64_block_append(&f, &f.blocks[0], addr_op(x0, 1, 24));
    a64_block_append(&f, &f.blocks[0], addr_op(x1, 1, 24));
    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_MOV);
    T_ASSERT(t, treg_eq(f.blocks[0].insts[1].ops[1].reg, x0));

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0], addr_op(x0, 1, 24));
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF64, x0, x2, timm(1)));
    a64_block_append(&f, &f.blocks[0], addr_op(x1, 1, 24));
    T_ASSERT(t, !a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[2].op, A64_OP_ADDR);
    arena_free_all(&a);
}

void test_a64_peep_post_ra_w_self_mov_requires_provenance(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg x0 = a64_phys(A64_X0);
    A64Reg x1 = a64_phys(A64_X1);
    A64Reg x2 = a64_phys(A64_X2);
    A64Inst mov;

    arena_init(&a);
    init_func(&f, &a, 1);
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF32, x0, x1, timm(1)));
    memset(&mov, 0, sizeof(mov));
    mov.op = A64_OP_MOV;
    mov.sf = A64_SF32;
    mov.nops = 2;
    mov.ops[0] = treg(x0);
    mov.ops[1] = treg(x0);
    a64_block_append(&f, &f.blocks[0], mov);
    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_ADD);

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF64, x0, x1, timm(1)));
    a64_block_append(&f, &f.blocks[0], mov);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].sf, A64_SF32);

    /* Pairing runs after local rewrites on each fixpoint iteration.  Once
     * these stores become STP, its first transfer register must not be
     * mistaken for a W-register definition on the next iteration: stores do
     * not clear the source register's upper half. */
    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, x0, x2, 0, 4, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_STORE, x1, x2, 4, 4, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0], mov);
    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_STP);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_MOV);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].sf, A64_SF32);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    arena_free_all(&a);
}

void test_a64_peep_post_ra_madd_and_flags_guard(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg x0 = a64_phys(A64_X0);
    A64Reg x1 = a64_phys(A64_X1);
    A64Reg x2 = a64_phys(A64_X2);
    A64Reg x3 = a64_phys(A64_X3);
    A64Inst adds;

    arena_init(&a);
    init_func(&f, &a, 1);
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_MUL, A64_SF64, x0, x1, treg(x2)));
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF64, x0, x0, treg(x3)));
    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_MADD);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].nops, 4);
    T_ASSERT(t, treg_eq(f.blocks[0].insts[0].ops[3].reg, x3));

    f.blocks[0].n = 0;
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_MUL, A64_SF64, x0, x1, treg(x2)));
    adds = binop(A64_OP_ADDS, A64_SF64, x0, x0, treg(x3));
    adds.flags = A64IF_DEFS_NZCV;
    a64_block_append(&f, &f.blocks[0], adds);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_ADDS);
    arena_free_all(&a);
}

void test_a64_peep_post_ra_production_pairing(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg base = a64_phys(A64_X10);
    A64Reg x0 = a64_phys(A64_X0);
    A64Reg x1 = a64_phys(A64_X1);

    arena_init(&a);
    init_func(&f, &a, 1);
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, x0, base, 0, 8, A64_ADDR_SCALED));
    a64_block_append(&f, &f.blocks[0],
                     memop(A64_OP_LOAD, x1, base, 8, 8, A64_ADDR_SCALED));
    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_LDP);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    arena_free_all(&a);
}

void test_a64_peep_post_ra_keeps_signed_extension(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Reg x0 = a64_phys(A64_X0);
    A64Reg x1 = a64_phys(A64_X1);

    arena_init(&a);
    init_func(&f, &a, 1);
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ADD, A64_SF32, x0, x1, timm(1)));
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_LSL, A64_SF64, x0, x0, timm(32)));
    a64_block_append(&f, &f.blocks[0],
                     binop(A64_OP_ASR, A64_SF64, x0, x0, timm(32)));

    /* W writes zero the high half, but a signed i32->i64 conversion must
     * replicate bit 31.  The LSL/ASR pair is required for 0x80000000 and is
     * never treated as the UXTW-shaped W self-copy. */
    T_ASSERT(t, !a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 3);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, A64_OP_LSL);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[2].op, A64_OP_ASR);
    arena_free_all(&a);
}

void test_a64_peep_post_ra_layout_branches(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst branch;

    arena_init(&a);
    init_func(&f, &a, 4);
    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_B;
    branch.nops = 1;
    branch.ops[0] = tlabel(2);
    a64_block_append(&f, &f.blocks[0], branch);

    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_BCOND;
    branch.cond = A64_CC_EQ;
    branch.flags = A64IF_USES_NZCV;
    branch.nops = 2;
    branch.ops[0] = tlabel(3); /* taken edge is the layout successor */
    branch.ops[1] = tlabel(4);
    a64_block_append(&f, &f.blocks[1], branch);

    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_B;
    branch.nops = 1;
    branch.ops[0] = tlabel(4);
    a64_block_append(&f, &f.blocks[2], branch);
    branch.ops[0] = tlabel(1); /* last block has no layout successor */
    a64_block_append(&f, &f.blocks[3], branch);

    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 0);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].op, A64_OP_BCOND);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].cond, A64_CC_NE);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].ops[0].id, 4);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].ops[1].id, 3);
    T_ASSERT_EQ_INT(t, f.blocks[2].n, 0);
    T_ASSERT_EQ_INT(t, f.blocks[3].n, 1);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    arena_free_all(&a);
}

void test_a64_peep_post_ra_keeps_midblock_fallthrough_branch(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst branch;
    A64Inst mov;

    arena_init(&a);
    init_func(&f, &a, 2);
    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_B;
    branch.nops = 1;
    branch.ops[0] = tlabel(2);
    a64_block_append(&f, &f.blocks[0], branch);
    memset(&mov, 0, sizeof(mov));
    mov.op = A64_OP_MOV;
    mov.sf = A64_SF64;
    mov.nops = 2;
    mov.ops[0] = treg(a64_phys(A64_X0));
    mov.ops[1] = treg(a64_phys(A64_X1));
    a64_block_append(&f, &f.blocks[0], mov);

    T_ASSERT(t, !a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_B);
    arena_free_all(&a);
}

void test_a64_peep_post_ra_layout_zero_and_bit_branches(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst branch;

    arena_init(&a);
    init_func(&f, &a, 4);
    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_CBZ;
    branch.sf = A64_SF64;
    branch.nops = 3;
    branch.ops[0] = treg(a64_phys(A64_X0));
    branch.ops[1] = tlabel(2);
    branch.ops[2] = tlabel(4);
    a64_block_append(&f, &f.blocks[0], branch);

    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_TBZ;
    branch.sf = A64_SF64;
    branch.nops = 4;
    branch.ops[0] = treg(a64_phys(A64_X1));
    branch.ops[1] = timm(5);
    branch.ops[2] = tlabel(3);
    branch.ops[3] = tlabel(4);
    a64_block_append(&f, &f.blocks[1], branch);

    T_ASSERT(t, a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_CBNZ);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[1].id, 4);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[2].id, 2);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].op, A64_OP_TBNZ);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].ops[2].id, 4);
    T_ASSERT_EQ_INT(t, f.blocks[1].insts[0].ops[3].id, 3);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    arena_free_all(&a);
}

void test_a64_peep_post_ra_keeps_out_of_range_layout_inversion(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst branch;
    A64Inst pad;
    u32 i;

    arena_init(&a);
    init_func(&f, &a, 4);
    memset(&branch, 0, sizeof(branch));
    branch.op = A64_OP_TBZ;
    branch.sf = A64_SF64;
    branch.nops = 4;
    branch.ops[0] = treg(a64_phys(A64_X0));
    branch.ops[1] = timm(3);
    branch.ops[2] = tlabel(2); /* near taken edge is the layout successor */
    branch.ops[3] = tlabel(4); /* false edge is outside imm14 range */
    a64_block_append(&f, &f.blocks[0], branch);
    memset(&pad, 0, sizeof(pad));
    pad.op = A64_OP_ATOMIC_LLSC; /* conservatively estimated as 64 bytes */
    for (i = 0; i < 513; i++)
        a64_block_append(&f, &f.blocks[2], pad);

    T_ASSERT(t, !a64_peep_post_ra(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, A64_OP_TBZ);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[2].id, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].ops[3].id, 4);
    arena_free_all(&a);
}

static void append_layout_bit_branch(A64Func *f, u16 op)
{
    A64Inst branch;

    memset(&branch, 0, sizeof(branch));
    branch.op = op;
    branch.sf = A64_SF64;
    branch.nops = 4;
    branch.ops[0] = treg(a64_phys(A64_X0));
    branch.ops[1] = timm(3);
    branch.ops[2] = tlabel(2); /* layout successor */
    branch.ops[3] = tlabel(4); /* proposed narrow target */
    a64_block_append(f, &f->blocks[0], branch);
}

static void assert_layout_bit_branch_unchanged(TestCtx *t, const A64Func *f,
                                               u16 op)
{
    T_ASSERT_EQ_INT(t, f->blocks[0].n, 1);
    T_ASSERT_EQ_INT(t, f->blocks[0].insts[0].op, op);
    T_ASSERT_EQ_INT(t, f->blocks[0].insts[0].ops[2].id, 2);
    T_ASSERT_EQ_INT(t, f->blocks[0].insts[0].ops[3].id, 4);
}

void test_a64_peep_layout_range_accounts_for_emission_pseudos(TestCtx *t)
{
    Arena a;
    A64Func f;
    A64Inst pad;
    u32 i;

    arena_init(&a);

    /* ADDR's former 16-byte estimate puts 1170 pseudos plus the branch at
     * 18732 bytes, apparently inside TBZ's +32764 limit. The real 28-byte
     * worst case reaches 32772, so layout inversion must be declined. */
    init_func(&f, &a, 4);
    append_layout_bit_branch(&f, A64_OP_TBZ);
    memset(&pad, 0, sizeof(pad));
    pad.op = A64_OP_ADDR;
    for (i = 0; i < 1170; i++)
        a64_block_append(&f, &f.blocks[2], pad);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    assert_layout_bit_branch_unchanged(t, &f, A64_OP_TBZ);

    /* TLSADDR's former 16-byte estimate likewise puts 1024 pseudos at only
     * 16396 bytes including the branch. Its real 32-byte maximum reaches
     * 32780, just beyond TBNZ's range. Exercise the inverse opcode too. */
    init_func(&f, &a, 4);
    append_layout_bit_branch(&f, A64_OP_TBNZ);
    pad.op = A64_OP_TLSADDR;
    for (i = 0; i < 1024; i++)
        a64_block_append(&f, &f.blocks[2], pad);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    assert_layout_bit_branch_unchanged(t, &f, A64_OP_TBNZ);

    /* CAS emits seven instructions on the Armv8.0 LL/SC path. */
    init_func(&f, &a, 4);
    append_layout_bit_branch(&f, A64_OP_TBZ);
    pad.op = A64_OP_ATOMIC_CAS;
    for (i = 0; i < 1170; i++)
        a64_block_append(&f, &f.blocks[2], pad);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    assert_layout_bit_branch_unchanged(t, &f, A64_OP_TBZ);

    /* Inline asm is copied verbatim and has no finite MIR-side size bound.
     * Its presence makes layout inversion unprovable even for a short text
     * template, so the conservative answer is always to retain the branch. */
    init_func(&f, &a, 4);
    append_layout_bit_branch(&f, A64_OP_TBZ);
    pad.op = A64_OP_ASM;
    a64_block_append(&f, &f.blocks[2], pad);
    T_ASSERT(t, !a64_peep_post_ra(&f));
    assert_layout_bit_branch_unchanged(t, &f, A64_OP_TBZ);

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
