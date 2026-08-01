#include "opt/opt.h"

#include <limits.h>
#include <string.h>

/* Trip counts are the number of successful condition tests (and therefore
 * body executions), not the index of the first value outside the loop.  The
 * modular path is deliberately exact: when a proof is unavailable it returns
 * false and the caller logs unroll_trip_wrap instead of guessing. */

static u32 int_width(IrType type)
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

static u64 width_mask(u32 width)
{
    return width == 64 ? UINT64_MAX : ((1ull << width) - 1);
}

static i64 sign_value(u64 value, u32 width)
{
    u64 mask = width_mask(width);
    u64 sign = 1ull << (width - 1);

    value &= mask;
    if (!(value & sign))
        return (i64)value;
    return (i64)(value | ~mask);
}

static bool pred_holds(IrIcmp pred, u64 x, u64 end, u32 width)
{
    u64 mask = width_mask(width);

    x &= mask;
    end &= mask;
    switch (pred) {
    case ICMP_EQ:
        return x == end;
    case ICMP_NE:
        return x != end;
    case ICMP_ULT:
        return x < end;
    case ICMP_ULE:
        return x <= end;
    case ICMP_UGT:
        return x > end;
    case ICMP_UGE:
        return x >= end;
    case ICMP_SLT:
        return sign_value(x, width) < sign_value(end, width);
    case ICMP_SLE:
        return sign_value(x, width) <= sign_value(end, width);
    case ICMP_SGT:
        return sign_value(x, width) > sign_value(end, width);
    case ICMP_SGE:
        return sign_value(x, width) >= sign_value(end, width);
    }
    return false;
}

static u64 gcd_pow2(u64 value, u32 width)
{
    u64 bit;

    if (!value)
        return width == 64 ? 0 : 1ull << width;
    bit = value & (~value + 1);
    if (width < 64 && bit > (1ull << width))
        return 1ull << width;
    return bit;
}

/* Inverse of an odd number modulo 2^width.  Newton iteration doubles the
 * number of correct low bits on every step and unsigned overflow supplies
 * the modulo-2^64 arithmetic for the wide case. */
static u64 inverse_odd(u64 value, u32 width)
{
    u64 inv = value;
    u32 bits;

    for (bits = 1; bits < width; bits <<= 1)
        inv *= 2 - value * inv;
    return inv & width_mask(width);
}

static bool modular_ne_count(u32 width, u64 start, u64 step, u64 end, u64 *trip)
{
    u64 mask = width_mask(width);
    u64 diff = (end - start) & mask;
    u64 divisor;
    u64 reduced_step;
    u64 reduced_diff;
    u32 reduced_width;

    step &= mask;
    if (!diff) {
        *trip = 0;
        return true;
    }
    if (!step)
        return false;
    divisor = gcd_pow2(step, width);
    if (divisor && (diff & (divisor - 1)))
        return false;
    reduced_step = step / divisor;
    reduced_diff = diff / divisor;
    reduced_width = width;
    while (divisor > 1) {
        divisor >>= 1;
        reduced_width--;
    }
    *trip = (reduced_diff * inverse_odd(reduced_step, reduced_width)) &
            width_mask(reduced_width);
    return true;
}

static bool enumerate_modular(u32 width, IrIcmp pred, u64 start, u64 step,
                              u64 end, u64 *trip)
{
    u64 period = 1ull << width;
    u64 mask = period - 1;
    u64 x = start & mask;
    u64 n;

    step &= mask;
    for (n = 0; n < period; n++) {
        if (!pred_holds(pred, x, end, width)) {
            *trip = n;
            return true;
        }
        x = (x + step) & mask;
    }
    return false;
}

static bool unsigned_nowrap_count(u32 width, IrIcmp pred, u64 start, u64 step,
                                  u64 end, u64 mask, u64 *trip)
{
    u64 distance;
    u64 amount;

    start &= mask;
    step &= mask;
    end &= mask;
    if (!pred_holds(pred, start, end, width)) {
        *trip = 0;
        return true;
    }
    if (!step)
        return false;
    switch (pred) {
    case ICMP_ULT:
        distance = end - start;
        *trip = distance / step + (distance % step != 0);
        return *trip <= (mask - start) / step;
    case ICMP_ULE:
        if (end == mask)
            return false;
        distance = end + 1 - start;
        *trip = distance / step + (distance % step != 0);
        return *trip <= (mask - start) / step;
    case ICMP_UGT:
    case ICMP_UGE:
        amount = (~step + 1) & mask;
        if (!amount)
            return false;
        distance = pred == ICMP_UGT ? start - end : start - end + 1;
        *trip = distance / amount + (distance % amount != 0);
        return *trip <= start / amount;
    default:
        return false;
    }
}

bool opt_unroll_trip_count(IrType type, IrIcmp pred, u64 start, u64 step,
                           u64 end, bool modular, u64 *trip)
{
    u32 width = int_width(type);
    u64 mask;

    if (!width || !trip)
        return false;
    mask = width_mask(width);
    start &= mask;
    step &= mask;
    end &= mask;
    if (!pred_holds(pred, start, end, width)) {
        *trip = 0;
        return true;
    }
    if (pred == ICMP_NE)
        return modular_ne_count(width, start, step, end, trip);
    if (pred == ICMP_EQ) {
        if (!step)
            return false;
        *trip = 1;
        return true;
    }
    /* Complete state-space traversal is cheap for the sub-word types and
     * proves both terminating wrap walks and cycles. */
    if (modular && width <= 16)
        return enumerate_modular(width, pred, start, step, end, trip);
    /* For wider IVs, accept only a monotone proof that reaches the boundary
     * before wrapping.  Signed strict-overflow loops use signed predicates;
     * their common positive/negative steps are handled by the same distances
     * after ordering has been established by the predicate. */
    if (!modular && pred >= ICMP_SLT && pred <= ICMP_SGE) {
        i64 s = sign_value(start, width);
        i64 e = sign_value(end, width);
        i64 d = sign_value(step, width);
        u64 distance;
        u64 amount;

        if (d > 0 && (pred == ICMP_SLT || pred == ICMP_SLE)) {
            if ((pred == ICMP_SLT && s >= e) || (pred == ICMP_SLE && s > e)) {
                *trip = 0;
                return true;
            }
            distance = (u64)e - (u64)s;
            if (pred == ICMP_SLE) {
                if (distance == UINT64_MAX)
                    return false;
                distance++;
            }
            *trip = distance / (u64)d + (distance % (u64)d != 0);
            return true;
        }
        if (d < 0 && (pred == ICMP_SGT || pred == ICMP_SGE)) {
            amount = ~((u64)d) + 1;
            distance = (u64)s - (u64)e;
            if (pred == ICMP_SGE) {
                if (distance == UINT64_MAX)
                    return false;
                distance++;
            }
            *trip = distance / amount + (distance % amount != 0);
            return true;
        }
        return false;
    }
    return unsigned_nowrap_count(width, pred, start, step, end, mask, trip);
}

/* The structural unroller is appended below the analysis helpers. */

static bool value_is_param(const IrBlock *block, IrOperand op, u32 *ordinal)
{
    u32 i;

    if (op.kind != IROP_VALUE)
        return false;
    for (i = 0; i < block->nparams; i++)
        if (block->params[i].v == op.a) {
            *ordinal = i;
            return true;
        }
    return false;
}

static bool value_is_block_param(const IrBlock *block, IrOperand op)
{
    u32 ignored;

    return value_is_param(block, op, &ignored);
}

static bool resolve_preheader_constant(const IrFunc *f, BlockId preheader,
                                       IrOperand op, IrOperand *constant)
{
    const IrBlock *pre = &f->blocks[preheader.v - 1];
    u32 ordinal;
    u32 bi;
    bool found = false;

    if (op.kind == IROP_ICONST) {
        *constant = op;
        return true;
    }
    if (!value_is_param(pre, op, &ordinal))
        return false;
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *term = f->blocks[bi].last;
        u32 ei;

        if (!term)
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            const IrEdge *edge = &term->edges[ei];

            if (edge->target.v != preheader.v)
                continue;
            if (ordinal >= edge->nargs ||
                edge->args[ordinal].kind != IROP_ICONST)
                return false;
            if (found && (constant->type != edge->args[ordinal].type ||
                          constant->a != edge->args[ordinal].a))
                return false;
            *constant = edge->args[ordinal];
            found = true;
        }
    }
    return found;
}

static IrInst *find_value_inst(IrFunc *f, ValueId value)
{
    IrValInfo info;
    IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    info = f->vals[value.v - 1];
    if (info.def_kind != VDEF_INST || !info.def_block.v ||
        info.def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[info.def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

static bool trace_header_param(IrFunc *f, const IrBlock *header, IrOperand op,
                               u32 *ordinal)
{
    u32 depth;

    for (depth = 0; depth < 4; depth++) {
        IrInst *cast;

        if (value_is_param(header, op, ordinal))
            return true;
        if (op.kind != IROP_VALUE)
            return false;
        cast = find_value_inst(f, (ValueId){(u32)op.a});
        if (!cast || cast->nops != 1 ||
            (cast->op != IR_ZEXT && cast->op != IR_SEXT &&
             cast->op != IR_TRUNC))
            return false;
        op = cast->ops[0];
    }
    return false;
}

static bool find_recurrence(IrFunc *f, const IrBlock *header, IrOperand next,
                            u32 iv_param, IrInst **update, u64 *step)
{
    IrInst *in;
    IrOperand base;
    IrOperand amount;
    u32 ordinal;

    if (next.kind != IROP_VALUE)
        return false;
    in = find_value_inst(f, (ValueId){(u32)next.a});
    if (in && in->op == IR_TRUNC && in->nops == 1 &&
        in->ops[0].kind == IROP_VALUE)
        in = find_value_inst(f, (ValueId){(u32)in->ops[0].a});
    if (!in || (in->op != IR_IADD && in->op != IR_ISUB) || in->nops != 2)
        return false;
    base = in->ops[0];
    amount = in->ops[1];
    if (!trace_header_param(f, header, base, &ordinal) || ordinal != iv_param ||
        amount.kind != IROP_ICONST)
        return false;
    *update = in;
    *step = amount.a;
    if (in->op == IR_ISUB)
        *step = ~*step + 1;
    return true;
}

static bool loop_has_pinned(const IrFunc *f, const Loop *loop)
{
    u32 bi;

    for (bi = 0; bi < loop_block_count(loop); bi++) {
        const IrBlock *block = &f->blocks[loop_block(loop, bi).v - 1];
        const IrInst *in;

        for (in = block->first; in; in = in->next)
            if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                return true;
    }
    return false;
}

static ValueId new_inst_value(IrModule *m, IrFunc *f, IrType type,
                              BlockId block, u32 pos)
{
    IrValInfo *values;
    ValueId result;

    if (f->nvals == f->cap_vals) {
        u32 cap = f->cap_vals ? f->cap_vals * 2 : 16;

        values =
            arena_alloc(m->arena, cap * sizeof(*values), _Alignof(IrValInfo));
        if (f->nvals)
            memcpy(values, f->vals, f->nvals * sizeof(*values));
        f->vals = values;
        f->cap_vals = cap;
    }
    memset(&f->vals[f->nvals], 0, sizeof(f->vals[f->nvals]));
    f->vals[f->nvals].type = (u8)type;
    f->vals[f->nvals].def_kind = VDEF_INST;
    f->vals[f->nvals].def_block = block;
    f->vals[f->nvals].def_pos = pos;
    result.v = ++f->nvals;
    return result;
}

static IrOperand substitute(IrOperand op, const IrOperand *map, u32 nmap)
{
    if (op.kind == IROP_VALUE && op.a < nmap && map[op.a].kind != IROP_NONE) {
        IrOperand replacement = map[op.a];

        replacement.b = op.b;
        return replacement;
    }
    return op;
}

static void append_inst(IrBlock *block, IrInst *in)
{
    in->next = NULL;
    if (block->last)
        block->last->next = in;
    else
        block->first = in;
    block->last = in;
    block->ninsts++;
}

static void clone_nonterms(IrModule *m, IrFunc *f, BlockId destination,
                           const IrBlock *source, IrOperand *map, u32 nmap)
{
    IrBlock *dst = &f->blocks[destination.v - 1];
    const IrInst *old;

    for (old = source->first; old && old != source->last; old = old->next) {
        IrInst *copy = arena_alloc(m->arena, sizeof(*copy), _Alignof(IrInst));
        u32 i;

        memcpy(copy, old, sizeof(*copy));
        copy->ops = NULL;
        copy->edges = NULL;
        copy->nedges = 0;
        if (old->nops) {
            copy->ops = arena_alloc(m->arena, old->nops * sizeof(*copy->ops),
                                    _Alignof(IrOperand));
            for (i = 0; i < old->nops; i++)
                copy->ops[i] = substitute(old->ops[i], map, nmap);
        }
        if (old->result.v) {
            copy->result = new_inst_value(m, f, (IrType)old->type, destination,
                                          dst->ninsts);
            if (old->result.v < nmap)
                map[old->result.v] = ir_op_value(f, copy->result);
        }
        append_inst(dst, copy);
    }
}

/* Clone the closed instruction range [first,last].  Unlike clone_nonterms,
 * this is safe after the source terminator has been detached: `last` is an
 * arena-stable instruction identity captured before appending any clones. */
static void clone_range(IrModule *m, IrFunc *f, BlockId destination,
                        const IrInst *first, const IrInst *last, IrOperand *map,
                        u32 nmap)
{
    IrBlock *dst = &f->blocks[destination.v - 1];
    const IrInst *old;

    for (old = first; old; old = old->next) {
        IrInst *copy = arena_alloc(m->arena, sizeof(*copy), _Alignof(IrInst));
        u32 i;

        memcpy(copy, old, sizeof(*copy));
        copy->ops = NULL;
        copy->edges = NULL;
        copy->nedges = 0;
        if (old->nops) {
            copy->ops = arena_alloc(m->arena, old->nops * sizeof(*copy->ops),
                                    _Alignof(IrOperand));
            for (i = 0; i < old->nops; i++)
                copy->ops[i] = substitute(old->ops[i], map, nmap);
        }
        if (old->result.v) {
            copy->result = new_inst_value(m, f, (IrType)old->type, destination,
                                          dst->ninsts);
            if (old->result.v < nmap)
                map[old->result.v] = ir_op_value(f, copy->result);
        }
        append_inst(dst, copy);
        if (old == last)
            break;
    }
}

typedef struct {
    const Loop *loop;
    IrBlock *header;
    IrBlock *latch;
    IrBlock *preheader;
    IrInst *compare;
    IrInst *update;
    IrEdge *preedge;
    IrEdge *backedge;
    IrEdge *exitedge;
    u32 iv_param;
    u64 trip;
    u32 cost;
} FullPlan;

static bool analyze_full(IrFunc *f, const Loop *loop, const OptConfig *cfg,
                         FullPlan *plan)
{
    TripInfo shared_trip;
    const char *trip_reason = NULL;
    BlockId header_id = loop_header(loop);
    BlockId latch_id;
    BlockId preheader_id = loop_preheader(loop);
    IrInst *term;
    IrOperand iv;
    IrOperand bound;
    IrOperand start;
    IrOperand start_constant;
    IrOperand next;
    IrIcmp pred;
    u64 step;
    bool modular;
    bool direct_iv;
    bool direct_update;
    bool have_shared_trip;
    u32 i;
    u64 static_copies;

    memset(plan, 0, sizeof(*plan));
    plan->loop = loop;
    if (loop_exit_count(loop) != 1) {
        OPT_BAIL(cfg, "unroll", "unroll_multi_exit");
        return false;
    }
    if (loop_latch_count(loop) != 1 || loop_block_count(loop) != 2 ||
        !preheader_id.v) {
        OPT_BAIL(cfg, "unroll", "unroll_shape");
        return false;
    }
    latch_id = loop_latch(loop, 0);
    plan->header = &f->blocks[header_id.v - 1];
    plan->latch = &f->blocks[latch_id.v - 1];
    plan->preheader = &f->blocks[preheader_id.v - 1];
    if (!plan->preheader->last || plan->preheader->last->op != IR_BR ||
        plan->preheader->last->nedges != 1 ||
        plan->preheader->last->edges[0].target.v != header_id.v ||
        !plan->latch->last || plan->latch->last->op != IR_BR ||
        plan->latch->last->nedges != 1 ||
        plan->latch->last->edges[0].target.v != header_id.v) {
        OPT_BAIL(cfg, "unroll", "unroll_shape");
        return false;
    }
    plan->preedge = &plan->preheader->last->edges[0];
    plan->backedge = &plan->latch->last->edges[0];
    term = plan->header->last;
    if (!term || term->op != IR_CONDBR || term->nops != 1 ||
        term->ops[0].kind != IROP_VALUE || term->nedges != 2) {
        OPT_BAIL(cfg, "unroll", "unroll_shape");
        return false;
    }
    for (i = 0; i < 2; i++)
        if (!loop_contains(loop, term->edges[i].target))
            plan->exitedge = &term->edges[i];
    if (!plan->exitedge ||
        plan->exitedge->target.v != loop_exit_target(loop, 0).v) {
        OPT_BAIL(cfg, "unroll", "unroll_shape");
        return false;
    }
    /* Trip counting below models the compare-true edge as one body
     * execution.  Inverted do-until shapes need predicate inversion before
     * they can use that proof; keep them intact until that transform exists. */
    if (plan->exitedge == &term->edges[0])
        goto unsupported;
    /* Share the canonical induction recognizer with the other loop passes.
     * The local checks below intentionally remain stricter: this transform
     * only accepts a direct two-block recurrence whose cloned instruction
     * order is mechanically evident.  A forwarding preheader can make the
     * shared result runtime-shaped; resolve_preheader_constant() below is
     * the narrow additional proof for that case. */
    have_shared_trip =
        loop_trip_analyze(f, loop, cfg->fwrapv, &shared_trip, &trip_reason);
    if (!have_shared_trip) {
        if (trip_reason && strcmp(trip_reason, "trip_wrap") == 0) {
            OPT_BAIL(cfg, "unroll", "unroll_trip_wrap");
            return false;
        }
        /* The shared service intentionally accepts only a direct header IV.
         * Keep the older transform-local trace below for trunc/zext lowered
         * subword recurrences; it is what proves exact modular nontermination
         * instead of weakening that case to an unsupported-shape bail. */
    }
    plan->compare = find_value_inst(f, (ValueId){(u32)term->ops[0].a});
    if (!plan->compare || plan->compare->op != IR_ICMP ||
        plan->compare->nops != 2)
        goto unsupported;
    pred = (IrIcmp)plan->compare->subop;
    iv = plan->compare->ops[0];
    bound = plan->compare->ops[1];
    direct_iv = value_is_param(plan->header, iv, &plan->iv_param);
    if ((!direct_iv &&
         !trace_header_param(f, plan->header, iv, &plan->iv_param)) ||
        bound.kind != IROP_ICONST)
        goto unsupported;
    if (plan->iv_param >= plan->preedge->nargs ||
        plan->iv_param >= plan->backedge->nargs ||
        plan->preedge->nargs != plan->header->nparams ||
        plan->backedge->nargs != plan->header->nparams)
        goto unsupported;
    for (i = 0; i < plan->exitedge->nargs; i++) {
        IrOperand arg = plan->exitedge->args[i];

        if (arg.kind == IROP_VALUE &&
            f->vals[arg.a - 1].def_block.v == header_id.v &&
            !value_is_block_param(plan->header, arg))
            goto unsupported;
    }
    start = plan->preedge->args[plan->iv_param];
    next = plan->backedge->args[plan->iv_param];
    if (!resolve_preheader_constant(f, preheader_id, start, &start_constant) ||
        !find_recurrence(f, plan->header, next, plan->iv_param, &plan->update,
                         &step))
        goto unsupported;
    direct_update =
        next.kind == IROP_VALUE && next.a == plan->update->result.v &&
        plan->update->ops[0].kind == IROP_VALUE &&
        plan->update->ops[0].a == plan->header->params[plan->iv_param].v;
    modular = pred == ICMP_EQ || pred == ICMP_NE || pred >= ICMP_ULT ||
              cfg->fwrapv || !(plan->update->flags & IRF_NSW) || !direct_iv;
    if (!opt_unroll_trip_count(
            ir_value_type(f, plan->header->params[plan->iv_param]), pred,
            start_constant.a, step, bound.a, modular, &plan->trip)) {
        OPT_BAIL(cfg, "unroll", "unroll_trip_wrap");
        return false;
    }
    if (have_shared_trip && shared_trip.kind == LOOP_TRIP_CONSTANT &&
        shared_trip.constant != plan->trip)
        CGF_ICE("unroll: shared and local trip proofs disagree");
    if (!direct_iv || !direct_update || plan->header->ninsts != 2)
        goto unsupported;
    plan->cost = plan->header->ninsts + plan->latch->ninsts - 2;
    if (plan->trip > 12) {
        OPT_BAIL(cfg, "unroll", "unroll_partial_unsupported");
        return false;
    }
    static_copies = plan->trip <= 8 ? plan->trip : 4 + plan->trip % 4;
    if (static_copies * plan->cost > cfg->unroll_threshold) {
        OPT_BAIL(cfg, "unroll", "unroll_size");
        return false;
    }
    if (cfg->level == OPT_OS && plan->trip > 1) {
        OPT_BAIL(cfg, "unroll", "unroll_size");
        return false;
    }
    if (loop_has_pinned(f, loop) && !(plan->trip >= 9 && plan->trip <= 12)) {
        OPT_BAIL(cfg, "unroll", "unroll_pinned");
        return false;
    }
    return true;

unsupported:
    OPT_BAIL(cfg, "unroll", "unroll_runtime_unsupported");
    return false;
}

static void detach_terminator(IrBlock *block, IrInst *term)
{
    IrInst *prev = NULL;
    IrInst *in;

    for (in = block->first; in && in != term; in = in->next)
        prev = in;
    if (in != term)
        CGF_ICE("unroll: preheader terminator is not in its block");
    if (prev)
        prev->next = NULL;
    else
        block->first = NULL;
    block->last = prev;
    block->ninsts--;
    term->next = NULL;
}

static void commit_full(IrModule *m, IrFunc *f, const FullPlan *plan)
{
    u32 old_nvals = f->nvals;
    u32 nmap = old_nvals + 1;
    IrOperand *map =
        arena_alloc(m->arena, nmap * sizeof(*map), _Alignof(IrOperand));
    IrOperand *next_params =
        arena_alloc(m->arena,
                    (plan->header->nparams ? plan->header->nparams : 1) *
                        sizeof(*next_params),
                    _Alignof(IrOperand));
    IrInst *term = plan->preheader->last;
    u64 iteration;
    u32 i;

    memset(map, 0, nmap * sizeof(*map));
    for (i = 0; i < plan->header->nparams; i++)
        map[plan->header->params[i].v] = plan->preedge->args[i];
    detach_terminator(plan->preheader, term);
    for (iteration = 0; iteration < plan->trip; iteration++) {
        clone_nonterms(m, f, loop_preheader(plan->loop), plan->header, map,
                       nmap);
        clone_nonterms(m, f, loop_preheader(plan->loop), plan->latch, map,
                       nmap);
        for (i = 0; i < plan->header->nparams; i++)
            next_params[i] = substitute(plan->backedge->args[i], map, nmap);
        for (i = 0; i < plan->header->nparams; i++)
            map[plan->header->params[i].v] = next_params[i];
    }
    memset(term, 0, sizeof(*term));
    term->op = IR_BR;
    term->type = IRT_VOID;
    term->nedges = 1;
    term->edges = arena_alloc(m->arena, sizeof(*term->edges), _Alignof(IrEdge));
    memset(term->edges, 0, sizeof(*term->edges));
    term->edges[0].target = plan->exitedge->target;
    term->edges[0].nargs = plan->exitedge->nargs;
    if (plan->exitedge->nargs) {
        term->edges[0].args = arena_alloc(
            m->arena, plan->exitedge->nargs * sizeof(*term->edges[0].args),
            _Alignof(IrOperand));
        for (i = 0; i < plan->exitedge->nargs; i++)
            term->edges[0].args[i] =
                substitute(plan->exitedge->args[i], map, nmap);
    }
    append_inst(plan->preheader, term);
    ir_func_remove_unreachable(f);
    ir_func_renumber(m->arena, f);
}

/* Constant partial unroll for the deliberately narrow canonical shape
 * accepted by analyze_full().  Peel trip%4 iterations into the preheader,
 * then serialize four copies of the original latch body in each remaining
 * iteration.  Prefix peeling is equivalent to a suffix remainder for a
 * constant trip count and avoids manufacturing a second live-out merge:
 * the original header/exit LCSSA edge remains the sole final-state path.
 *
 * No block is appended here.  IrInst identities and numeric IDs are stable
 * while value storage grows; all edge operands are resolved again at the
 * commit point rather than retained in a cross-mutation plan. */
static void commit_partial_four(IrModule *m, IrFunc *f, const FullPlan *plan)
{
    BlockId preheader_id = loop_preheader(plan->loop);
    BlockId latch_id = loop_latch(plan->loop, 0);
    IrBlock *preheader = &f->blocks[preheader_id.v - 1];
    IrBlock *latch = &f->blocks[latch_id.v - 1];
    IrInst *preterm = preheader->last;
    IrInst *latchterm = latch->last;
    IrInst *body_first = latch->first;
    IrInst *body_last = NULL;
    IrOperand *map;
    IrOperand *state;
    u32 nmap = f->nvals + 1;
    u32 nparams = plan->header->nparams;
    u32 i;
    u32 copy;
    u64 peel = plan->trip % 4;

    for (body_last = latch->first; body_last && body_last->next != latchterm;
         body_last = body_last->next)
        ;
    if (!body_first || !body_last || !preterm || !latchterm)
        CGF_ICE("unroll: partial plan lost its canonical terminators");
    map = arena_alloc(m->arena, nmap * sizeof(*map), _Alignof(IrOperand));
    state = arena_alloc(m->arena, (nparams ? nparams : 1) * sizeof(*state),
                        _Alignof(IrOperand));
    memset(map, 0, nmap * sizeof(*map));

    /* Peel the constant remainder, preserving the exact serial operation
     * order (including floating-point and pinned memory operations). */
    for (i = 0; i < nparams; i++)
        map[plan->header->params[i].v] = plan->preedge->args[i];
    /* Every peeled iteration is reached through the compare's continue edge.
     * A legal latch instruction may consume that header-defined predicate;
     * remap it explicitly so the peeled clone never references a definition
     * that still lives in the not-yet-executed loop header. */
    map[plan->compare->result.v] = ir_op_iconst((IrType)plan->compare->type, 1);
    detach_terminator(preheader, preterm);
    for (copy = 0; copy < peel; copy++) {
        clone_range(m, f, preheader_id, body_first, body_last, map, nmap);
        for (i = 0; i < nparams; i++)
            state[i] = substitute(plan->backedge->args[i], map, nmap);
        for (i = 0; i < nparams; i++)
            map[plan->header->params[i].v] = state[i];
    }
    preterm->edges[0].args = arena_alloc(
        m->arena, (nparams ? nparams : 1) * sizeof(*preterm->edges[0].args),
        _Alignof(IrOperand));
    preterm->edges[0].nargs = nparams;
    for (i = 0; i < nparams; i++)
        preterm->edges[0].args[i] = map[plan->header->params[i].v];
    append_inst(preheader, preterm);

    /* The original body is lane zero.  Each subsequent clone consumes the
     * prior lane's complete state, so FP reductions remain one serial chain. */
    memset(map, 0, nmap * sizeof(*map));
    for (i = 0; i < nparams; i++)
        map[plan->header->params[i].v] = plan->backedge->args[i];
    detach_terminator(latch, latchterm);
    for (copy = 1; copy < 4; copy++) {
        clone_range(m, f, latch_id, body_first, body_last, map, nmap);
        for (i = 0; i < nparams; i++)
            state[i] = substitute(plan->backedge->args[i], map, nmap);
        for (i = 0; i < nparams; i++)
            map[plan->header->params[i].v] = state[i];
    }
    latchterm->edges[0].args = arena_alloc(
        m->arena, (nparams ? nparams : 1) * sizeof(*latchterm->edges[0].args),
        _Alignof(IrOperand));
    latchterm->edges[0].nargs = nparams;
    for (i = 0; i < nparams; i++)
        latchterm->edges[0].args[i] = map[plan->header->params[i].v];
    append_inst(latch, latchterm);
    ir_func_renumber(m->arena, f);
}

static bool unroll_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    OptConfig fc = *cfg;
    bool changed = false;

    fc.current_func = f->name;
    for (;;) {
        Arena scratch;
        IrDomTree *dom;
        LoopTree *tree;
        FullPlan plan;
        u32 i;
        u32 max_depth = 0;
        bool committed = false;

        arena_init(&scratch);
        dom = ir_domtree_build(&scratch, f);
        tree = loop_tree_build(&scratch, f, dom);
        if (loop_tree_irreducible(tree)) {
            OPT_BAIL(&fc, "unroll", "loop_irreducible");
            arena_free_all(&scratch);
            break;
        }
        if (loop_canonicalize(m, f, tree)) {
            changed = true;
            arena_free_all(&scratch);
            continue;
        }
        if (cfg->verify_after_each) {
            char why[192];

            if (!loop_tree_verify_canonical(tree, f, why, sizeof(why)))
                CGF_ICE("unroll: non-canonical loop tree in '@%s': %s", f->name,
                        why);
        }
        for (i = 0; i < loop_tree_count(tree); i++) {
            const Loop *loop = loop_tree_at(tree, i);

            if (loop_depth(loop) > max_depth)
                max_depth = loop_depth(loop);
        }
        if (loop_tree_count(tree))
            for (;;) {
                for (i = 0; i < loop_tree_count(tree); i++) {
                    const Loop *loop = loop_tree_at(tree, i);

                    if (loop_depth(loop) != max_depth ||
                        !analyze_full(f, loop, &fc, &plan))
                        continue;
                    if (plan.trip <= 8)
                        commit_full(m, f, &plan);
                    else
                        commit_partial_four(m, f, &plan);
                    changed = true;
                    committed = true;
                    break;
                }
                if (committed || max_depth == 0)
                    break;
                max_depth--;
            }
        if (!committed) {
            arena_free_all(&scratch);
            break;
        }
        arena_free_all(&scratch);
    }
    return changed;
}

bool opt_unroll(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= unroll_func(m, &m->funcs[i], cfg);
    return changed;
}

const Pass OPT_PASS_UNROLL = {"unroll", opt_unroll,
                              PASS_PINNED_METADATA_CLONES};
