#include "cg/arm64/peep.h"
#include "util/base.h"

#include <stdlib.h>
#include <string.h>

/* Post-selection A64 cleanup.  The pass deliberately works on one basic
 * block at a time for pairing: a block boundary is an observable branch
 * target, never an optimization window. */

static bool reg_eq(A64Reg a, A64Reg b)
{
    return a.id == b.id && a.physical == b.physical;
}

static bool reg_valid(A64Reg r)
{
    return r.id != 0;
}

static bool reg_is_sp_or_zr(A64Reg r)
{
    return r.physical && (r.id == (u32)A64_SP + 1 || r.id == (u32)A64_XZR + 1);
}

static bool reg_is_fp(A64Reg r)
{
    u32 phys;

    if (!r.physical || !r.id)
        return false;
    phys = r.id - 1;
    return phys >= A64_V0 && phys <= A64_V31;
}

static void adjust_flags_after_remove(A64Block *b, u32 removed);
static bool layout_target_fits(const A64Func *f, u32 bi, u32 at, u16 op,
                               u32 target);

static bool reg_operand_parts(const A64Inst *in, u32 n, A64Reg *out)
{
    if (n >= in->nops || in->ops[n].kind != A64O_REG)
        return false;
    *out = in->ops[n].reg;
    return reg_valid(*out);
}

static bool inst_has_side_record_barrier(const A64Inst *in)
{
    /* Calls have architectural clobbers which are intentionally absent from
     * their inline operands.  Inline asm has the same side-record shape and
     * may name or clobber registers unavailable to this local pass. */
    return in->op == A64_OP_CALL || in->op == A64_OP_ASM;
}

static bool op0_is_def(u16 op)
{
    switch (op) {
    case A64_OP_STORE:
    case A64_OP_STP:
    case A64_OP_FCMP:
    case A64_OP_B:
    case A64_OP_BCOND:
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
    case A64_OP_CALL:
    case A64_OP_RET:
    case A64_OP_BR:
    case A64_OP_UNREACHABLE:
    case A64_OP_STACKRESTORE:
    case A64_OP_VASTART:
        return false;
    default:
        return true;
    }
}

static bool inst_defines_reg(const A64Inst *in, A64Reg reg)
{
    u32 i;

    if (inst_has_side_record_barrier(in))
        return true;
    if (in->op == A64_OP_LDP)
        for (i = 0; i < 2 && i < in->nops; i++)
            if (in->ops[i].kind == A64O_REG && reg_eq(in->ops[i].reg, reg))
                return true;
    if (op0_is_def(in->op) && in->nops && in->ops[0].kind == A64O_REG &&
        reg_eq(in->ops[0].reg, reg))
        return true;
    /* Pre/post-indexed memory operands update their base register. */
    for (i = 0; i < in->nops; i++)
        if (in->ops[i].kind == A64O_MEM &&
            (in->ops[i].mem.mode == A64_ADDR_PRE ||
             in->ops[i].mem.mode == A64_ADDR_POST) &&
            reg_eq(in->ops[i].mem.base, reg))
            return true;
    return false;
}

static void block_remove(A64Block *b, u32 at)
{
    memmove(&b->insts[at], &b->insts[at + 1],
            (b->n - at - 1) * sizeof(*b->insts));
    b->n--;
    adjust_flags_after_remove(b, at);
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
    if (ra->physical != rb->physical ||
        (ra->physical && reg_is_fp(*ra) != reg_is_fp(*rb)))
        return false;
    /* emit_pair derives both register spellings from the first register's
     * bank and supports D-register pairs.  Pairing two scalar S accesses
     * would silently turn 4-byte STRs into 8-byte STPs and overwrite the
     * adjacent object (caught by the HFA and packed-struct cross corpus). */
    if (ra->physical && reg_is_fp(*ra) && size != 8)
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

static bool is_self_mov(const A64Inst *in)
{
    A64Reg dst, src;

    /* A W-register self-copy is not generally a no-op: it clears the upper
     * half of the architectural X register and is our i32->i64 zero-extend
     * representation.  Only the full-width GP copy is provenance-free. */
    return in->op == A64_OP_MOV && in->sf == A64_SF64 && in->nops == 2 &&
           !(in->flags & (A64IF_DEFS_NZCV | A64IF_USES_NZCV | A64IF_VOLATILE |
                          A64IF_ATOMIC)) &&
           reg_operand_parts(in, 0, &dst) && reg_operand_parts(in, 1, &src) &&
           reg_eq(dst, src);
}

static bool is_redundant_w_self_mov(const A64Block *b, u32 at)
{
    const A64Inst *in = &b->insts[at];
    const A64Inst *prior;
    A64Reg dst, src, prior_dst;
    u32 i;

    if (at == 0 || in->op != A64_OP_MOV || in->sf != A64_SF32 ||
        in->nops != 2 || in->flags || !reg_operand_parts(in, 0, &dst) ||
        !reg_operand_parts(in, 1, &src) || !reg_eq(dst, src) ||
        reg_is_sp_or_zr(dst) || reg_is_fp(dst))
        return false;
    prior = &b->insts[at - 1];
    if (prior->sf != A64_SF32 || !op0_is_def(prior->op) ||
        inst_has_side_record_barrier(prior) ||
        !reg_operand_parts(prior, 0, &prior_dst) || !reg_eq(prior_dst, dst))
        return false;
    /* A writeback address defines its base at X width independently of the
     * transfer width.  Do not use the transfer destination as provenance
     * when the same architectural register is also that base. */
    for (i = 0; i < prior->nops; i++)
        if (prior->ops[i].kind == A64O_MEM &&
            (prior->ops[i].mem.mode == A64_ADDR_PRE ||
             prior->ops[i].mem.mode == A64_ADDR_POST) &&
            reg_eq(prior->ops[i].mem.base, dst))
            return false;
    return true;
}

static bool rewrite_add_zero(A64Block *b, u32 at)
{
    A64Inst *in = &b->insts[at];
    A64Reg dst, src;

    if (in->op != A64_OP_ADD || in->nops != 3 || in->flags ||
        !reg_operand_parts(in, 0, &dst) || !reg_operand_parts(in, 1, &src) ||
        in->ops[2].kind != A64O_IMM || in->ops[2].imm != 0)
        return false;
    if (reg_eq(dst, src)) {
        block_remove(b, at);
        return true;
    }
    /* MOV encodes architectural register 31 as XZR, while ADD's operand
     * positions can name SP.  The frame builder intentionally spells
     * `add x29, sp, #0`; converting that to `mov x29, sp` loses the identity
     * information and is rejected by the MIR verifier. */
    if (reg_is_sp_or_zr(dst) || reg_is_sp_or_zr(src))
        return false;
    in->op = A64_OP_MOV;
    in->nops = 2;
    memset(&in->ops[2], 0, 2 * sizeof(in->ops[0]));
    return true;
}

static bool addr_shape(const A64Inst *in, A64Reg *dst, i64 *offset)
{
    if (in->op != A64_OP_ADDR || (in->nops != 2 && in->nops != 3) ||
        in->flags || !reg_operand_parts(in, 0, dst) ||
        (in->ops[1].kind != A64O_SYM && in->ops[1].kind != A64O_CPOOL))
        return false;
    if (in->nops == 3 && in->ops[2].kind != A64O_IMM)
        return false;
    *offset = in->nops == 3 ? in->ops[2].imm : 0;
    return true;
}

static bool same_addr(const A64Inst *a, const A64Inst *b)
{
    A64Reg ignored;
    i64 ao, bo;

    return addr_shape(a, &ignored, &ao) && addr_shape(b, &ignored, &bo) &&
           a->ops[1].kind == b->ops[1].kind && a->ops[1].id == b->ops[1].id &&
           ao == bo;
}

static bool rewrite_addr_cse(A64Block *b, u32 at)
{
    A64Inst *in = &b->insts[at];
    A64Reg dst;
    i64 offset;
    u32 j;

    if (!addr_shape(in, &dst, &offset))
        return false;
    (void)offset;
    for (j = at; j-- > 0;) {
        A64Inst *prior = &b->insts[j];
        A64Reg available;
        i64 prior_offset;
        u32 k;
        bool clobbered = false;

        if (!same_addr(prior, in) ||
            !addr_shape(prior, &available, &prior_offset))
            continue;
        (void)prior_offset;
        for (k = j + 1; k < at; k++)
            if (inst_defines_reg(&b->insts[k], available)) {
                clobbered = true;
                break;
            }
        if (clobbered)
            continue;

        /* ADDR is the indivisible adrp+add pseudo in this MIR.  Reusing a
         * complete identical address is therefore the strongest sound CSE
         * expressible here; page-only reuse would require splitting ADDR and
         * carrying relocation-half semantics through allocation. */
        if (reg_eq(dst, available)) {
            block_remove(b, at);
        } else {
            in->op = A64_OP_MOV;
            in->nops = 2;
            in->ops[1].kind = A64O_REG;
            in->ops[1].reg = available;
            memset(&in->ops[2], 0, 2 * sizeof(in->ops[0]));
        }
        return true;
    }
    return false;
}

static bool rewrite_madd(A64Block *b, u32 at)
{
    A64Inst *mul;
    A64Inst *add;
    A64Reg product, dst, lhs, rhs, a, c, acc;

    if (at + 1 >= b->n)
        return false;
    mul = &b->insts[at];
    add = &b->insts[at + 1];
    if (mul->op != A64_OP_MUL || add->op != A64_OP_ADD || mul->nops != 3 ||
        add->nops != 3 || mul->sf != add->sf || mul->flags || add->flags ||
        !reg_operand_parts(mul, 0, &product) ||
        !reg_operand_parts(mul, 1, &lhs) || !reg_operand_parts(mul, 2, &rhs) ||
        !reg_operand_parts(add, 0, &dst) || !reg_operand_parts(add, 1, &a) ||
        !reg_operand_parts(add, 2, &c) || !reg_eq(dst, product))
        return false;
    if (reg_eq(a, product) == reg_eq(c, product))
        return false; /* product must occur exactly once */
    acc = reg_eq(a, product) ? c : a;
    add->op = A64_OP_MADD;
    add->nops = 4;
    add->ops[0].kind = A64O_REG;
    add->ops[0].reg = dst;
    add->ops[1].kind = A64O_REG;
    add->ops[1].reg = lhs;
    add->ops[2].kind = A64O_REG;
    add->ops[2].reg = rhs;
    add->ops[3].kind = A64O_REG;
    add->ops[3].reg = acc;
    block_remove(b, at);
    return true;
}

static bool rewrite_layout_branch(A64Func *f, u32 bi, u32 at)
{
    A64Block *b = &f->blocks[bi];
    A64Inst *in = &b->insts[at];
    u32 next = bi + 2; /* block ids are one-based */
    u32 taken_at, fall_at;

    if (bi + 1 >= f->nblocks || at + 1 != b->n)
        return false;
    if (in->op == A64_OP_B && in->nops == 1 && in->ops[0].kind == A64O_LABEL &&
        in->ops[0].id == next) {
        /* Emission already knows this is a fallthrough, but removing it here
         * keeps MIR instruction counts honest and exposes the same fact to
         * every later consumer.  A mid-block B is not eligible: removing it
         * would execute instructions the branch used to skip. */
        block_remove(b, at);
        return true;
    }

    switch (in->op) {
    case A64_OP_BCOND:
        taken_at = 0;
        fall_at = 1;
        break;
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
        taken_at = 1;
        fall_at = 2;
        break;
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
        taken_at = 2;
        fall_at = 3;
        break;
    default:
        return false;
    }
    if (in->nops != fall_at + 1 || in->ops[taken_at].kind != A64O_LABEL ||
        in->ops[fall_at].kind != A64O_LABEL || in->ops[taken_at].id != next ||
        in->ops[fall_at].id == next)
        return false;
    if (in->op == A64_OP_BCOND && in->cond > A64_CC_LE)
        return false;
    /* The original conditional edge is the adjacent block and therefore
     * always encodable; its false edge rides an imm26 B.  After inversion
     * that false edge becomes the narrow conditional target, so retain the
     * two-instruction form when it is outside this opcode's range. */
    if (!layout_target_fits(f, bi, at, in->op, in->ops[fall_at].id))
        return false;

    {
        A64Operand tmp = in->ops[taken_at];

        in->ops[taken_at] = in->ops[fall_at];
        in->ops[fall_at] = tmp;
    }
    if (in->op == A64_OP_BCOND) {
        in->cond ^= 1u;
    } else if (in->op == A64_OP_CBZ) {
        in->op = A64_OP_CBNZ;
    } else if (in->op == A64_OP_CBNZ) {
        in->op = A64_OP_CBZ;
    } else if (in->op == A64_OP_TBZ) {
        in->op = A64_OP_TBNZ;
    } else {
        in->op = A64_OP_TBZ;
    }
    return true;
}

static bool peep_local(A64Func *f)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        A64Block *b = &f->blocks[bi];
        u32 i = 0;

        while (i < b->n) {
            if (rewrite_layout_branch(f, bi, i)) {
                changed = true;
                continue;
            }
            if (is_self_mov(&b->insts[i]) || is_redundant_w_self_mov(b, i)) {
                block_remove(b, i);
                changed = true;
                continue;
            }
            if (rewrite_add_zero(b, i) || rewrite_addr_cse(b, i) ||
                rewrite_madd(b, i)) {
                changed = true;
                continue;
            }
            i++;
        }
    }
    return changed;
}

bool a64_peep_post_ra(A64Func *f)
{
    bool any = false;
    u32 iteration;

    for (iteration = 0; iteration < 10; iteration++) {
        bool changed = peep_local(f);

        changed |= a64_peep_pair_mem(f);
        any |= changed;
        if (!changed)
            return any;
    }
    CGF_ICE("a64 peephole: fixpoint did not converge after 10 iterations");
}

static bool has_explicit_false_edge(const A64Inst *in)
{
    u32 i, edges = 0;

    for (i = 0; i < in->nops; i++)
        if (in->ops[i].kind == A64O_LABEL || in->ops[i].kind == A64O_SYM)
            edges++;
    return edges >= 2;
}

/* Some finalized MIR operations expand in the emitter.  Overestimates are
 * intentional: declining a layout inversion costs nothing but an extra B,
 * while an underestimate can move the old far edge into a narrow conditional
 * branch and hand the assembler invalid output.  Inline asm is unbounded and
 * therefore makes the proof unavailable. */
static bool worst_inst_bytes(const A64Inst *in, u64 *bytes)
{
    switch (in->op) {
    case A64_OP_BCOND:
    case A64_OP_CBZ:
    case A64_OP_CBNZ:
    case A64_OP_TBZ:
    case A64_OP_TBNZ:
        /* Every non-adjacent conditional edge expands to an inverted local
         * branch plus B.  A two-edge terminator can need one more B when
         * neither successor is the next block; a mid-block probe needs two
         * instructions even though it names only its taken successor. */
        *bytes = has_explicit_false_edge(in) ? 12 : 8;
        return true;
    case A64_OP_ADDR:
        /* adrp + add/load, plus at most two instructions for a GOT addend. */
        *bytes = 16;
        return true;
    case A64_OP_TLSADDR:
        /* mrs + two relocation halves + an optional constant addend. */
        *bytes = 16;
        return true;
    case A64_OP_ATOMIC_LLSC:
        *bytes = 64;
        return true;
    case A64_OP_ATOMIC_CAS:
        *bytes = 28;
        return true;
    case A64_OP_VASTART:
        *bytes = 32;
        return true;
    case A64_OP_ALLOCA_DYN:
        /* round-up (2 ADDs worst case), AND, SUB sp, and the outgoing-area
         * ADD (2 worst case), plus the over-alignment AND -- see
         * frame_expand_dynamic. */
        *bytes = 40;
        return true;
    case A64_OP_STACKRESTORE:
        *bytes = 24;
        return true;
    case A64_OP_CALL:
        *bytes = 16;
        return true;
    case A64_OP_ASM:
        return false;
    default:
        *bytes = 4;
        return true;
    }
}

static bool add_inst_bytes(u64 *total, const A64Inst *in)
{
    u64 bytes;

    if (!worst_inst_bytes(in, &bytes) || *total > (u64)INT64_MAX - bytes)
        return false;
    *total += bytes;
    return true;
}

static bool layout_target_fits(const A64Func *f, u32 bi, u32 at, u16 op,
                               u32 target)
{
    u32 target_bi;
    u32 bj, i;
    u64 distance = 0;

    if (bi >= f->nblocks || at >= f->blocks[bi].n || target == 0 ||
        target > f->nblocks)
        return false;
    target_bi = target - 1;
    if (target_bi > bi) {
        for (i = at; i < f->blocks[bi].n; i++)
            if (!add_inst_bytes(&distance, &f->blocks[bi].insts[i]))
                return false;
        for (bj = bi + 1; bj < target_bi; bj++)
            for (i = 0; i < f->blocks[bj].n; i++)
                if (!add_inst_bytes(&distance, &f->blocks[bj].insts[i]))
                    return false;
        return a64_branch_delta_fits(op, (i64)distance);
    }

    for (bj = target_bi; bj < bi; bj++)
        for (i = 0; i < f->blocks[bj].n; i++)
            if (!add_inst_bytes(&distance, &f->blocks[bj].insts[i]))
                return false;
    for (i = 0; i < at; i++)
        if (!add_inst_bytes(&distance, &f->blocks[bi].insts[i]))
            return false;
    return a64_branch_delta_fits(op, -(i64)distance);
}

static bool fits_branch(i64 delta, i64 lo, i64 hi)
{
    return (delta & 3) == 0 && delta >= lo && delta <= hi;
}

/* Architectural displacement bounds are properties of the opcode, not of a
 * particular layout walk. */
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
