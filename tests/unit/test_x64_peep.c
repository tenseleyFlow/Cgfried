#include <string.h>

#include "cg/x86_64/peep.h"
#include "diag.h"
#include "unit.h"
#include "util/arena.h"

static X64VReg preg(X64Reg r)
{
    X64VReg v = {(u32)r + 1};
    return v;
}

static X64Operand oreg(X64Reg r)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_VREG;
    o.r = preg(r);
    return o;
}

static X64Operand oimm(i64 value)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_IMM;
    o.imm = value;
    return o;
}

static X64Inst ins(u16 op, X64Width width)
{
    X64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.width = width;
    return in;
}

static void init_func(X64Func *f, Arena *a, u32 nblocks)
{
    memset(f, 0, sizeof(*f));
    f->name = "peep";
    f->arena = a;
    f->allocated = true;
    f->blocks =
        arena_alloc(a, nblocks * sizeof(*f->blocks), _Alignof(X64Block));
    memset(f->blocks, 0, nblocks * sizeof(*f->blocks));
    f->nblocks = nblocks;
    f->cap_blocks = nblocks;
}

static void set_block(X64Func *f, u32 bi, const X64Inst *insts, u32 n)
{
    X64Block *b = &f->blocks[bi];

    b->insts = arena_alloc(f->arena, n * sizeof(*b->insts), _Alignof(X64Inst));
    memcpy(b->insts, insts, n * sizeof(*b->insts));
    b->n = n;
    b->cap = n;
}

void test_x64_peep_self_mov_cmp_zero_and_flags_remap(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[3];

    arena_init(&a);
    init_func(&f, &a, 1);
    v[0] = ins(X64_OP_CMP, X64_Q);
    v[0].flags = X64IF_DEFS_FLAGS;
    v[0].a = oreg(X64_RAX);
    v[0].b = oimm(0);
    v[1] = ins(X64_OP_MOV, X64_Q);
    v[1].def = preg(X64_RBX);
    v[1].a = oreg(X64_RBX);
    v[2] = ins(X64_OP_JCC, X64_Q);
    v[2].flags = X64IF_USES_FLAGS;
    v[2].flags_src = 0;
    v[2].cc = X64_CC_E;
    set_block(&f, 0, v, 3);

    T_ASSERT(t, x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, X64_OP_TEST);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].b.kind, X64O_VREG);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].b.r.v, X64_RAX + 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].flags_src, 0);
    T_ASSERT(t, !x64_peep_once(&f));

    v[0] = ins(X64_OP_MOV, X64_L);
    v[0].def = preg(X64_RAX);
    v[0].a = oreg(X64_RAX);
    set_block(&f, 0, v, 1);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    arena_free_all(&a);
}

void test_x64_peep_lea_multiply_and_flags_live_rejection(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[4];
    u32 shift;

    arena_init(&a);
    init_func(&f, &a, 1);
    v[0] = ins(X64_OP_MOV, X64_Q);
    v[0].def = preg(X64_RAX);
    v[0].a = oreg(X64_RCX);
    v[1] = ins(X64_OP_SHL, X64_Q);
    v[1].flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
    v[1].def = preg(X64_RAX);
    v[1].a = oreg(X64_RAX);
    v[1].b = oimm(1);
    v[2] = ins(X64_OP_ADD, X64_Q);
    v[2].flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
    v[2].def = preg(X64_RAX);
    v[2].a = oreg(X64_RAX);
    v[2].b = oreg(X64_RCX);
    v[3] = ins(X64_OP_RET, X64_Q);
    for (shift = 1; shift <= 3; shift++) {
        v[1].b.imm = shift;
        set_block(&f, 0, v, 4);
        T_ASSERT(t, x64_peep_once(&f));
        T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
        T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, X64_OP_LEA);
        T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].a.mem.base.v, X64_RCX + 1);
        T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].a.mem.index.v, X64_RCX + 1);
        T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].a.mem.scale, 1u << shift);
    }

    v[1].b.imm = 2;
    set_block(&f, 0, v, 4);
    f.blocks[0].insts[3] = ins(X64_OP_JCC, X64_Q);
    f.blocks[0].insts[3].flags = X64IF_USES_FLAGS;
    f.blocks[0].insts[3].flags_src = 2;
    f.blocks[0].insts[3].cc = X64_CC_NE;
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 4);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, X64_OP_MOV);
    arena_free_all(&a);
}

void test_x64_peep_push_pop_window_and_rsp_rejection(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[3];

    arena_init(&a);
    init_func(&f, &a, 1);
    v[0] = ins(X64_OP_PUSH, X64_Q);
    v[0].a = oreg(X64_RAX);
    v[1] = ins(X64_OP_MOV, X64_Q);
    v[1].def = preg(X64_R8);
    v[1].a = oreg(X64_R9);
    v[2] = ins(X64_OP_POP, X64_Q);
    v[2].def = preg(X64_RCX);
    set_block(&f, 0, v, 3);
    T_ASSERT(t, x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, X64_OP_MOV);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].def.v, X64_RCX + 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].a.r.v, X64_RAX + 1);

    v[1] = ins(X64_OP_LOAD, X64_Q);
    v[1].def = preg(X64_R8);
    v[1].a.kind = X64O_MEM;
    v[1].a.mem.base = preg(X64_RSP);
    v[1].a.mem.scale = 1;
    set_block(&f, 0, v, 3);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 3);
    arena_free_all(&a);
}

void test_x64_peep_jmp_next_block_only(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst j, ret;

    arena_init(&a);
    init_func(&f, &a, 3);
    j = ins(X64_OP_JMP, X64_Q);
    j.target = 2;
    ret = ins(X64_OP_RET, X64_Q);
    set_block(&f, 0, &j, 1);
    set_block(&f, 1, &ret, 1);
    set_block(&f, 2, &ret, 1);
    T_ASSERT(t, x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 0);

    j.target = 3;
    set_block(&f, 0, &j, 1);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    arena_free_all(&a);
}

static void make_setcc_branch(X64Inst v[5])
{
    v[0] = ins(X64_OP_CMP, X64_Q);
    v[0].flags = X64IF_DEFS_FLAGS;
    v[0].a = oreg(X64_RAX);
    v[0].b = oreg(X64_RBX);
    v[1] = ins(X64_OP_SETCC, X64_B);
    v[1].flags = X64IF_USES_FLAGS;
    v[1].flags_src = 0;
    v[1].cc = X64_CC_L;
    v[1].def = preg(X64_RCX);
    v[2] = ins(X64_OP_MOVZX, X64_L);
    v[2].src_width = X64_B;
    v[2].def = preg(X64_RCX);
    v[2].a = oreg(X64_RCX);
    v[3] = ins(X64_OP_TEST, X64_L);
    v[3].flags = X64IF_DEFS_FLAGS;
    v[3].a = oreg(X64_RCX);
    v[3].b = oreg(X64_RCX);
    v[4] = ins(X64_OP_JCC, X64_Q);
    v[4].flags = X64IF_USES_FLAGS;
    v[4].flags_src = 3;
    v[4].cc = X64_CC_NE;
    v[4].target = 2;
    v[4].target2 = 3;
}

void test_x64_peep_setcc_refusion_and_liveout_rejection(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[5], ret, use;

    arena_init(&a);
    init_func(&f, &a, 3);
    make_setcc_branch(v);
    ret = ins(X64_OP_RET, X64_Q);
    set_block(&f, 0, v, 5);
    set_block(&f, 1, &ret, 1);
    set_block(&f, 2, &ret, 1);
    T_ASSERT(t, x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, X64_OP_JCC);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].cc, X64_CC_L);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].flags_src, 0);

    make_setcc_branch(v);
    set_block(&f, 0, v, 5);
    use = ins(X64_OP_MOV, X64_Q);
    use.def = preg(X64_RDX);
    use.a = oreg(X64_RCX);
    set_block(&f, 1, &use, 1);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 5);
    arena_free_all(&a);
}

void test_x64_peep_setcc_refusion_requires_matching_widths(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[5], direct[4], ret;

    arena_init(&a);
    init_func(&f, &a, 3);
    ret = ins(X64_OP_RET, X64_Q);
    set_block(&f, 1, &ret, 1);
    set_block(&f, 2, &ret, 1);

    /* Without MOVZX, SETCC defines only one byte.  A wider TEST observes
     * upper bits that the SETCC did not define and cannot be re-fused. */
    make_setcc_branch(v);
    direct[0] = v[0];
    direct[1] = v[1];
    direct[2] = v[3];
    direct[3] = v[4];
    direct[3].flags_src = 2;
    set_block(&f, 0, direct, 4);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 4);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, X64_OP_SETCC);

    /* A byte-to-word extension does not define the high half tested by a
     * 32-bit TEST either.  The extension and TEST widths must agree. */
    make_setcc_branch(v);
    v[2].width = X64_W;
    set_block(&f, 0, v, 5);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 5);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[2].width, X64_W);
    arena_free_all(&a);
}

void test_x64_peep_setcc_refusion_uses_complete_switch_cfg(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[5], switch_block[3], use_block[2], ret, jump_table;

    arena_init(&a);
    init_func(&f, &a, 5);
    ret = ins(X64_OP_RET, X64_Q);
    use_block[0] = ins(X64_OP_MOV, X64_Q);
    use_block[0].def = preg(X64_RDX);
    use_block[0].a = oreg(X64_RCX);
    use_block[1] = ret;

    /* Switch compare trees branch in the middle of a block.  RCX escapes
     * through that edge even though the block's final JMP reaches a path
     * where it is dead. */
    make_setcc_branch(v);
    v[4].target = 2;
    v[4].target2 = 2;
    set_block(&f, 0, v, 5);
    switch_block[0] = ins(X64_OP_CMP, X64_L);
    switch_block[0].flags = X64IF_DEFS_FLAGS;
    switch_block[0].a = oreg(X64_RAX);
    switch_block[0].b = oreg(X64_RBX);
    switch_block[1] = ins(X64_OP_JCC, X64_Q);
    switch_block[1].flags = X64IF_USES_FLAGS;
    switch_block[1].flags_src = 0;
    switch_block[1].cc = X64_CC_E;
    switch_block[1].target = 4;
    switch_block[2] = ins(X64_OP_JMP, X64_Q);
    switch_block[2].target = 5;
    set_block(&f, 1, switch_block, 3);
    set_block(&f, 2, &ret, 1);
    set_block(&f, 3, use_block, 2);
    set_block(&f, 4, &ret, 1);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 5);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, X64_OP_SETCC);

    /* A dense switch fans out through every JMPTBL target rather than the
     * target/target2 fields.  The shared liveness view owns that side table
     * and must keep the SETCC result live here too. */
    make_setcc_branch(v);
    v[4].target = 2;
    v[4].target2 = 2;
    set_block(&f, 0, v, 5);
    jump_table = ins(X64_OP_JMPTBL, X64_Q);
    jump_table.table = 0;
    set_block(&f, 1, &jump_table, 1);
    f.tables = arena_alloc(&a, sizeof(*f.tables), _Alignof(X64Table));
    f.ntables = 1;
    f.cap_tables = 1;
    f.tables[0].targets =
        arena_alloc(&a, 2 * sizeof(*f.tables[0].targets), _Alignof(u32));
    f.tables[0].targets[0] = 4;
    f.tables[0].targets[1] = 5;
    f.tables[0].n = 2;
    set_block(&f, 2, &ret, 1);
    set_block(&f, 3, use_block, 2);
    set_block(&f, 4, &ret, 1);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 5);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, X64_OP_SETCC);
    arena_free_all(&a);
}

void test_x64_peep_zext_self_mov_requires_provenance(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[3];

    arena_init(&a);
    init_func(&f, &a, 1);
    v[0] = ins(X64_OP_ADD, X64_L);
    v[0].flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
    v[0].def = preg(X64_RAX);
    v[0].a = oreg(X64_RAX);
    v[0].b = oreg(X64_RBX);
    v[1] = ins(X64_OP_MOV, X64_L);
    v[1].def = preg(X64_RAX);
    v[1].a = oreg(X64_RAX);
    set_block(&f, 0, v, 2);
    T_ASSERT(t, x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 1);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, X64_OP_ADD);

    /* A 64-bit producer does not prove that the high half may be discarded;
     * the movl is the i32->i64 zero extension and must remain. */
    v[0].width = X64_Q;
    set_block(&f, 0, v, 2);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].n, 2);
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[1].op, X64_OP_MOV);

    /* B/W MOVZX forms extend genuinely narrow values and are never erased
     * by the L-write provenance rule. */
    v[2] = ins(X64_OP_MOVZX, X64_L);
    v[2].src_width = X64_B;
    v[2].def = preg(X64_RAX);
    v[2].a = oreg(X64_RAX);
    set_block(&f, 0, &v[2], 1);
    T_ASSERT(t, !x64_peep_once(&f));
    T_ASSERT_EQ_INT(t, f.blocks[0].insts[0].op, X64_OP_MOVZX);
    arena_free_all(&a);
}

typedef struct VerifyFix {
    int errors;
} VerifyFix;

static void verify_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        ((VerifyFix *)user)->errors++;
}

void test_x64_verify_flags_src_requires_defining_producer(TestCtx *t)
{
    Arena a;
    X64Func f;
    X64Inst v[2];
    DiagCtx *dc;
    DiagSink sink;
    VerifyFix fix = {0};

    arena_init(&a);
    init_func(&f, &a, 1);
    dc = diag_ctx_new(&a);
    sink.handle = verify_sink;
    sink.user = &fix;
    diag_set_sink(dc, sink);
    v[0] = ins(X64_OP_MOV, X64_Q); /* deliberately not DEFS_FLAGS */
    v[0].def = preg(X64_RAX);
    v[0].a = oreg(X64_RBX);
    v[1] = ins(X64_OP_JCC, X64_Q);
    v[1].flags = X64IF_USES_FLAGS;
    v[1].flags_src = 0;
    v[1].cc = X64_CC_E;
    set_block(&f, 0, v, 2);
    T_ASSERT(t, x64_mir_verify(&f, dc) > 0);
    T_ASSERT(t, fix.errors > 0);
    arena_free_all(&a);
}
