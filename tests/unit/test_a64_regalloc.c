#include "unit.h"

#include "cg/arm64/mir.h"

#include <stdlib.h>
#include <string.h>

/* Sprint 48: the A64 register allocator. The def/use table is the part that
 * fails silently rather than loudly — wrong liveness is a miscompile, not a
 * crash — so it is tested through the observable liveness sets rather than
 * by reaching into static helpers. */

static A64Operand treg(A64Reg r)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_REG;
    o.reg = r;
    return o;
}

static A64Operand tmem(A64Reg base, A64Reg index)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_MEM;
    o.mem.base = base;
    o.mem.index = index;
    o.mem.size = 8;
    o.mem.mode = index.id ? A64_ADDR_REG_LSL : A64_ADDR_SCALED;
    return o;
}

static A64Operand timm(i64 v)
{
    A64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = A64O_IMM;
    o.imm = v;
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

static void init_func(A64Func *f, Arena *a, u32 nblocks)
{
    memset(f, 0, sizeof(*f));
    f->arena = a;
    f->blocks =
        arena_alloc(a, nblocks * sizeof(*f->blocks), _Alignof(A64Block));
    memset(f->blocks, 0, nblocks * sizeof(*f->blocks));
    f->nblocks = nblocks;
    f->cap_blocks = nblocks;
    f->name = "t";
}

static void put(A64Func *f, u32 bi, u16 op, u8 nops, A64Operand a, A64Operand b,
                A64Operand c)
{
    A64Inst in;

    memset(&in, 0, sizeof(in));
    in.op = op;
    in.sf = A64_SF64;
    in.nops = nops;
    in.ops[0] = a;
    in.ops[1] = b;
    in.ops[2] = c;
    a64_block_append(f, &f->blocks[bi], in);
}

static bool live_in_has(const A64Func *f, const u64 *live_in, u32 words,
                        u32 block, A64Reg v)
{
    (void)f;
    return (live_in[(size_t)block * words + (v.id >> 6)] >> (v.id & 63)) & 1;
}

/* An address operand's base and index are uses on EVERY instruction,
 * including one whose operand 0 is a definition. Missing that makes a
 * pointer look dead across its own dereference. */
void test_a64_regalloc_memory_operands_are_uses(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg base, index, dst, other;
    u64 *live_in, *live_out;
    u32 words;

    arena_init(&arena);
    init_func(&f, &arena, 2);
    base = a64_newv(&f, A64RC_GP);
    index = a64_newv(&f, A64RC_GP);
    dst = a64_newv(&f, A64RC_GP);
    other = a64_newv(&f, A64RC_GP);

    put(&f, 0, A64_OP_B, 1, tlabel(2), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));
    /* load dst, [base, index] — dst is defined here, base and index are read */
    put(&f, 1, A64_OP_LOAD, 2, treg(dst), tmem(base, index),
        treg((A64Reg){0, 0}));
    put(&f, 1, A64_OP_RET, 1, treg(dst), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    words = a64_liveness_words(&f);
    live_in = calloc((size_t)words * f.nblocks, sizeof(u64));
    live_out = calloc((size_t)words * f.nblocks, sizeof(u64));
    a64_liveness(&f, live_in, live_out);

    T_ASSERT(t, live_in_has(&f, live_in, words, 1, base));
    T_ASSERT(t, live_in_has(&f, live_in, words, 1, index));
    /* and the entry block sees them flow in, since nothing defines them */
    T_ASSERT(t, live_in_has(&f, live_in, words, 0, base));
    T_ASSERT(t, live_in_has(&f, live_in, words, 0, index));
    /* dst is defined before its use, so it never enters live-in */
    T_ASSERT(t, !live_in_has(&f, live_in, words, 1, dst));
    T_ASSERT(t, !live_in_has(&f, live_in, words, 1, other));

    free(live_out);
    free(live_in);
    arena_free_all(&arena);
}

/* store's operand 0 is the stored VALUE, a use. Reading it as a definition
 * would kill the value at exactly the instruction that consumes it. */
void test_a64_regalloc_store_operand_zero_is_a_use(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg value, addr;
    u64 *live_in, *live_out;
    u32 words;

    arena_init(&arena);
    init_func(&f, &arena, 2);
    value = a64_newv(&f, A64RC_GP);
    addr = a64_newv(&f, A64RC_GP);

    put(&f, 0, A64_OP_B, 1, tlabel(2), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));
    put(&f, 1, A64_OP_STORE, 2, treg(value), tmem(addr, (A64Reg){0, 0}),
        treg((A64Reg){0, 0}));
    put(&f, 1, A64_OP_RET, 0, treg((A64Reg){0, 0}), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    words = a64_liveness_words(&f);
    live_in = calloc((size_t)words * f.nblocks, sizeof(u64));
    live_out = calloc((size_t)words * f.nblocks, sizeof(u64));
    a64_liveness(&f, live_in, live_out);

    T_ASSERT(t, live_in_has(&f, live_in, words, 1, value));
    T_ASSERT(t, live_in_has(&f, live_in, words, 1, addr));
    T_ASSERT(t, live_in_has(&f, live_in, words, 0, value));

    free(live_out);
    free(live_in);
    arena_free_all(&arena);
}

/* movk reads its own destination: the 16-bit insert leaves the other bits
 * alone, so the register is live BEFORE the instruction as well as after. */
void test_a64_regalloc_movk_reads_its_destination(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg acc;
    u64 *live_in, *live_out;
    u32 words;

    arena_init(&arena);
    init_func(&f, &arena, 2);
    acc = a64_newv(&f, A64RC_GP);

    put(&f, 0, A64_OP_B, 1, tlabel(2), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));
    put(&f, 1, A64_OP_MOVK, 2, treg(acc), timm(7), treg((A64Reg){0, 0}));
    put(&f, 1, A64_OP_RET, 1, treg(acc), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    words = a64_liveness_words(&f);
    live_in = calloc((size_t)words * f.nblocks, sizeof(u64));
    live_out = calloc((size_t)words * f.nblocks, sizeof(u64));
    a64_liveness(&f, live_in, live_out);

    T_ASSERT(t, live_in_has(&f, live_in, words, 1, acc));

    free(live_out);
    free(live_in);
    arena_free_all(&arena);
}

/* The AAPCS64 preservation contract, stated once and tested directly so
 * Sprint 49's 128-bit values inherit it rather than rediscovering it. */
void test_a64_regalloc_callee_saved_cost_model(TestCtx *t)
{
    T_ASSERT(t, a64_reg_is_callee_saved_gp(A64_X19));
    T_ASSERT(t, a64_reg_is_callee_saved_gp(A64_X28));
    T_ASSERT(t, !a64_reg_is_callee_saved_gp(A64_X18));
    T_ASSERT(t, !a64_reg_is_callee_saved_gp(A64_X9));
    T_ASSERT(t, !a64_reg_is_callee_saved_gp(A64_X29));

    T_ASSERT(t, a64_reg_preserved_across_call(A64_X19, false));
    T_ASSERT(t, !a64_reg_preserved_across_call(A64_X0, false));
    T_ASSERT(t, !a64_reg_preserved_across_call(A64_X18, false));
    /* d8-d15 survive a call; q8-q15 do NOT — only the low halves are the
     * callee's responsibility. */
    T_ASSERT(t, a64_reg_preserved_across_call(A64_V8, false));
    T_ASSERT(t, a64_reg_preserved_across_call(A64_V15, false));
    T_ASSERT(t, !a64_reg_preserved_across_call(A64_V8, true));
    T_ASSERT(t, !a64_reg_preserved_across_call(A64_V15, true));
    T_ASSERT(t, !a64_reg_preserved_across_call(A64_V16, false));
    T_ASSERT(t, !a64_reg_preserved_across_call(A64_V0, false));
}

/* SP alignment is checked in hardware at every SP-based access, so the frame
 * size is rounded, never merely assumed. */
void test_a64_regalloc_frame_total_rounds_to_sixteen(TestCtx *t)
{
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(16, 0, 0), 16);
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(16, 8, 0), 32);
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(16, 16, 0), 32);
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(24, 8, 0), 32);
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(16, 500, 0), 528);
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(16, 0, 8), 32);
    T_ASSERT_EQ_INT(t, (long long)a64_frame_total(0, 0, 0) % 16, 0);
}

static bool block_has_vregs(const A64Block *b)
{
    u32 ii, i;

    for (ii = 0; ii < b->n; ii++)
        for (i = 0; i < b->insts[ii].nops; i++) {
            const A64Operand *op = &b->insts[ii].ops[i];

            if (op->kind == A64O_REG && op->reg.id && !op->reg.physical)
                return true;
            if (op->kind == A64O_MEM) {
                if (op->mem.base.id && !op->mem.base.physical)
                    return true;
                if (op->mem.index.id && !op->mem.index.physical)
                    return true;
            }
        }
    return false;
}

/* End to end: every virtual register is coloured, the canonical prologue
 * opens the function and the mirrored epilogue precedes the return. */
void test_a64_regalloc_allocates_and_frames(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg a, b, sum;
    const A64Block *entry;
    u32 bi;

    arena_init(&arena);
    init_func(&f, &arena, 1);
    a = a64_newv(&f, A64RC_GP);
    b = a64_newv(&f, A64RC_GP);
    sum = a64_newv(&f, A64RC_GP);

    put(&f, 0, A64_OP_MOVZ, 2, treg(a), timm(1), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_MOVZ, 2, treg(b), timm(2), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_ADD, 3, treg(sum), treg(a), treg(b));
    put(&f, 0, A64_OP_RET, 1, treg(sum), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    a64_regalloc(&f);

    T_ASSERT(t, f.allocated);
    T_ASSERT_EQ_INT(t, (long long)f.frame_bytes % 16, 0);
    for (bi = 0; bi < f.nblocks; bi++)
        T_ASSERT(t, !block_has_vregs(&f.blocks[bi]));

    entry = &f.blocks[0];
    T_ASSERT(t, entry->n >= 3);
    /* stp x29, x30, [sp, #-FRAME]! then the mov-from-SP alias */
    T_ASSERT_EQ_INT(t, (long long)entry->insts[0].op, A64_OP_STP);
    T_ASSERT_EQ_INT(t, (long long)entry->insts[0].ops[2].mem.mode,
                    A64_ADDR_PRE);
    T_ASSERT_EQ_INT(t, (long long)entry->insts[1].op, A64_OP_ADD);
    T_ASSERT_EQ_INT(t, (long long)entry->insts[1].ops[0].reg.id,
                    (u32)A64_X29 + 1);
    T_ASSERT_EQ_INT(t, (long long)entry->insts[1].ops[1].reg.id,
                    (u32)A64_SP + 1);
    /* ldp x29, x30, [sp], #FRAME immediately before the return */
    T_ASSERT_EQ_INT(t, (long long)entry->insts[entry->n - 1].op, A64_OP_RET);
    T_ASSERT_EQ_INT(t, (long long)entry->insts[entry->n - 2].op, A64_OP_LDP);
    T_ASSERT_EQ_INT(t, (long long)entry->insts[entry->n - 2].ops[2].mem.mode,
                    A64_ADDR_POST);

    arena_free_all(&arena);
}

/* CGF_SPILL_ALL is the canary lane: every value round-trips through memory,
 * so reload/spill placement is exercised on ordinary programs. */
void test_a64_regalloc_spill_all_lane(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg a, b, sum;
    u32 bi, saved_set = 0;
    const char *old = getenv("CGF_SPILL_ALL");

    if (!old)
        setenv("CGF_SPILL_ALL", "1", 1);
    else
        saved_set = 1;

    arena_init(&arena);
    init_func(&f, &arena, 1);
    a = a64_newv(&f, A64RC_GP);
    b = a64_newv(&f, A64RC_GP);
    sum = a64_newv(&f, A64RC_GP);
    put(&f, 0, A64_OP_MOVZ, 2, treg(a), timm(1), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_MOVZ, 2, treg(b), timm(2), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_ADD, 3, treg(sum), treg(a), treg(b));
    put(&f, 0, A64_OP_RET, 1, treg(sum), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    a64_regalloc(&f);

    T_ASSERT(t, f.spill_bytes >= 24);
    for (bi = 0; bi < f.nblocks; bi++)
        T_ASSERT(t, !block_has_vregs(&f.blocks[bi]));
    arena_free_all(&arena);

    if (!saved_set)
        unsetenv("CGF_SPILL_ALL");
}

/* AAPCS64 stage C: NGRN and NSRN advance independently, so an interleaved
 * argument list still fills x0.. and v0.. in their own orders. The result
 * arrives in x0 and is copied out to an ordinary value. */
void test_a64_regalloc_marshals_aapcs64_arguments(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg a, b, d, e, res;
    A64CallInfo *call;
    A64Inst in;
    const A64Block *bb;
    u32 ii, seen = 0;

    arena_init(&arena);
    init_func(&f, &arena, 1);
    a = a64_newv(&f, A64RC_GP);
    b = a64_newv(&f, A64RC_GP);
    d = a64_newv_width(&f, A64RC_FP, A64_SF64);
    e = a64_newv(&f, A64RC_GP);
    res = a64_newv(&f, A64RC_GP);

    put(&f, 0, A64_OP_MOVZ, 2, treg(a), timm(1), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_MOVZ, 2, treg(b), timm(2), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_FMOV, 2, treg(d), treg(d), treg((A64Reg){0, 0}));
    put(&f, 0, A64_OP_MOVZ, 2, treg(e), timm(3), treg((A64Reg){0, 0}));

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_CALL;
    in.sf = A64_SF64;
    a64_block_append(&f, &f.blocks[0], in);
    call = a64_call_info_new(&f, &f.blocks[0].insts[f.blocks[0].n - 1],
                             FUNCREF_EXTERNAL, 0, (A64Reg){0, 0}, res, IRT_I64,
                             IR_ABIRET_NONE, false, false);
    a64_call_add_arg(&f, call, a, IRT_I64, 0);
    a64_call_add_arg(&f, call, d, IRT_F64, 0);
    a64_call_add_arg(&f, call, b, IRT_I64, 0);
    a64_call_add_arg(&f, call, e, IRT_I64, 0);

    put(&f, 0, A64_OP_RET, 1, treg(res), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    a64_regalloc(&f);

    bb = &f.blocks[0];
    for (ii = 0; ii < bb->n; ii++)
        if (bb->insts[ii].op == A64_OP_CALL) {
            const A64CallInfo *c = bb->insts[ii].call;

            seen = 1;
            T_ASSERT_EQ_INT(t, (long long)c->nargs, 4);
            /* integers take x0, x1, x2 in order; the double takes v0 and
             * consumes none of the general registers */
            T_ASSERT_EQ_INT(t, (long long)c->args[0].value.id,
                            (long long)A64_X0 + 1);
            T_ASSERT_EQ_INT(t, (long long)c->args[1].value.id,
                            (long long)A64_V0 + 1);
            T_ASSERT_EQ_INT(t, (long long)c->args[2].value.id,
                            (long long)A64_X1 + 1);
            T_ASSERT_EQ_INT(t, (long long)c->args[3].value.id,
                            (long long)A64_X2 + 1);
            T_ASSERT(t, c->args[0].value.physical);
            T_ASSERT_EQ_INT(t, (long long)c->result.id, (long long)A64_X0 + 1);
            T_ASSERT(t, c->result.physical);
        }
    T_ASSERT(t, seen);
    T_ASSERT_EQ_INT(t, (long long)f.out_args, 0);
    arena_free_all(&arena);
}

/* Nine or more integer arguments force the stack case. The outgoing area
 * must own SP+0 at the `bl` — that is where the callee reads it — so the
 * saved x29/x30 pair moves up and the frame is allocated by a separate
 * subtraction instead of the one-instruction pre-index form. */
void test_a64_regalloc_stack_arguments_use_the_two_step_frame(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg v[10], res;
    A64CallInfo *call;
    A64Inst in;
    const A64Block *bb;
    u32 i, ii, stores = 0;

    arena_init(&arena);
    init_func(&f, &arena, 1);
    for (i = 0; i < 10; i++) {
        v[i] = a64_newv(&f, A64RC_GP);
        put(&f, 0, A64_OP_MOVZ, 2, treg(v[i]), timm((i64)i),
            treg((A64Reg){0, 0}));
    }
    res = a64_newv(&f, A64RC_GP);

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_CALL;
    in.sf = A64_SF64;
    a64_block_append(&f, &f.blocks[0], in);
    call = a64_call_info_new(&f, &f.blocks[0].insts[f.blocks[0].n - 1],
                             FUNCREF_EXTERNAL, 0, (A64Reg){0, 0}, res, IRT_I64,
                             IR_ABIRET_NONE, false, false);
    for (i = 0; i < 10; i++)
        a64_call_add_arg(&f, call, v[i], IRT_I64, 0);
    put(&f, 0, A64_OP_RET, 1, treg(res), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    a64_regalloc(&f);

    /* two eight-byte slots, rounded to the 16-byte SP granule */
    T_ASSERT_EQ_INT(t, (long long)f.out_args, 16);
    T_ASSERT_EQ_INT(t, (long long)(f.frame_bytes % 16), 0);

    bb = &f.blocks[0];
    /* sub sp, sp, #FRAME ; stp x29, x30, [sp, #16] ; add x29, sp, #16 */
    T_ASSERT_EQ_INT(t, (long long)bb->insts[0].op, A64_OP_SUB);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[0].ops[0].reg.id,
                    (long long)A64_SP + 1);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[0].ops[2].imm,
                    (long long)f.frame_bytes);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[1].op, A64_OP_STP);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[1].ops[2].mem.mode,
                    A64_ADDR_SCALED);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[1].ops[2].mem.offset, 16);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[2].op, A64_OP_ADD);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[2].ops[0].reg.id,
                    (long long)A64_X29 + 1);
    T_ASSERT_EQ_INT(t, (long long)bb->insts[2].ops[2].imm, 16);

    /* exactly two outgoing stores, at [sp, #0] and [sp, #8] */
    for (ii = 0; ii < bb->n; ii++) {
        const A64Inst *cur = &bb->insts[ii];

        if (cur->op == A64_OP_STORE && cur->ops[1].kind == A64O_MEM &&
            cur->ops[1].mem.base.physical &&
            cur->ops[1].mem.base.id == (u32)A64_SP + 1) {
            T_ASSERT_EQ_INT(t, (long long)cur->ops[1].mem.offset,
                            (long long)(stores * 8));
            stores++;
        }
    }
    T_ASSERT_EQ_INT(t, (long long)stores, 2);

    /* the first eight still take x0-x7 in order */
    for (ii = 0; ii < bb->n; ii++)
        if (bb->insts[ii].op == A64_OP_CALL) {
            const A64CallInfo *c = bb->insts[ii].call;

            T_ASSERT_EQ_INT(t, (long long)c->nargs, 8);
            for (i = 0; i < 8; i++)
                T_ASSERT_EQ_INT(t, (long long)c->args[i].value.id,
                                (long long)A64_X0 + 1 + (long long)i);
        }
    arena_free_all(&arena);
}

/* Regression: a pre-coloured argument copy must not clobber a value the
 * allocator parked in that same register.
 *
 * Eight values are defined in order, so the natural colouring is v0->x0 ...
 * v7->x7, and then they are passed in REVERSE. That misalignment is what
 * exposes the bug: the first copy is `mov x0, <v7's home>` while v0 still
 * lives in x0 and is not read until the eighth copy. Without a fixed-interval
 * clash test v0 keeps x0, the first copy destroys it, and the eighth copy
 * passes v7's value where v0's belongs. Passing the arguments in order hides
 * this completely — every copy degenerates to `mov xN, xN`, which is why a
 * naive two-argument test reports success either way. */
void test_a64_regalloc_argument_copies_do_not_clobber_each_other(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg v[8], res;
    A64CallInfo *call;
    A64Inst in;
    const A64Block *bb;
    u32 i, ii;
    u8 home[8];
    bool dead[A64_REG_COUNT];

    arena_init(&arena);
    init_func(&f, &arena, 1);
    for (i = 0; i < 8; i++) {
        v[i] = a64_newv(&f, A64RC_GP);
        put(&f, 0, A64_OP_MOVZ, 2, treg(v[i]), timm((i64)i + 1),
            treg((A64Reg){0, 0}));
    }
    res = a64_newv(&f, A64RC_GP);

    memset(&in, 0, sizeof(in));
    in.op = A64_OP_CALL;
    in.sf = A64_SF64;
    a64_block_append(&f, &f.blocks[0], in);
    call = a64_call_info_new(&f, &f.blocks[0].insts[f.blocks[0].n - 1],
                             FUNCREF_EXTERNAL, 0, (A64Reg){0, 0}, res, IRT_I64,
                             IR_ABIRET_NONE, false, false);
    for (i = 8; i-- > 0;)
        a64_call_add_arg(&f, call, v[i], IRT_I64, 0);
    put(&f, 0, A64_OP_RET, 1, treg(res), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    a64_regalloc(&f);

    /* Recover each value's home from the movz that defines it, then walk the
     * copies: no copy may read a register an earlier copy already wrote. */
    bb = &f.blocks[0];
    memset(home, 0, sizeof(home));
    memset(dead, 0, sizeof(dead));
    i = 0;
    for (ii = 0; ii < bb->n && i < 8; ii++)
        if (bb->insts[ii].op == A64_OP_MOVZ) {
            T_ASSERT(t, bb->insts[ii].ops[0].reg.physical);
            home[i++] = (u8)(bb->insts[ii].ops[0].reg.id - 1);
        }
    T_ASSERT_EQ_INT(t, (long long)i, 8);

    for (ii = 0; ii < bb->n; ii++) {
        const A64Inst *cur = &bb->insts[ii];

        if (cur->op == A64_OP_CALL)
            break;
        if (cur->op != A64_OP_MOV || cur->nops != 2 ||
            cur->ops[1].kind != A64O_REG || !cur->ops[1].reg.physical)
            continue;
        T_ASSERT(t, !dead[cur->ops[1].reg.id - 1]);
        dead[cur->ops[0].reg.id - 1] = true;
    }
    /* and every value still had a distinct home to begin with */
    for (i = 0; i < 8; i++) {
        u32 k;

        for (k = i + 1; k < 8; k++)
            T_ASSERT(t, home[i] != home[k]);
    }
    arena_free_all(&arena);
}

/* A variadic function reserves the 192-byte register save area, dumps the
 * registers the named parameters did not consume, and resolves va_start's
 * three pointer fields once the frame size is known.
 *
 * The arithmetic that matters: the first unnamed general argument must land
 * exactly where __gr_offs starts reaching from __gr_top, or every va_arg is
 * off by a register. */
void test_a64_regalloc_variadic_save_area(TestCtx *t)
{
    Arena arena;
    A64Func f;
    A64Reg ap;
    const A64Block *bb;
    A64Inst in;
    u32 ii, gp_saved = 0, fp_saved = 0, gr_top = 0, vr_top = 0;
    u32 first_unnamed_gp = 0;
    bool seen_gr = false, seen_vr = false;

    arena_init(&arena);
    init_func(&f, &arena, 1);
    f.variadic = true;
    f.va_named_gp = 1; /* one named integer parameter, as in `f(int n, ...)` */
    f.va_named_fp = 0;
    ap = a64_newv(&f, A64RC_GP);

    put(&f, 0, A64_OP_MOVZ, 2, treg(ap), timm(0), treg((A64Reg){0, 0}));
    memset(&in, 0, sizeof(in));
    in.op = A64_OP_VASTART;
    in.sf = A64_SF64;
    in.nops = 1;
    in.ops[0] = treg(ap);
    a64_block_append(&f, &f.blocks[0], in);
    put(&f, 0, A64_OP_RET, 0, treg((A64Reg){0, 0}), treg((A64Reg){0, 0}),
        treg((A64Reg){0, 0}));

    a64_regalloc(&f);

    /* 16 for the saved pair plus the 192-byte save area, rounded */
    T_ASSERT(t, f.frame_bytes >= 192 + 16);
    T_ASSERT_EQ_INT(t, (long long)(f.frame_bytes % 16), 0);

    bb = &f.blocks[0];
    for (ii = 0; ii < bb->n; ii++) {
        const A64Inst *cur = &bb->insts[ii];
        u32 reg;

        if (cur->op != A64_OP_STORE || cur->ops[1].kind != A64O_MEM ||
            !cur->ops[1].mem.base.physical ||
            cur->ops[1].mem.base.id != (u32)A64_SP + 1)
            continue;
        reg = cur->ops[0].reg.id - 1;
        if (reg <= A64_X7) {
            if (!gp_saved)
                first_unnamed_gp = (u32)cur->ops[1].mem.offset;
            gp_saved++;
            /* the named parameter's register is dead, never saved */
            T_ASSERT(t, reg != A64_X0);
        } else if (reg >= A64_V0 && reg <= A64_V7) {
            fp_saved++;
        }
    }
    T_ASSERT_EQ_INT(t, (long long)gp_saved, 7); /* x1-x7 */
    T_ASSERT_EQ_INT(t, (long long)fp_saved, 8); /* v0-v7 */

    /* va_start's stores: [ap,#8] is __gr_top, [ap,#16] is __vr_top; both are
     * formed from x29, which sits `out_args` bytes into the frame. */
    for (ii = 0; ii < bb->n; ii++) {
        const A64Inst *cur = &bb->insts[ii];

        if (cur->op == A64_OP_ADD && cur->ops[1].kind == A64O_REG &&
            cur->ops[1].reg.physical &&
            cur->ops[1].reg.id == (u32)A64_X29 + 1 &&
            cur->ops[2].kind == A64O_IMM && cur->ops[0].reg.physical &&
            cur->ops[0].reg.id == (u32)A64_X16 + 1) {
            if (!seen_gr) {
                gr_top = (u32)cur->ops[2].imm;
                seen_gr = true;
            } else if (!seen_vr) {
                vr_top = (u32)cur->ops[2].imm;
                seen_vr = true;
            }
        }
    }
    T_ASSERT(t, seen_gr && seen_vr);
    T_ASSERT_EQ_INT(t, (long long)vr_top, (long long)f.frame_bytes);
    T_ASSERT_EQ_INT(t, (long long)gr_top, (long long)f.frame_bytes - 128);
    /* THE invariant: __gr_top - 8*(8 - named) is the first saved register. */
    T_ASSERT_EQ_INT(t, (long long)(gr_top - 8 * (8 - f.va_named_gp)),
                    (long long)first_unnamed_gp);
    arena_free_all(&arena);
}
