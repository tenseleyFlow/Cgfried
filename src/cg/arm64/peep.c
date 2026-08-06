#include "cg/arm64/mir.h"
#include "util/base.h"

#include <stdlib.h>
#include <string.h>

/* Post-selection A64 cleanup and relaxation.  The pass deliberately works on
 * one basic block at a time for pairing: a block boundary is an observable
 * branch target, never an optimization window.  Branch relaxation is
 * monotone (narrow forms only expand), so restarting after each expansion
 * reaches a fixpoint without the oscillation hazards of a shrink/expand pass.
 */

static bool reg_eq(A64Reg a, A64Reg b)
{
    return a.id == b.id && a.physical == b.physical;
}

static bool reg_valid(A64Reg r)
{
    return r.id != 0;
}

static bool mem_parts(const A64Inst *in, A64Reg *rt, const A64Mem **mem)
{
    u32 i;
    bool saw_reg = false;
    bool saw_mem = false;

    if (in->nops != 2)
        return false;
    for (i = 0; i < in->nops; i++) {
        if (in->ops[i].kind == A64O_REG && !saw_reg) {
            *rt = in->ops[i].reg;
            saw_reg = true;
        } else if (in->ops[i].kind == A64O_MEM && !saw_mem) {
            *mem = &in->ops[i].mem;
            saw_mem = true;
        } else {
            return false;
        }
    }
    return saw_reg && saw_mem;
}

static bool pairable_mem(const A64Inst *a, const A64Inst *b, A64Reg *ra,
                         A64Reg *rb, const A64Mem **ma)
{
    const A64Mem *mb;
    u8 size;

    if (a->op != b->op || (a->op != A64_OP_LOAD && a->op != A64_OP_STORE) ||
        a->sf != b->sf ||
        ((a->flags | b->flags) & (A64IF_VOLATILE | A64IF_ATOMIC)))
        return false;
    if (!mem_parts(a, ra, ma) || !mem_parts(b, rb, &mb))
        return false;
    size = (*ma)->size;
    if ((size != 4 && size != 8) || mb->size != size ||
        (a->sf == A64_SF32 ? size != 4 : size != 8))
        return false;

    /* Pair addressing has one base plus a signed scaled immediate.  Pre/post
     * indexing changes the base between the original accesses; register
     * offsets and materialized addresses do not have an equivalent ldp/stp
     * form. */
    if (((*ma)->mode != A64_ADDR_SCALED && (*ma)->mode != A64_ADDR_UNSCALED) ||
        (mb->mode != A64_ADDR_SCALED && mb->mode != A64_ADDR_UNSCALED) ||
        reg_valid((*ma)->index) || reg_valid(mb->index) ||
        !reg_eq((*ma)->base, mb->base))
        return false;

    if ((*ma)->offset > INT64_MAX - size ||
        mb->offset != (*ma)->offset + size || (*ma)->offset % size != 0 ||
        (*ma)->offset / size < -64 || (*ma)->offset / size > 63)
        return false;

    /* Loading through a register that the first load overwrites changes the
     * second scalar address.  Reject either destination/base overlap as the
     * conservative rule, and require distinct transfer registers for both
     * loads and stores (the pass promises independent registers). */
    if (!reg_valid(*ra) || !reg_valid(*rb) || reg_eq(*ra, *rb))
        return false;
    if (a->op == A64_OP_LOAD &&
        (reg_eq(*ra, (*ma)->base) || reg_eq(*rb, (*ma)->base)))
        return false;
    return true;
}

static void adjust_flags_after_remove(A64Block *b, u32 removed)
{
    u32 i;

    for (i = removed; i < b->n; i++) {
        A64Inst *in = &b->insts[i];

        if (!(in->flags & A64IF_USES_NZCV))
            continue;
        if (in->flags_src == removed)
            CGF_ICE("a64 peephole removed a live NZCV producer");
        if (in->flags_src > removed)
            in->flags_src--;
    }
}

bool a64_peep_pair_mem(A64Func *f)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        u32 i = 0;

        while (i + 1 < b->n) {
            A64Inst *a = &b->insts[i];
            A64Inst *next = &b->insts[i + 1];
            const A64Mem *mem;
            A64Reg ra, rb;
            A64Inst pair;

            if (!pairable_mem(a, next, &ra, &rb, &mem)) {
                i++;
                continue;
            }
            memset(&pair, 0, sizeof(pair));
            pair.op = a->op == A64_OP_LOAD ? A64_OP_LDP : A64_OP_STP;
            pair.sf = a->sf;
            pair.nops = 3;
            pair.ops[0].kind = A64O_REG;
            pair.ops[0].reg = ra;
            pair.ops[1].kind = A64O_REG;
            pair.ops[1].reg = rb;
            pair.ops[2].kind = A64O_MEM;
            pair.ops[2].mem = *mem;
            pair.ops[2].mem.mode = A64_ADDR_SCALED;
            pair.loc = a->loc;
            *a = pair;
            memmove(&b->insts[i + 1], &b->insts[i + 2],
                    (b->n - i - 2) * sizeof(*b->insts));
            b->n--;
            adjust_flags_after_remove(b, i + 1);
            changed = true;
            i++;
        }
    }
    return changed;
}

static u64 add_sat(u64 a, u64 b)
{
    return UINT64_MAX - a < b ? UINT64_MAX : a + b;
}

static bool has_explicit_false_edge(const A64Inst *in)
{
    u32 i, edges = 0;

    for (i = 0; i < in->nops; i++)
        if (in->ops[i].kind == A64O_LABEL || in->ops[i].kind == A64O_SYM)
            edges++;
    return edges >= 2;
}

/* Pre-RA pseudos may grow later.  Overestimates are intentional: needless
 * widening costs one instruction, while an underestimate can hand the
 * assembler an unencodable branch. */
static u64 worst_inst_bytes(const A64Inst *in)
{
    switch (in->op) {
    case A64_OP_BCOND:
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
        /* Production conditional MIR carries both successors.  Unless isel
         * has canonicalized the false edge to fallthrough, emission is the
         * conditional transfer plus an explicit B to the false successor. */
        return has_explicit_false_edge(in) ? 8 : 4;
    case A64_OP_ATOMIC_LLSC:
        return 64;
    case A64_OP_VASTART:
        return 32;
    case A64_OP_ALLOCA_DYN:
        /* round-up (2 ADDs worst case), AND, SUB sp, and the outgoing-area
         * ADD (2 worst case) -- see frame_expand_dynamic. */
        return 32;
    case A64_OP_STACKRESTORE:
        return 24;
    case A64_OP_CALL:
        return 16;
    default:
        return 4;
    }
}

static u64 *block_offsets(const A64Func *f)
{
    u64 *off = cgf_xmalloc(((size_t)f->nblocks + 1) * sizeof(*off));
    u64 at = 0;
    u32 bi, i;

    for (bi = 0; bi < f->nblocks; bi++) {
        off[bi] = at;
        for (i = 0; i < f->blocks[bi].n; i++)
            at = add_sat(at, worst_inst_bytes(&f->blocks[bi].insts[i]));
    }
    off[f->nblocks] = at;
    return off;
}

static i64 branch_delta(u64 from, u64 to)
{
    if (to >= from) {
        u64 d = to - from;
        return d > (u64)INT64_MAX ? INT64_MAX : (i64)d;
    }
    if (from - to > (u64)INT64_MAX)
        return INT64_MIN;
    return -(i64)(from - to);
}

static bool branch_target(const A64Inst *in, u32 nth, A64Operand *out)
{
    u32 i, seen = 0;

    for (i = 0; i < in->nops; i++) {
        if (in->ops[i].kind != A64O_LABEL && in->ops[i].kind != A64O_SYM)
            continue;
        if (seen++ == nth) {
            *out = in->ops[i];
            return true;
        }
    }
    return false;
}

static bool branch_reg(const A64Inst *in, A64Reg *out)
{
    u32 i;

    for (i = 0; i < in->nops; i++) {
        if (in->ops[i].kind == A64O_REG) {
            *out = in->ops[i].reg;
            return true;
        }
    }
    return false;
}

static bool branch_bit(const A64Inst *in, u32 *out)
{
    u32 i;

    for (i = 0; i < in->nops; i++) {
        if (in->ops[i].kind == A64O_IMM && in->ops[i].imm >= 0 &&
            in->ops[i].imm < 64) {
            *out = (u32)in->ops[i].imm;
            return true;
        }
    }
    return false;
}

static bool target_delta(const A64Func *f, const u64 *off, u64 pc,
                         A64Operand target, i64 *delta)
{
    if (target.kind != A64O_LABEL || target.id == 0 || target.id > f->nblocks)
        return false;
    *delta = branch_delta(pc, off[target.id - 1]);
    return true;
}

static bool fits_branch(i64 delta, i64 lo, i64 hi)
{
    return (delta & 3) == 0 && delta >= lo && delta <= hi;
}

/* Shared with the eventual emitter: the architectural displacement bounds
 * are properties of the opcode, not of a particular layout walk. */
bool a64_branch_delta_fits(u16 op, i64 delta)
{
    switch (op) {
    case A64_OP_B:
        return fits_branch(delta, -134217728, 134217724);
    case A64_OP_BCOND:
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
        return fits_branch(delta, -1048576, 1048572);
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
        return fits_branch(delta, -32768, 32764);
    default:
        return false;
    }
}

static A64Operand reg_operand(A64Reg reg)
{
    A64Operand out;

    memset(&out, 0, sizeof(out));
    out.kind = A64O_REG;
    out.reg = reg;
    return out;
}

static A64Operand imm_operand(i64 imm)
{
    A64Operand out;

    memset(&out, 0, sizeof(out));
    out.kind = A64O_IMM;
    out.imm = imm;
    return out;
}

static void block_insert(A64Func *f, A64Block *b, u32 at, A64Inst in)
{
    u32 old_n = b->n;
    u32 i;
    A64Inst zero;

    memset(&zero, 0, sizeof(zero));
    a64_block_append(f, b, zero);
    memmove(&b->insts[at + 1], &b->insts[at], (old_n - at) * sizeof(*b->insts));
    b->insts[at] = in;
    for (i = at + 1; i < b->n; i++) {
        if ((b->insts[i].flags & A64IF_USES_NZCV) &&
            b->insts[i].flags_src >= at)
            b->insts[i].flags_src++;
    }
}

static void expand_cond(A64Func *f, u32 bi, u32 ii)
{
    A64Block *b = &f->blocks[bi];
    A64Inst old = b->insts[ii];
    A64Inst jump, fall_jump;
    A64Operand taken, fall;
    bool explicit_fall;

    if (!branch_target(&old, 0, &taken))
        CGF_ICE("a64 relax: conditional branch has no target");
    if (old.cond > A64_CC_LE)
        CGF_ICE("a64 relax: non-invertible condition");
    explicit_fall = branch_target(&old, 1, &fall);

    /* A local +8 displacement is invariant under every later layout change.
     * It skips the first unconditional branch.  With an explicit false edge,
     * that lands on a second B; with implicit fallthrough it lands directly
     * on the original next instruction. */
    old.cond ^= 1u;
    old.nops = 1;
    old.ops[0] = imm_operand(8);
    memset(&old.ops[1], 0, 3 * sizeof(old.ops[0]));
    b->insts[ii] = old;

    memset(&jump, 0, sizeof(jump));
    jump.op = A64_OP_B;
    jump.nops = 1;
    jump.ops[0] = taken;
    jump.loc = old.loc;
    block_insert(f, b, ii + 1, jump);
    if (explicit_fall) {
        memset(&fall_jump, 0, sizeof(fall_jump));
        fall_jump.op = A64_OP_B;
        fall_jump.nops = 1;
        fall_jump.ops[0] = fall;
        fall_jump.loc = old.loc;
        block_insert(f, b, ii + 2, fall_jump);
    }
}

static void expand_zero_branch(A64Func *f, u32 bi, u32 ii)
{
    A64Block *b = &f->blocks[bi];
    A64Inst old = b->insts[ii];
    A64Inst cmp, br;
    A64Reg tested;
    u32 nlabels = 0;

    if (!branch_reg(&old, &tested))
        CGF_ICE("a64 relax: cbz/cbnz has no tested register");
    memset(&cmp, 0, sizeof(cmp));
    cmp.op = A64_OP_SUBS;
    cmp.sf = old.sf;
    cmp.flags = A64IF_DEFS_NZCV;
    cmp.nops = 3;
    cmp.ops[0] = reg_operand(a64_phys(A64_XZR));
    cmp.ops[1] = reg_operand(tested);
    cmp.ops[2] = imm_operand(0);
    cmp.loc = old.loc;

    memset(&br, 0, sizeof(br));
    br.op = A64_OP_BCOND;
    br.cond = old.op == A64_OP_CBZ ? A64_CC_EQ : A64_CC_NE;
    br.flags = A64IF_USES_NZCV;
    br.flags_src = ii;
    while (nlabels < 2 && branch_target(&old, nlabels, &br.ops[nlabels]))
        nlabels++;
    if (!nlabels)
        CGF_ICE("a64 relax: cbz/cbnz has no target");
    br.nops = (u8)nlabels;
    br.loc = old.loc;
    b->insts[ii] = cmp;
    block_insert(f, b, ii + 1, br);
}

static void expand_test_branch(A64Func *f, u32 bi, u32 ii)
{
    A64Block *b = &f->blocks[bi];
    A64Inst old = b->insts[ii];
    A64Inst tst, br;
    A64Reg tested;
    u32 bit, nlabels = 0;

    if (!branch_reg(&old, &tested) || !branch_bit(&old, &bit))
        CGF_ICE("a64 relax: tbz/tbnz operands are malformed");
    if (old.sf == A64_SF32 && bit >= 32)
        CGF_ICE("a64 relax: 32-bit tbz/tbnz bit is out of range");
    memset(&tst, 0, sizeof(tst));
    tst.op = A64_OP_ANDS;
    tst.sf = old.sf;
    tst.flags = A64IF_DEFS_NZCV;
    tst.nops = 3;
    tst.ops[0] = reg_operand(a64_phys(A64_XZR));
    tst.ops[1] = reg_operand(tested);
    tst.ops[2] = imm_operand(bit == 63 ? INT64_MIN : (i64)(UINT64_C(1) << bit));
    tst.loc = old.loc;

    memset(&br, 0, sizeof(br));
    br.op = A64_OP_BCOND;
    br.cond = old.op == A64_OP_TBZ ? A64_CC_EQ : A64_CC_NE;
    br.flags = A64IF_USES_NZCV;
    br.flags_src = ii;
    while (nlabels < 2 && branch_target(&old, nlabels, &br.ops[nlabels]))
        nlabels++;
    if (!nlabels)
        CGF_ICE("a64 relax: tbz/tbnz has no target");
    br.nops = (u8)nlabels;
    br.loc = old.loc;
    b->insts[ii] = tst;
    block_insert(f, b, ii + 1, br);
}

bool a64_relax_branches(A64Func *f)
{
    bool any = false;

    for (;;) {
        u64 *off = block_offsets(f);
        bool expanded = false;
        u32 bi;

        for (bi = 0; bi < f->nblocks && !expanded; bi++) {
            A64Block *b = &f->blocks[bi];
            u64 pc = off[bi];
            u32 ii;

            for (ii = 0; ii < b->n; ii++) {
                A64Inst *in = &b->insts[ii];
                A64Operand target;
                A64Operand false_target;
                i64 delta = 0;
                i64 false_delta;
                bool local;

                if (in->op == A64_OP_BCOND && in->nops == 1 &&
                    in->ops[0].kind == A64O_IMM) {
                    if (in->ops[0].imm != 8)
                        CGF_ICE("a64 relax: invalid local branch displacement");
                    pc = add_sat(pc, worst_inst_bytes(in));
                    continue;
                }

                if (in->op == A64_OP_B) {
                    if (!branch_target(in, 0, &target))
                        CGF_ICE(
                            "a64 relax: unconditional branch has no target");
                    if (target.kind != A64O_LABEL)
                        CGF_ICE("a64 relax: external unconditional branch is "
                                "not supported");
                    if (!target_delta(f, off, pc, target, &delta))
                        CGF_ICE("a64 relax: unconditional branch target is "
                                "outside the function");
                    if (!a64_branch_delta_fits(A64_OP_B, delta))
                        CGF_ICE("a64 relax: unconditional branch displacement "
                                "exceeds imm26");
                    pc = add_sat(pc, worst_inst_bytes(in));
                    continue;
                }

                if ((in->op == A64_OP_BCOND || in->op == A64_OP_CBZ ||
                     in->op == A64_OP_CBNZ || in->op == A64_OP_TBZ ||
                     in->op == A64_OP_TBNZ) &&
                    !branch_target(in, 0, &target))
                    CGF_ICE("a64 relax: branch has no target operand");
                else if (in->op != A64_OP_BCOND && in->op != A64_OP_CBZ &&
                         in->op != A64_OP_CBNZ && in->op != A64_OP_TBZ &&
                         in->op != A64_OP_TBNZ) {
                    pc = add_sat(pc, worst_inst_bytes(in));
                    continue;
                }

                /* Two-successor MIR emits an ordinary B immediately after
                 * the conditional transfer.  Its imm26 contract is separate
                 * from the narrow taken-edge opcode and must be checked from
                 * that second instruction's PC. */
                if (branch_target(in, 1, &false_target)) {
                    if (!target_delta(f, off, add_sat(pc, 4), false_target,
                                      &false_delta))
                        CGF_ICE("a64 relax: false-edge branch target is "
                                "outside the function");
                    if (!a64_branch_delta_fits(A64_OP_B, false_delta))
                        CGF_ICE("a64 relax: false-edge branch displacement "
                                "exceeds imm26");
                }

                local = target_delta(f, off, pc, target, &delta);
                if ((in->op == A64_OP_TBZ || in->op == A64_OP_TBNZ) &&
                    (!local || !a64_branch_delta_fits(in->op, delta))) {
                    expand_test_branch(f, bi, ii);
                    expanded = true;
                } else if ((in->op == A64_OP_CBZ || in->op == A64_OP_CBNZ) &&
                           (!local || !a64_branch_delta_fits(in->op, delta))) {
                    expand_zero_branch(f, bi, ii);
                    expanded = true;
                } else if (in->op == A64_OP_BCOND &&
                           (!local || !a64_branch_delta_fits(in->op, delta))) {
                    expand_cond(f, bi, ii);
                    expanded = true;
                }
                if (expanded)
                    break;
                pc = add_sat(pc, worst_inst_bytes(in));
            }
        }
        free(off);
        if (!expanded)
            return any;
        any = true;
    }
}
