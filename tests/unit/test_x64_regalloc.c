#include <stdlib.h>
#include <string.h>

#include "cg/cg.h"
#include "cg/shared.h"
#include "unit.h"
#include "util/arena.h"

/* Sprint 22 units: the two-address hazard table (3 cases x enough
 * opcodes), the frame alignment law, block liveness on hand-built MIR,
 * the one-allocator-at-every-opt-level invariant, and the sprint's
 * secret weapon — a tiny MIR interpreter running 200 random
 * straight-line functions in vreg form and post-RA form and demanding
 * identical results. */

/* --- MIR construction helpers ----------------------------------------------
 */

static X64Func *mkf(Arena *a, u32 nblocks)
{
    X64Func *f = arena_alloc(a, sizeof(X64Func), _Alignof(X64Func));

    memset(f, 0, sizeof(*f));
    f->arena = a;
    f->name = "u";
    f->blocks = arena_alloc(a, nblocks * sizeof(X64Block), _Alignof(X64Block));
    memset(f->blocks, 0, nblocks * sizeof(X64Block));
    f->nblocks = nblocks;
    f->cap_blocks = nblocks;
    return f;
}

static X64Inst *put(X64Func *f, u32 bi, u16 op, u8 w)
{
    X64Block *b = &f->blocks[bi];
    X64Inst *in;

    if (b->n == b->cap) {
        u32 nc = b->cap ? b->cap * 2 : 32;
        X64Inst *ni =
            arena_alloc(f->arena, nc * sizeof(X64Inst), _Alignof(X64Inst));

        if (b->n)
            memcpy(ni, b->insts, b->n * sizeof(X64Inst));
        b->insts = ni;
        b->cap = nc;
    }
    in = &b->insts[b->n++];
    memset(in, 0, sizeof(*in));
    in->op = op;
    in->width = w;
    return in;
}

static X64Operand ov(u32 id)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_VREG;
    o.r.v = id;
    return o;
}

static X64Operand oi(i64 v)
{
    X64Operand o;

    memset(&o, 0, sizeof(o));
    o.kind = X64O_IMM;
    o.imm = v;
    return o;
}

/* --- the invariant ----------------------------------------------------------
 */

static const u8 shared_test_pool[] = {0, 1};

static void shared_pool(void *ctx, u32 vreg, const u8 **regs, u32 *nregs)
{
    (void)ctx;
    (void)vreg;
    *regs = shared_test_pool;
    *nregs = CGF_ARRAY_LEN(shared_test_pool);
}

static bool shared_same_class(void *ctx, u32 a, u32 b)
{
    (void)ctx;
    (void)a;
    (void)b;
    return true;
}

static bool shared_reg_usable(void *ctx, u32 vreg, u8 reg, u32 start, u32 end)
{
    (void)ctx;
    (void)vreg;
    (void)reg;
    (void)start;
    (void)end;
    return true;
}

static u32 shared_spill_8(void *ctx, u32 vreg)
{
    (void)ctx;
    (void)vreg;
    return 8;
}

void test_cg_shared_linear_scan_and_spill_slots(TestCtx *t)
{
    CgInterval iv[4];
    CgLinearScanPolicy policy;
    CgSpillSlots slots = {0};

    memset(iv, 0, sizeof(iv));
    memset(&policy, 0, sizeof(policy));
    /* DESIGNATED, so a new CgInterval field does not silently shift these.
     * The positional form broke the day `no_spill` was added -- loudly, as it
     * happens, because -Werror=missing-field-initializers caught it, but only
     * because the field went at the END. One inserted in the middle would
     * have compiled and quietly re-aimed every value. */
    iv[1] = (CgInterval){.vreg = 1, .start = 0, .end = 10, .live = true};
    iv[2] = (CgInterval){.vreg = 2, .start = 1, .end = 2, .live = true};
    iv[3] = (CgInterval){.vreg = 3, .start = 1, .end = 8, .live = true};
    policy.nphys_regs = 2;
    policy.pool = shared_pool;
    policy.same_class = shared_same_class;
    policy.reg_usable = shared_reg_usable;
    policy.spill_size = shared_spill_8;
    policy.spill_align = shared_spill_8;

    cg_linear_scan(iv, 3, &policy, &slots);
    T_ASSERT_EQ_INT(t, iv[1].phys, 0); /* furthest end is evicted */
    T_ASSERT_EQ_INT(t, iv[1].slot, -8);
    T_ASSERT_EQ_INT(t, iv[2].phys, 2);
    T_ASSERT_EQ_INT(t, iv[3].phys, 1);
    T_ASSERT_EQ_INT(t, slots.count, 1);

    T_ASSERT_EQ_INT(t, cg_spill_slot_assign(&slots, 16, 16), -32);
    T_ASSERT_EQ_INT(t, cg_spill_slot_assign(&slots, 8, 8), -40);
    T_ASSERT_EQ_INT(t, slots.count, 3);
}

/* CgInterval.no_spill: an inline-asm operand constrained "r" must END UP in a
 * register, because the template names it and the spill path reloads through
 * a scratch set too small to hold several operands at once. Pinned HERE, at
 * the shared allocator, rather than only through a backend fixture -- both
 * backends depend on the rule and neither owns it.
 *
 * Two cases, because the rule has two halves that fail separately:
 * the eviction heuristic must INVERT (spill the shorter interval, which it
 * otherwise never does), and spill_all must skip it entirely. */
void test_cg_shared_no_spill_always_gets_a_register(TestCtx *t)
{
    CgInterval iv[4];
    CgLinearScanPolicy policy;
    CgSpillSlots slots = {0};

    memset(iv, 0, sizeof(iv));
    memset(&policy, 0, sizeof(policy));
    policy.nphys_regs = 2;
    policy.pool = shared_pool;
    policy.same_class = shared_same_class;
    policy.reg_usable = shared_reg_usable;
    policy.spill_size = shared_spill_8;
    policy.spill_align = shared_spill_8;

    /* THE SHAPE IS THE TEST. The unspillable interval must (a) arrive when
     * both registers are already taken, so eviction is the only way it can
     * be served, and (b) have the LONGEST end, so the ordinary heuristic --
     * spill whichever lives longer -- would pick IT. v3 is that interval;
     * without the inversion it spills and the template reads a scratch.
     *
     * The first draft of this test used the shape from the case above, where
     * the no_spill interval starts FIRST and therefore gets a register
     * without any eviction at all. It passed with the inversion mutated out.
     * Mutate every new gate. */
    iv[1] = (CgInterval){.vreg = 1, .start = 0, .end = 2, .live = true};
    iv[2] = (CgInterval){.vreg = 2, .start = 0, .end = 3, .live = true};
    iv[3] = (CgInterval){
        .vreg = 3, .start = 1, .end = 100, .live = true, .no_spill = true};
    cg_linear_scan(iv, 3, &policy, &slots);
    T_ASSERT(t, iv[3].phys != 0);
    T_ASSERT_EQ_INT(t, iv[3].slot, 0);
    T_ASSERT_EQ_INT(t, slots.count, 1); /* someone else went instead */

    /* spill_all is a stress lane, not a semantics change. */
    memset(iv, 0, sizeof(iv));
    memset(&slots, 0, sizeof(slots));
    policy.spill_all = true;
    iv[1] = (CgInterval){
        .vreg = 1, .start = 0, .end = 10, .live = true, .no_spill = true};
    iv[2] = (CgInterval){.vreg = 2, .start = 1, .end = 2, .live = true};
    cg_linear_scan(iv, 2, &policy, &slots);
    T_ASSERT(t, iv[1].phys != 0);
    T_ASSERT_EQ_INT(t, iv[1].slot, 0);
    T_ASSERT_EQ_INT(t, iv[2].phys, 0); /* everything else still spills */
    T_ASSERT(t, iv[2].slot != 0);
}

void test_x64_linear_scan_at_every_opt_level(TestCtx *t)
{
    X64RegallocFn f0 = x64_regalloc_entry(CG_O0);
    X64RegallocFn f1 = x64_regalloc_entry(CG_O1);
    X64RegallocFn f2 = x64_regalloc_entry(CG_O2);

    T_ASSERT(t, f0 == f1);
    T_ASSERT(t, f1 == f2);
    T_ASSERT(t, f0 == &x64_regalloc);
}

/* --- the alignment law ------------------------------------------------------
 */

void test_x64_frame_align_table(TestCtx *t)
{
    /* Exact expectations: pushes-parity x raw size (the sprint table). */
    static const struct {
        u32 pushes, raw, n;
    } cases[] = {
        {0, 0, 0},   {0, 8, 16},  {0, 16, 16}, {0, 24, 32}, {1, 0, 8},
        {1, 8, 8},   {1, 16, 24}, {1, 24, 24}, {2, 0, 0},   {2, 8, 16},
        {2, 16, 16}, {2, 24, 32}, {3, 0, 8},   {3, 8, 8},   {3, 16, 24},
        {3, 24, 24}, {0, 4, 16},  {1, 4, 8},   {5, 52, 56}, {2, 40, 48},
    };
    u32 i, p, raw;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        u32 n = x64_frame_align_pad(cases[i].pushes, cases[i].raw);

        T_ASSERT_EQ_INT(t, n, cases[i].n);
    }
    /* The LAW, exhaustively: rsp ends 0 mod 16 and space covers raw. */
    for (p = 0; p <= 5; p++)
        for (raw = 0; raw <= 64; raw += 4) {
            u32 n = x64_frame_align_pad(p, raw);

            T_ASSERT(t, ((n + 8 * p) % 16) == 0);
            T_ASSERT(t, n >= raw);
        }
}

/* --- liveness on hand-built MIR ---------------------------------------------
 */

static bool lbit(const u64 *w, u32 words, u32 blk, u32 v)
{
    return (w[blk * words + (v >> 6)] >> (v & 63)) & 1;
}

void test_x64_liveness_diamond(TestCtx *t)
{
    Arena a;
    X64Func *f;
    X64Inst *in;
    u32 words;
    u64 *lin, *lout;

    arena_init(&a);
    f = mkf(&a, 4);
    f->nvregs = 5;
    /* bb1: v1=$1; v2=$2; jcc bb2,bb3 */
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 1;
    in->a = oi(1);
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 2;
    in->a = oi(2);
    in = put(f, 0, X64_OP_JCC, X64_L);
    in->target = 2;
    in->target2 = 3;
    /* bb2: v3=v1; jmp bb4 */
    in = put(f, 1, X64_OP_MOV, X64_L);
    in->def.v = 3;
    in->a = ov(1);
    in = put(f, 1, X64_OP_JMP, X64_L);
    in->target = 4;
    /* bb3: v4=v2; jmp bb4 */
    in = put(f, 2, X64_OP_MOV, X64_L);
    in->def.v = 4;
    in->a = ov(2);
    in = put(f, 2, X64_OP_JMP, X64_L);
    in->target = 4;
    /* bb4: v5=v1; ret — v1 is live THROUGH both arms */
    in = put(f, 3, X64_OP_MOV, X64_L);
    in->def.v = 5;
    in->a = ov(1);
    put(f, 3, X64_OP_RET, X64_L);

    words = x64_liveness_words(f);
    lin = arena_alloc(&a, 4 * words * 8, 8);
    lout = arena_alloc(&a, 4 * words * 8, 8);
    memset(lin, 0, 4 * words * 8);
    memset(lout, 0, 4 * words * 8);
    x64_liveness(f, lin, lout);

    T_ASSERT(t, !lbit(lin, words, 0, 1)); /* defined in bb1 */
    T_ASSERT(t, !lbit(lin, words, 0, 2));
    T_ASSERT(t, lbit(lout, words, 0, 1));
    T_ASSERT(t, lbit(lout, words, 0, 2));
    T_ASSERT(t, lbit(lin, words, 1, 1));  /* bb2 uses v1 */
    T_ASSERT(t, !lbit(lin, words, 1, 2)); /* v2 only feeds bb3 */
    T_ASSERT(t, lbit(lin, words, 2, 1));  /* v1 live THROUGH bb3 */
    T_ASSERT(t, lbit(lin, words, 2, 2));
    T_ASSERT(t, lbit(lin, words, 3, 1));  /* bb4 uses v1 */
    T_ASSERT(t, !lbit(lin, words, 3, 3)); /* v3 dead by bb4 */
    arena_free_all(&a);
}

void test_x64_liveness_loop(TestCtx *t)
{
    Arena a;
    X64Func *f;
    X64Inst *in;
    u32 words;
    u64 *lin, *lout;

    arena_init(&a);
    f = mkf(&a, 3);
    f->nvregs = 4;
    /* bb1: v1=$5; v2=$0; jmp bb2 */
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 1;
    in->a = oi(5);
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 2;
    in->a = oi(0);
    in = put(f, 0, X64_OP_JMP, X64_L);
    in->target = 2;
    /* bb2: v2 = add v2,$1 (self-use); v3=v1; jcc bb2,bb3 */
    in = put(f, 1, X64_OP_ADD, X64_L);
    in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
    in->def.v = 2;
    in->a = ov(2);
    in->b = oi(1);
    in = put(f, 1, X64_OP_MOV, X64_L);
    in->def.v = 3;
    in->a = ov(1);
    in = put(f, 1, X64_OP_JCC, X64_L);
    in->target = 2;
    in->target2 = 3;
    /* bb3: v4=v2; ret */
    in = put(f, 2, X64_OP_MOV, X64_L);
    in->def.v = 4;
    in->a = ov(2);
    put(f, 2, X64_OP_RET, X64_L);

    words = x64_liveness_words(f);
    lin = arena_alloc(&a, 3 * words * 8, 8);
    lout = arena_alloc(&a, 3 * words * 8, 8);
    memset(lin, 0, 3 * words * 8);
    memset(lout, 0, 3 * words * 8);
    x64_liveness(f, lin, lout);

    T_ASSERT(t, lbit(lin, words, 1, 1)); /* v1 live around the loop */
    T_ASSERT(t, lbit(lin, words, 1, 2)); /* v2 loop-carried (self-use) */
    T_ASSERT(t, lbit(lout, words, 1, 1));
    T_ASSERT(t, lbit(lout, words, 1, 2));
    T_ASSERT(t, lbit(lin, words, 2, 2));  /* exit reads v2 */
    T_ASSERT(t, !lbit(lin, words, 2, 3)); /* v3 dead */
    arena_free_all(&a);
}

void test_x64_liveness_dead_def(TestCtx *t)
{
    Arena a;
    X64Func *f;
    X64Inst *in;
    u32 words;
    u64 *lin, *lout;

    arena_init(&a);
    f = mkf(&a, 1);
    f->nvregs = 3;
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 1;
    in->a = oi(1);
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 2; /* never used */
    in->a = oi(2);
    in = put(f, 0, X64_OP_MOV, X64_L);
    in->def.v = 3;
    in->a = ov(1);
    put(f, 0, X64_OP_RET, X64_L);

    words = x64_liveness_words(f);
    lin = arena_alloc(&a, words * 8, 8);
    lout = arena_alloc(&a, words * 8, 8);
    memset(lin, 0, words * 8);
    memset(lout, 0, words * 8);
    x64_liveness(f, lin, lout);

    T_ASSERT(t, lin[0] == 0);  /* nothing live-in to entry */
    T_ASSERT(t, lout[0] == 0); /* nothing escapes a ret block */
    arena_free_all(&a);
}

/* --- the two-address hazard table -------------------------------------------
 *
 * Driven on ALLOCATED-form MIR with forced registers (ids are
 * X64Reg + 1: rax=1 rcx=2 rdx=3; r10=11 is the reserved rescue). */

#define PRAX (X64_RAX + 1)
#define PRCX (X64_RCX + 1)
#define PRDX (X64_RDX + 1)
#define PR10 (X64_R10 + 1)

/* Build one func: single TWO_ADDR inst `def = op src1, src2vreg?`. */
static X64Func *two_addr_case(Arena *a, u16 op, u32 dst, u32 s1, u32 s2)
{
    X64Func *f = mkf(a, 1);
    X64Inst *in = put(f, 0, op, X64_Q);

    f->allocated = true;
    f->nvregs = 0;
    in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
    in->def.v = dst;
    in->a = ov(s1);
    if (s2)
        in->b = ov(s2);
    put(f, 0, X64_OP_RET, X64_Q);
    return f;
}

void test_x64_twoaddr_dst_is_src1(TestCtx *t)
{
    static const u16 ops[] = {X64_OP_ADD, X64_OP_SUB, X64_OP_IMUL, X64_OP_XOR,
                              X64_OP_SHL};
    u32 i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        Arena a;
        X64Func *f;
        const X64Block *b;

        arena_init(&a);
        f = two_addr_case(&a, ops[i], PRAX, PRAX, PRCX);
        x64_twoaddr_fixup(f);
        b = &f->blocks[0];
        T_ASSERT_EQ_INT(t, b->n, 2); /* untouched + ret */
        T_ASSERT_EQ_INT(t, b->insts[0].op, ops[i]);
        T_ASSERT_EQ_INT(t, b->insts[0].def.v, PRAX);
        T_ASSERT_EQ_INT(t, b->insts[0].a.r.v, PRAX);
        T_ASSERT_EQ_INT(t, b->insts[0].b.r.v, PRCX);
        arena_free_all(&a);
    }
}

void test_x64_twoaddr_dst_is_src2_commutative(TestCtx *t)
{
    static const u16 ops[] = {X64_OP_ADD, X64_OP_AND, X64_OP_OR, X64_OP_XOR,
                              X64_OP_IMUL};
    u32 i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        Arena a;
        X64Func *f;
        const X64Block *b;

        arena_init(&a);
        f = two_addr_case(&a, ops[i], PRCX, PRAX, PRCX);
        x64_twoaddr_fixup(f);
        b = &f->blocks[0];
        T_ASSERT_EQ_INT(t, b->n, 2); /* swapped in place + ret */
        T_ASSERT_EQ_INT(t, b->insts[0].op, ops[i]);
        T_ASSERT_EQ_INT(t, b->insts[0].def.v, PRCX);
        T_ASSERT_EQ_INT(t, b->insts[0].a.r.v, PRCX);
        T_ASSERT_EQ_INT(t, b->insts[0].b.r.v, PRAX);
        arena_free_all(&a);
    }
}

void test_x64_twoaddr_dst_is_src2_noncommutative(TestCtx *t)
{
    static const u16 ops[] = {X64_OP_SUB, X64_OP_SHL, X64_OP_SHR, X64_OP_SAR};
    u32 i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        Arena a;
        X64Func *f;
        const X64Block *b;

        arena_init(&a);
        f = two_addr_case(&a, ops[i], PRCX, PRAX, PRCX);
        x64_twoaddr_fixup(f);
        b = &f->blocks[0];
        T_ASSERT_EQ_INT(t, b->n, 4);
        /* mov r10, rcx (rescue src2 before the mov clobbers it) */
        T_ASSERT_EQ_INT(t, b->insts[0].op, X64_OP_MOV);
        T_ASSERT_EQ_INT(t, b->insts[0].def.v, PR10);
        T_ASSERT_EQ_INT(t, b->insts[0].a.r.v, PRCX);
        /* mov rcx, rax */
        T_ASSERT_EQ_INT(t, b->insts[1].op, X64_OP_MOV);
        T_ASSERT_EQ_INT(t, b->insts[1].def.v, PRCX);
        T_ASSERT_EQ_INT(t, b->insts[1].a.r.v, PRAX);
        /* op rcx, r10 */
        T_ASSERT_EQ_INT(t, b->insts[2].op, ops[i]);
        T_ASSERT_EQ_INT(t, b->insts[2].def.v, PRCX);
        T_ASSERT_EQ_INT(t, b->insts[2].a.r.v, PRCX);
        T_ASSERT_EQ_INT(t, b->insts[2].b.r.v, PR10);
        arena_free_all(&a);
    }
}

void test_x64_twoaddr_plain(TestCtx *t)
{
    static const u16 ops[] = {X64_OP_SUB, X64_OP_ADD, X64_OP_SAR, X64_OP_AND};
    u32 i;

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        Arena a;
        X64Func *f;
        const X64Block *b;

        arena_init(&a);
        f = two_addr_case(&a, ops[i], PRDX, PRAX, PRCX);
        x64_twoaddr_fixup(f);
        b = &f->blocks[0];
        T_ASSERT_EQ_INT(t, b->n, 3);
        /* mov rdx, rax; op rdx, rcx */
        T_ASSERT_EQ_INT(t, b->insts[0].op, X64_OP_MOV);
        T_ASSERT_EQ_INT(t, b->insts[0].def.v, PRDX);
        T_ASSERT_EQ_INT(t, b->insts[0].a.r.v, PRAX);
        T_ASSERT_EQ_INT(t, b->insts[1].op, ops[i]);
        T_ASSERT_EQ_INT(t, b->insts[1].def.v, PRDX);
        T_ASSERT_EQ_INT(t, b->insts[1].a.r.v, PRDX);
        T_ASSERT_EQ_INT(t, b->insts[1].b.r.v, PRCX);
        arena_free_all(&a);
    }
}

void test_x64_twoaddr_unary(TestCtx *t)
{
    Arena a;
    X64Func *f;
    const X64Block *b;

    arena_init(&a);
    f = two_addr_case(&a, X64_OP_NEG, PRCX, PRAX, 0);
    x64_twoaddr_fixup(f);
    b = &f->blocks[0];
    T_ASSERT_EQ_INT(t, b->n, 3);
    T_ASSERT_EQ_INT(t, b->insts[0].op, X64_OP_MOV);
    T_ASSERT_EQ_INT(t, b->insts[0].def.v, PRCX);
    T_ASSERT_EQ_INT(t, b->insts[0].a.r.v, PRAX);
    T_ASSERT_EQ_INT(t, b->insts[1].op, X64_OP_NEG);
    T_ASSERT_EQ_INT(t, b->insts[1].def.v, PRCX);
    T_ASSERT_EQ_INT(t, b->insts[1].a.r.v, PRCX);
    arena_free_all(&a);
}

/* --- the MIR interpreter differential ---------------------------------------
 *
 * Straight-line functions only (no branches): interpret the vreg form,
 * allocate, interpret the physical form (registers + spill slots +
 * push/pop are just numbered cells and bytes), compare the value that
 * lands in rax. Every seed also verifies post-RA. A quarter of the
 * seeds run under CGF_SPILL_ALL=1 — the same code path, all spills. */

#include "x64sim.h"

/* splitmix64: the repo's fixed-seed PRNG shape (no libc rand). */
static u64 sm64(u64 *st)
{
    u64 z = (*st += 0x9e3779b97f4a7c15ull);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

/* Deterministic straight-line function from a seed. Every value stays
 * live into a final xor-fold, so vreg count == peak pressure; a few
 * variable-count shifts add CL pre-coloring into the mix. Returns the
 * result vreg (pre-colored rax by the final mov, mirroring isel's ret). */
static u32 gen_func(X64Func *f, u64 seed)
{
    u64 st = seed;
    u32 nconst = 4 + (u32)(sm64(&st) % 14);
    u32 nops = 8 + (u32)(sm64(&st) % 16);
    u32 cur = 0, i, acc, res;
    X64Inst *in;

    for (i = 0; i < nconst; i++) {
        in = put(f, 0, X64_OP_MOV, (sm64(&st) & 1) ? X64_Q : X64_L);
        in->def.v = ++cur;
        in->a = oi((i32)sm64(&st));
    }
    for (i = 0; i < nops; i++) {
        u32 pick = (u32)(sm64(&st) % 12);
        u32 s1 = 1 + (u32)(sm64(&st) % cur);
        u32 s2 = 1 + (u32)(sm64(&st) % cur);
        u8 w = (sm64(&st) & 1) ? X64_Q : X64_L;

        if (pick < 6) {
            static const u16 alu[6] = {X64_OP_ADD, X64_OP_SUB, X64_OP_AND,
                                       X64_OP_OR,  X64_OP_XOR, X64_OP_IMUL};

            in = put(f, 0, alu[pick], w);
            in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
            in->def.v = cur + 1;
            in->a = ov(s1);
            if (sm64(&st) & 1)
                in->b = oi((i16)sm64(&st));
            else
                in->b = ov(s2);
            cur++;
        } else if (pick < 9) {
            static const u16 sh[3] = {X64_OP_SHL, X64_OP_SHR, X64_OP_SAR};

            in = put(f, 0, sh[pick - 6], w);
            in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
            in->def.v = cur + 1;
            in->a = ov(s1);
            if (sm64(&st) % 3) {
                in->b = oi((i64)(sm64(&st) % 31));
            } else {
                /* variable count: the CL pre-color constraint */
                in->b = ov(s2);
                in->b.fixed = X64_RCX + 1;
            }
            cur++;
        } else if (pick < 10) {
            in = put(f, 0, (sm64(&st) & 1) ? X64_OP_NEG : X64_OP_NOT, w);
            in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
            in->def.v = cur + 1;
            in->a = ov(s1);
            cur++;
        } else {
            in = put(f, 0, X64_OP_MOV, w);
            in->def.v = cur + 1;
            in->a = ov(s1);
            cur++;
        }
    }
    /* xor-fold EVERYTHING (keeps every vreg live to its fold point). */
    acc = 1;
    for (i = 2; i <= cur; i++) {
        in = put(f, 0, X64_OP_XOR, X64_Q);
        in->flags = X64IF_TWO_ADDR | X64IF_DEFS_FLAGS;
        in->def.v = cur + i; /* fresh, disjoint from 1..cur */
        in->a = ov(acc);
        in->b = ov(i);
        acc = cur + i;
    }
    res = 2 * cur + 1;
    in = put(f, 0, X64_OP_MOV, X64_Q);
    in->def.v = res;
    in->def_fixed = X64_RAX + 1;
    in->a = ov(acc);
    put(f, 0, X64_OP_RET, X64_Q);
    f->nvregs = res;
    return res;
}

typedef struct VerifyCount {
    int errors;
} VerifyCount;

static void count_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)dc;
    if (d->level == DIAG_ERROR || d->level == DIAG_FATAL)
        ((VerifyCount *)user)->errors++;
}

void test_x64_regalloc_interp_differential(TestCtx *t)
{
    u32 iter;
    u8 *mem1 = cgf_xmalloc(SIM_MEM);
    u8 *mem2 = cgf_xmalloc(SIM_MEM);

    for (iter = 0; iter < 200; iter++) {
        Arena a1, a2;
        X64Func *f1, *f2;
        Sim s1, s2;
        u32 res1;
        bool ok1, ok2;
        bool spill_all = (iter % 4) == 3;

        arena_init(&a1);
        arena_init(&a2);
        f1 = mkf(&a1, 1);
        res1 = gen_func(f1, 0x5eedull + iter);
        f2 = mkf(&a2, 1);
        (void)gen_func(f2, 0x5eedull + iter); /* identical build */

        if (spill_all)
            setenv("CGF_SPILL_ALL", "1", 1);
        x64_regalloc(f2);
        if (spill_all)
            unsetenv("CGF_SPILL_ALL");
        T_ASSERT(t, f2->allocated);
        if (spill_all)
            T_ASSERT(t, f2->spill_slots > 0);

        /* Post-RA MIR must satisfy the extended verifier. */
        {
            VerifyCount vc = {0};
            DiagCtx *dc = diag_ctx_new(&a2);
            DiagSink sink;

            sink.handle = count_sink;
            sink.user = &vc;
            diag_set_sink(dc, sink);
            T_ASSERT_EQ_INT(t, x64_mir_verify(f2, dc), 0);
            T_ASSERT_EQ_INT(t, vc.errors, 0);
        }

        sim_init(&s1, mem1, f1);
        sim_init(&s2, mem2, f2);
        ok1 = sim_run(t, f1, &s1);
        ok2 = sim_run(t, f2, &s2);
        T_ASSERT(t, ok1);
        T_ASSERT(t, ok2);
        /* THE differential: rax post-RA == the result vreg pre-RA. */
        T_ASSERT(t, s2.val[X64_RAX + 1] == s1.val[res1]);

        arena_free_all(&a1);
        arena_free_all(&a2);
    }
    free(mem1);
    free(mem2);
}
