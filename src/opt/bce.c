#include "opt/opt.h"

#include <limits.h>
#include <string.h>

#include "util/arena.h"

const Pass OPT_PASS_BCE = {"bce", opt_bce, PASS_PINNED_EXACT};

typedef struct BceRange {
    u64 lo;
    u64 hi;
    bool signed_order;
} BceRange;

typedef struct BceFold {
    IrInst *inst;
    BlockId block;
    bool value;
} BceFold;

typedef struct BceBails {
    bool wrap;
    bool unproven;
} BceBails;

#define BAIL_ONCE(cfg, seen, reason)                                           \
    do {                                                                       \
        if (!*(seen)) {                                                        \
            OPT_BAIL((cfg), "bce", reason);                                    \
            *(seen) = true;                                                    \
        }                                                                      \
    } while (0)

static u32 int_bits(IrType type)
{
    switch (type) {
    case IRT_I8:
        return 8;
    case IRT_I16:
        return 16;
    case IRT_I32:
        return 32;
    case IRT_I64:
        return 64;
    default:
        return 0;
    }
}

static u64 width_mask(u32 bits)
{
    return bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
}

static bool operand_is_value(IrOperand op, ValueId value)
{
    return op.kind == IROP_VALUE && op.a == value.v;
}

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static IrInst *value_inst(IrFunc *f, ValueId value)
{
    IrValInfo *vi;
    IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    vi = &f->vals[value.v - 1];
    if (vi->def_kind != VDEF_INST || !vi->def_block.v ||
        vi->def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[vi->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

static IrIcmp swap_pred(IrIcmp pred)
{
    switch (pred) {
    case ICMP_EQ:
    case ICMP_NE:
        return pred;
    case ICMP_SLT:
        return ICMP_SGT;
    case ICMP_SLE:
        return ICMP_SGE;
    case ICMP_SGT:
        return ICMP_SLT;
    case ICMP_SGE:
        return ICMP_SLE;
    case ICMP_ULT:
        return ICMP_UGT;
    case ICMP_ULE:
        return ICMP_UGE;
    case ICMP_UGT:
        return ICMP_ULT;
    case ICMP_UGE:
        return ICMP_ULE;
    }
    CGF_ICE("bce: invalid icmp predicate %d", (int)pred);
}

static bool signed_pred(IrIcmp pred)
{
    return pred >= ICMP_SLT && pred <= ICMP_SGE;
}

/* Interpret a width-sized bit pattern as a signed displacement without an
 * implementation-defined unsigned-to-signed cast.  The magnitude form also
 * represents INT64_MIN, whose positive counterpart is not an i64. */
static void signed_displacement(u64 bits_value, u32 bits, bool *negative,
                                u64 *magnitude)
{
    u64 mask = width_mask(bits);
    u64 sign = UINT64_C(1) << (bits - 1);
    u64 v = bits_value & mask;

    *negative = (v & sign) != 0;
    *magnitude = *negative ? ((~v + 1) & mask) : v;
}

/* Build an exact interval for offsets [first_offset,last_offset] of the
 * recurrence.  Coordinates are raw unsigned values for unsigned predicates
 * and sign-bit-biased values for signed predicates, making both orders plain
 * u64 comparisons.  Any modular crossing is rejected rather than split into
 * a disjoint interval: BCE's lattice has no speculative state. */
static bool recurrence_range(const LoopInduction *iv, u64 first_offset,
                             u64 last_offset, bool want_signed, BceRange *out)
{
    u32 bits = int_bits(iv->type);
    u64 mask, sign, start, step_bits, coord, room, amount;
    bool negative;

    if (!bits || iv->start.kind != IROP_ICONST ||
        iv->step.kind != IROP_ICONST || first_offset > last_offset)
        return false;
    mask = width_mask(bits);
    sign = UINT64_C(1) << (bits - 1);
    start = iv->start.a & mask;
    step_bits = iv->step.a & mask;
    if (iv->subtract_step)
        step_bits = (~step_bits + 1) & mask;
    signed_displacement(step_bits, bits, &negative, &amount);
    if (amount == 0)
        return false;

    coord = want_signed ? (start ^ sign) : start;
    if (negative) {
        if (first_offset > coord / amount || last_offset > coord / amount)
            return false;
        out->hi = coord - first_offset * amount;
        out->lo = coord - last_offset * amount;
    } else {
        room = mask - coord;
        if (first_offset > room / amount || last_offset > room / amount)
            return false;
        out->lo = coord + first_offset * amount;
        out->hi = coord + last_offset * amount;
    }
    out->signed_order = want_signed;
    return true;
}

static bool prove_range(IrIcmp pred, const BceRange *range, u64 constant,
                        u32 bits, bool *value)
{
    u64 c = constant & width_mask(bits);

    if (range->signed_order)
        c ^= UINT64_C(1) << (bits - 1);
    switch (pred) {
    case ICMP_EQ:
        if (range->lo == range->hi && range->lo == c)
            *value = true;
        else if (c < range->lo || c > range->hi)
            *value = false;
        else
            return false;
        return true;
    case ICMP_NE:
        if (range->lo == range->hi && range->lo == c)
            *value = false;
        else if (c < range->lo || c > range->hi)
            *value = true;
        else
            return false;
        return true;
    case ICMP_SLT:
    case ICMP_ULT:
        if (range->hi < c)
            *value = true;
        else if (range->lo >= c)
            *value = false;
        else
            return false;
        return true;
    case ICMP_SLE:
    case ICMP_ULE:
        if (range->hi <= c)
            *value = true;
        else if (range->lo > c)
            *value = false;
        else
            return false;
        return true;
    case ICMP_SGT:
    case ICMP_UGT:
        if (range->lo > c)
            *value = true;
        else if (range->hi <= c)
            *value = false;
        else
            return false;
        return true;
    case ICMP_SGE:
    case ICMP_UGE:
        if (range->lo >= c)
            *value = true;
        else if (range->hi < c)
            *value = false;
        else
            return false;
        return true;
    }
    return false;
}

static IrIcmp invert_pred(IrIcmp pred)
{
    switch (pred) {
    case ICMP_EQ:
        return ICMP_NE;
    case ICMP_NE:
        return ICMP_EQ;
    case ICMP_SLT:
        return ICMP_SGE;
    case ICMP_SLE:
        return ICMP_SGT;
    case ICMP_SGT:
        return ICMP_SLE;
    case ICMP_SGE:
        return ICMP_SLT;
    case ICMP_ULT:
        return ICMP_UGE;
    case ICMP_ULE:
        return ICMP_UGT;
    case ICMP_UGT:
        return ICMP_ULE;
    case ICMP_UGE:
        return ICMP_ULT;
    }
    CGF_ICE("bce: invalid icmp predicate %d", (int)pred);
}

static bool ordered_domains_match(IrIcmp a, IrIcmp b)
{
    bool a_ordered = a != ICMP_EQ && a != ICMP_NE;
    bool b_ordered = b != ICMP_EQ && b != ICMP_NE;

    return !a_ordered || !b_ordered || signed_pred(a) == signed_pred(b);
}

static bool fact_proves(IrIcmp fact, IrIcmp wanted, bool *value)
{
    if (!ordered_domains_match(fact, wanted))
        return false;
    if (wanted == fact) {
        *value = true;
        return true;
    }
    if (wanted == invert_pred(fact)) {
        *value = false;
        return true;
    }
    switch (fact) {
    case ICMP_EQ:
        if (wanted == ICMP_SLE || wanted == ICMP_SGE || wanted == ICMP_ULE ||
            wanted == ICMP_UGE)
            *value = true;
        else if (wanted == ICMP_SLT || wanted == ICMP_SGT ||
                 wanted == ICMP_ULT || wanted == ICMP_UGT)
            *value = false;
        else
            return false;
        return true;
    case ICMP_NE:
        return false;
    case ICMP_SLT:
        if (wanted == ICMP_NE || wanted == ICMP_SLE)
            *value = true;
        else if (wanted == ICMP_EQ || wanted == ICMP_SGT)
            *value = false;
        else
            return false;
        return true;
    case ICMP_ULT:
        if (wanted == ICMP_NE || wanted == ICMP_ULE)
            *value = true;
        else if (wanted == ICMP_EQ || wanted == ICMP_UGT)
            *value = false;
        else
            return false;
        return true;
    case ICMP_SGT:
        if (wanted == ICMP_NE || wanted == ICMP_SGE)
            *value = true;
        else if (wanted == ICMP_EQ || wanted == ICMP_SLT)
            *value = false;
        else
            return false;
        return true;
    case ICMP_UGT:
        if (wanted == ICMP_NE || wanted == ICMP_UGE)
            *value = true;
        else if (wanted == ICMP_EQ || wanted == ICMP_ULT)
            *value = false;
        else
            return false;
        return true;
    case ICMP_SLE:
    case ICMP_SGE:
    case ICMP_ULE:
    case ICMP_UGE:
        return false;
    }
    return false;
}

static bool comparison_fact(const IrInst *condition, const IrInst *candidate,
                            bool true_edge, bool *value)
{
    IrIcmp fact;

    if (condition->op != IR_ICMP || candidate->op != IR_ICMP ||
        condition->nops != 2 || candidate->nops != 2)
        return false;
    fact = (IrIcmp)condition->subop;
    if (operand_equal(condition->ops[0], candidate->ops[0]) &&
        operand_equal(condition->ops[1], candidate->ops[1])) {
        /* already normalized */
    } else if (operand_equal(condition->ops[0], candidate->ops[1]) &&
               operand_equal(condition->ops[1], candidate->ops[0])) {
        fact = swap_pred(fact);
    } else {
        return false;
    }
    if (!true_edge)
        fact = invert_pred(fact);
    return fact_proves(fact, (IrIcmp)candidate->subop, value);
}

static bool edge_target_has_unique_predecessor(const IrFunc *f, BlockId target,
                                               BlockId predecessor)
{
    u32 bi, count = 0;

    /* Block 1 also has the function invocation as an implicit predecessor.
     * A CFG self-edge is therefore never its unique predecessor, even when
     * it is the only explicit edge targeting entry.  Treating that backedge
     * as an entry fact folds comparisons on the first execution using a
     * condition that has not executed yet. */
    if (target.v == 1)
        return false;
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 ei;

            for (ei = 0; ei < in->nedges; ei++)
                if (in->edges[ei].target.v == target.v) {
                    if (bi + 1 != predecessor.v)
                        return false;
                    count++;
                }
        }
    }
    return count == 1;
}

/* A condbr edge is a fact only in blocks dominated by that particular edge's
 * target.  Checking dominance in the opposite direction would let a later
 * (post-dominating) test justify an earlier access and is a classic BCE
 * miscompile. */
static bool prove_from_dominating_edge(IrFunc *f, const IrDomTree *dom,
                                       BlockId at, const IrInst *candidate,
                                       bool *value)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *term = f->blocks[bi].last;
        IrInst *condition;
        u32 edge;

        if (!term || term->op != IR_CONDBR || term->nops != 1 ||
            term->nedges != 2 || term->ops[0].kind != IROP_VALUE)
            continue;
        condition = value_inst(f, (ValueId){(u32)term->ops[0].a});
        if (!condition || condition == candidate)
            continue;
        for (edge = 0; edge < 2; edge++)
            if (edge_target_has_unique_predecessor(f, term->edges[edge].target,
                                                   (BlockId){bi + 1}) &&
                ir_dominates(dom, term->edges[edge].target, at) &&
                comparison_fact(condition, candidate, edge == 0, value)) {
                return true;
            }
    }
    return false;
}

static bool prove_from_trip(const TripInfo *trip, const OptConfig *cfg,
                            const IrInst *in, bool *value, bool *wrapped)
{
    const LoopInduction *iv = &trip->induction;
    IrOperand variable, constant;
    IrIcmp pred = (IrIcmp)in->subop;
    BceRange range;
    u64 first = 0, last;
    bool is_signed = signed_pred(pred);
    u32 bits;

    *wrapped = false;
    if (trip->kind != LOOP_TRIP_CONSTANT || trip->constant == 0 ||
        in->op != IR_ICMP || in->nops != 2)
        return false;
    if (operand_is_value(in->ops[0], iv->iv) ||
        operand_is_value(in->ops[0], iv->update)) {
        variable = in->ops[0];
        constant = in->ops[1];
    } else if (operand_is_value(in->ops[1], iv->iv) ||
               operand_is_value(in->ops[1], iv->update)) {
        variable = in->ops[1];
        constant = in->ops[0];
        pred = swap_pred(pred);
    } else {
        return false;
    }
    if (constant.kind != IROP_ICONST || variable.type != iv->type ||
        constant.type != iv->type)
        return false;
    bits = int_bits(iv->type);
    if (!bits)
        return false;
    /* -fwrapv removes the signed no-wrap license even when the source
     * arithmetic happened to carry an old NSW bit. */
    if (is_signed && (cfg->fwrapv || !iv->signed_no_wrap)) {
        *wrapped = true;
        return false;
    }
    if (operand_is_value(variable, iv->update))
        first = 1;
    last = trip->constant - 1 + first;
    if (!recurrence_range(iv, first, last, is_signed, &range)) {
        *wrapped = true;
        return false;
    }
    return prove_range(pred, &range, constant.a, bits, value);
}

static void replace_value(IrFunc *f, ValueId old, bool value)
{
    IrOperand replacement = ir_op_iconst(IRT_I32, value ? 1 : 0);
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 oi, ei, ai;

            for (oi = 0; oi < in->nops; oi++)
                if (operand_is_value(in->ops[oi], old))
                    in->ops[oi] = replacement;
            for (ei = 0; ei < in->nedges; ei++)
                for (ai = 0; ai < in->edges[ei].nargs; ai++)
                    if (operand_is_value(in->edges[ei].args[ai], old))
                        in->edges[ei].args[ai] = replacement;
        }
    }
}

static void remove_inst(IrFunc *f, const BceFold *fold)
{
    IrBlock *block = &f->blocks[fold->block.v - 1];
    IrInst *prev = NULL;
    IrInst *in;

    for (in = block->first; in && in != fold->inst; in = in->next)
        prev = in;
    if (!in)
        CGF_ICE("bce: planned comparison disappeared");
    if (prev)
        prev->next = in->next;
    else
        block->first = in->next;
    if (block->last == in)
        block->last = prev;
    block->ninsts--;
}

static bool run_function(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;
    BceFold *folds;
    bool *planned;
    BceBails bails = {0};
    u32 max_folds = 0, nfolds = 0, li;

    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    for (li = 0; li < f->nblocks; li++)
        max_folds += f->blocks[li].ninsts;
    folds = arena_alloc(&scratch, (max_folds ? max_folds : 1) * sizeof(*folds),
                        _Alignof(BceFold));
    planned = arena_alloc(&scratch, ((size_t)f->nvals + 1) * sizeof(*planned),
                          _Alignof(bool));
    memset(planned, 0, ((size_t)f->nvals + 1) * sizeof(*planned));

    for (li = 0; li < loop_tree_count(tree); li++) {
        const Loop *loop = loop_tree_at(tree, li);
        TripInfo trip;
        const char *trip_reason = NULL;
        bool have_trip =
            loop_trip_analyze(f, loop, cfg->fwrapv, &trip, &trip_reason);
        bool trip_wrapped =
            !have_trip && trip_reason && strcmp(trip_reason, "trip_wrap") == 0;
        u32 bi;

        (void)trip_reason;
        for (bi = 0; bi < loop_block_count(loop); bi++) {
            BlockId block = loop_block(loop, bi);
            IrInst *in;

            for (in = f->blocks[block.v - 1].first; in; in = in->next) {
                bool value, wrapped = false, proved;

                if (in->op != IR_ICMP || !in->result.v || planned[in->result.v])
                    continue;
                /* The loop's controlling comparison must retain the exit;
                 * its truth is path-sensitive, not loop-global. */
                if (have_trip && in->result.v == trip.induction.compare.v)
                    continue;
                proved = prove_from_dominating_edge(f, dom, block, in, &value);
                if (!proved && have_trip)
                    proved = prove_from_trip(&trip, cfg, in, &value, &wrapped);
                if (!proved) {
                    if (wrapped || trip_wrapped)
                        BAIL_ONCE(cfg, &bails.wrap, "bce_wrap");
                    else
                        BAIL_ONCE(cfg, &bails.unproven, "bce_unproven");
                    continue;
                }
                planned[in->result.v] = true;
                folds[nfolds++] = (BceFold){in, block, value};
            }
        }
    }
    for (li = 0; li < nfolds; li++) {
        replace_value(f, folds[li].inst->result, folds[li].value);
        remove_inst(f, &folds[li]);
    }
    if (nfolds)
        ir_func_renumber(m->arena, f);
    arena_free_all(&scratch);
    return nfolds != 0;
}

bool opt_bce(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 fi;

    for (fi = 0; fi < m->nfuncs; fi++) {
        OptConfig local = *cfg;

        local.current_func = m->funcs[fi].name;
        changed |= run_function(m, &m->funcs[fi], &local);
    }
    return changed;
}
