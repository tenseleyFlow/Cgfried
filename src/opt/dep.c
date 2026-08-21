#include "opt/dep.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "opt/opt.h"
#include "util/arena.h"
#include "util/base.h"

const Pass OPT_PASS_FUSION = {"fusion", opt_fusion, PASS_PINNED_EXACT};

typedef struct Affine {
    i64 k;
    i64 c;
} Affine;

struct DepRangeCtx {
    Arena scratch;
    const IrFunc *f;
    IrDomTree *dom;
    LoopTree *tree;
};

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static const IrInst *def_inst(const IrFunc *f, ValueId value)
{
    const IrValInfo *vi;
    const IrInst *in;

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

static bool checked_add(i64 a, i64 b, i64 *out)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b))
        return false;
    *out = a + b;
    return true;
}

static bool checked_sub(i64 a, i64 b, i64 *out)
{
    if ((b > 0 && a < INT64_MIN + b) || (b < 0 && a > INT64_MAX + b))
        return false;
    *out = a - b;
    return true;
}

static bool checked_mul(i64 a, i64 b, i64 *out)
{
    if (a == 0 || b == 0) {
        *out = 0;
        return true;
    }
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN))
        return false;
    if (a > 0) {
        if ((b > 0 && a > INT64_MAX / b) || (b < 0 && b < INT64_MIN / a))
            return false;
    } else if ((b > 0 && a < INT64_MIN / b) || (b < 0 && a < INT64_MAX / b)) {
        return false;
    }
    *out = a * b;
    return true;
}

static bool affine_expr(const IrFunc *f, IrOperand op, ValueId iv, u32 depth,
                        Affine *out)
{
    const IrInst *in;
    Affine a, b;

    if (depth > 16)
        return false;
    if (op.kind == IROP_ICONST) {
        out->k = 0;
        out->c = (i64)op.a;
        return true;
    }
    if (op.kind != IROP_VALUE)
        return false;
    if (op.a == iv.v) {
        out->k = 1;
        out->c = 0;
        return true;
    }
    in = def_inst(f, (ValueId){(u32)op.a});
    if (!in)
        return false;
    if ((in->op == IR_SEXT || in->op == IR_ZEXT) && in->nops == 1)
        return affine_expr(f, in->ops[0], iv, depth + 1, out);
    if (in->nops != 2)
        return false;
    /* simplify canonicalizes multiplication by a power of two into shl
     * before the O3 loop group.  Dependence analysis must understand that
     * equivalent affine spelling or default-pipeline fusion becomes dead. */
    if (in->op == IR_SHL) {
        i64 scale;

        if (in->ops[1].kind != IROP_ICONST || in->ops[1].a > 62 ||
            !affine_expr(f, in->ops[0], iv, depth + 1, &a))
            return false;
        scale = (i64)(UINT64_C(1) << in->ops[1].a);
        return checked_mul(a.k, scale, &out->k) &&
               checked_mul(a.c, scale, &out->c);
    }
    if (in->op != IR_IADD && in->op != IR_ISUB && in->op != IR_IMUL)
        return false;
    if (!affine_expr(f, in->ops[0], iv, depth + 1, &a) ||
        !affine_expr(f, in->ops[1], iv, depth + 1, &b))
        return false;
    if (in->op == IR_IADD)
        return checked_add(a.k, b.k, &out->k) && checked_add(a.c, b.c, &out->c);
    if (in->op == IR_ISUB)
        return checked_sub(a.k, b.k, &out->k) && checked_sub(a.c, b.c, &out->c);
    if (a.k && b.k)
        return false;
    if (a.k)
        return checked_mul(a.k, b.c, &out->k) && checked_mul(a.c, b.c, &out->c);
    return checked_mul(b.k, a.c, &out->k) && checked_mul(b.c, a.c, &out->c);
}

static u32 affine_type_bits(IrType type)
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

static bool signed_loop_predicate(IrIcmp pred)
{
    return pred >= ICMP_SLT && pred <= ICMP_SGE;
}

static bool bits_as_i64(u64 value, u32 bits, bool is_signed, i64 *out)
{
    u64 mask, sign, magnitude;

    if (!bits)
        return false;
    mask = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
    value &= mask;
    if (!is_signed) {
        if (value > (u64)INT64_MAX)
            return false;
        *out = (i64)value;
        return true;
    }
    sign = UINT64_C(1) << (bits - 1);
    if (!(value & sign)) {
        *out = (i64)value;
        return true;
    }
    magnitude = (~value + 1) & mask;
    if (magnitude == (UINT64_C(1) << 63)) {
        *out = INT64_MIN;
        return true;
    }
    *out = -(i64)magnitude;
    return true;
}

DepRangeCtx *dep_range_ctx_new(const IrFunc *f)
{
    DepRangeCtx *ctx;

    if (!f)
        return NULL;
    ctx = cgf_xmalloc(sizeof(*ctx));
    arena_init(&ctx->scratch);
    ctx->f = f;
    ctx->dom = ir_domtree_build(&ctx->scratch, f);
    ctx->tree = loop_tree_build(&ctx->scratch, f, ctx->dom);
    return ctx;
}

void dep_range_ctx_free(DepRangeCtx *ctx)
{
    if (!ctx)
        return;
    arena_free_all(&ctx->scratch);
    free(ctx);
}

static bool affine_range(const DepRangeCtx *ctx, IrOperand operand,
                         BlockId access_block, i64 *lo, i64 *hi)
{
    const IrFunc *f;
    u32 li;
    bool found = false;

    if (!ctx || !lo || !hi)
        return false;
    f = ctx->f;
    for (li = 0; li < loop_tree_count(ctx->tree) && !found; li++) {
        const Loop *loop = loop_tree_at(ctx->tree, li);
        TripInfo trip;
        Affine expr;
        const char *reason = NULL;
        i64 start, step, last, first_value, last_value, delta;
        u32 bits;
        bool is_signed;

        if (!loop_trip_analyze(f, loop, false, &trip, &reason) ||
            trip.kind != LOOP_TRIP_CONSTANT || trip.constant == 0 ||
            loop_exit_count(loop) != 1 ||
            trip.induction.start.kind != IROP_ICONST ||
            trip.induction.step.kind != IROP_ICONST ||
            !affine_expr(f, operand, trip.induction.iv, 0, &expr) ||
            expr.k == 0)
            continue;
        if (access_block.v) {
            /* The full-iteration interval proves a default-tier diagnostic
             * only when the actual access executes on every path to the
             * latch.  A pointer computed before a conditional dereference is
             * insufficient: the invalid endpoint may be skipped. */
            if (!loop_contains(loop, access_block) ||
                !ir_dominates(ctx->dom, access_block, trip.induction.latch))
                continue;
        } else if (operand.kind == IROP_VALUE && operand.a <= f->nvals) {
            BlockId at = f->vals[operand.a - 1].def_block;

            /* A range over all iterations proves that this operation occurs
             * at the bad endpoint only when its defining/access block is on
             * every path to the latch.  Conditional bodies remain MAY and
             * are rejected for the default warning tier. */
            if (at.v && loop_contains(loop, at) &&
                !ir_dominates(ctx->dom, at, trip.induction.latch))
                continue;
        }
        bits = affine_type_bits(trip.induction.type);
        is_signed = signed_loop_predicate(trip.induction.pred);
        if (!bits_as_i64(trip.induction.start.a, bits, is_signed, &start) ||
            !bits_as_i64(trip.induction.step.a, bits, is_signed, &step))
            continue;
        if (trip.induction.subtract_step) {
            if (step == INT64_MIN)
                continue;
            step = -step;
        }
        if (trip.constant - 1 > (u64)INT64_MAX ||
            !checked_mul(step, (i64)(trip.constant - 1), &delta) ||
            !checked_add(start, delta, &last) ||
            !checked_mul(expr.k, start, &first_value) ||
            !checked_add(first_value, expr.c, &first_value) ||
            !checked_mul(expr.k, last, &last_value) ||
            !checked_add(last_value, expr.c, &last_value))
            continue;
        *lo = first_value < last_value ? first_value : last_value;
        *hi = first_value > last_value ? first_value : last_value;
        found = true;
    }
    return found;
}

bool dep_affine_range_ctx(const DepRangeCtx *ctx, IrOperand operand, i64 *lo,
                          i64 *hi)
{
    return affine_range(ctx, operand, (BlockId){0}, lo, hi);
}

bool dep_affine_range(const IrFunc *f, IrOperand operand, i64 *lo, i64 *hi)
{
    DepRangeCtx *ctx = dep_range_ctx_new(f);
    bool found = dep_affine_range_ctx(ctx, operand, lo, hi);

    dep_range_ctx_free(ctx);
    return found;
}

bool dep_affine_range_at(const IrFunc *f, IrOperand operand,
                         BlockId access_block, i64 *lo, i64 *hi)
{
    DepRangeCtx *ctx;
    bool found;

    if (!access_block.v)
        return false;
    ctx = dep_range_ctx_new(f);
    found = affine_range(ctx, operand, access_block, lo, hi);
    dep_range_ctx_free(ctx);
    return found;
}

static bool affine_ptr_range(const DepRangeCtx *ctx, IrOperand pointer,
                             BlockId access_block, i64 *lo, i64 *hi)
{
    const IrFunc *f = ctx ? ctx->f : NULL;
    const IrInst *in;
    i64 base_lo = 0, base_hi = 0, add_lo, add_hi;

    if (!f || pointer.kind != IROP_VALUE ||
        !(in = def_inst(f, (ValueId){(u32)pointer.a})))
        return false;
    if (in->op == IR_BITCAST && in->nops == 1)
        return affine_ptr_range(ctx, in->ops[0], access_block, lo, hi);
    if (in->op != IR_PTRADD || in->nops != 2 ||
        !affine_range(ctx, in->ops[1], access_block, &add_lo, &add_hi))
        return false;
    if (in->ops[0].kind == IROP_VALUE) {
        const IrInst *base = def_inst(f, (ValueId){(u32)in->ops[0].a});

        if (base && (base->op == IR_PTRADD || base->op == IR_BITCAST) &&
            !affine_ptr_range(ctx, in->ops[0], access_block, &base_lo,
                              &base_hi))
            return false;
    }
    return checked_add(base_lo, add_lo, lo) && checked_add(base_hi, add_hi, hi);
}

bool dep_affine_ptr_range(const IrFunc *f, IrOperand pointer, i64 *lo, i64 *hi)
{
    DepRangeCtx *ctx = dep_range_ctx_new(f);
    bool found = affine_ptr_range(ctx, pointer, (BlockId){0}, lo, hi);

    dep_range_ctx_free(ctx);
    return found;
}

bool dep_affine_ptr_range_at(const IrFunc *f, IrOperand pointer,
                             BlockId access_block, i64 *lo, i64 *hi)
{
    DepRangeCtx *ctx;
    bool found;

    if (!access_block.v)
        return false;
    ctx = dep_range_ctx_new(f);
    found = affine_ptr_range(ctx, pointer, access_block, lo, hi);
    dep_range_ctx_free(ctx);
    return found;
}

bool dep_access_from_ptr(const IrFunc *f, IrOperand ptr, ValueId iv, u64 size,
                         EffTypeId etype, DepAccess *out, const char **reason)
{
    const IrInst *in;
    Affine off;

    memset(out, 0, sizeof(*out));
    *reason = "dep_nonaffine";
    if (ptr.kind != IROP_VALUE)
        return false;
    in = def_inst(f, (ValueId){(u32)ptr.a});
    if (!in || in->op != IR_PTRADD || in->nops != 2 ||
        !affine_expr(f, in->ops[1], iv, 0, &off) || off.k == 0)
        return false;
    out->base = in->ops[0];
    out->stride = off.k;
    out->offset = off.c;
    out->size = size;
    out->etype = etype;
    *reason = NULL;
    return true;
}

static u64 abs_i64(i64 x)
{
    return x < 0 ? (u64)(-(x + 1)) + 1 : (u64)x;
}

static bool points_to_disjoint(AliasCtx *alias, IrOperand a, IrOperand b)
{
    PtsSet ap = alias_points_to(alias, a);
    PtsSet bp = alias_points_to(alias, b);
    u32 i;
    bool have_a = false;
    bool have_b = false;

    /* Alias analysis collapses module symbols the current function never
     * names into one compact sentinel object. Direct symbols still carry
     * their exact stable ids, so distinct ids prove distinct storage before
     * consulting that intentionally coarser points-to representation. */
    if (a.kind == IROP_SYMBOL && b.kind == IROP_SYMBOL && a.sym != b.sym)
        return true;
    if (ap.has_unknown || bp.has_unknown)
        return false;
    for (i = 0; i < ap.nwords; i++)
        have_a |= ap.words[i] != 0;
    for (i = 0; i < bp.nwords; i++)
        have_b |= bp.words[i] != 0;
    for (i = 0; i < ap.nwords && i < bp.nwords; i++)
        if (ap.words[i] & bp.words[i])
            return false;
    return have_a && have_b;
}

DepResult dep_query(AliasCtx *alias, DepAccess a, DepAccess b)
{
    DepResult r = {DEP_UNKNOWN, 0, "dep_nonaffine"};
    i64 diff;
    u64 stride, residue, gap;

    if (!a.size || !b.size || !a.stride || a.stride != b.stride)
        return r;
    if (!operand_equal(a.base, b.base) &&
        points_to_disjoint(alias, a.base, b.base)) {
        r.kind = DEP_INDEPENDENT;
        r.reason = NULL;
        return r;
    }
    if (!operand_equal(a.base, b.base)) {
        r.reason = "dep_bases_may_alias";
        return r;
    }
    if (!checked_sub(a.offset, b.offset, &diff))
        return r;
    stride = abs_i64(a.stride);
    residue = abs_i64(diff) % stride;
    gap = residue < stride - residue ? residue : stride - residue;
    if (residue != 0) {
        /* Equal-sized accesses narrower than the gap between the two residue
         * classes can never overlap in any pair of iterations. */
        if (a.size == b.size && a.size <= gap) {
            r.kind = DEP_INDEPENDENT;
            r.reason = NULL;
        }
        return r;
    }
    /* A unique exact distance requires equal accesses no wider than one
     * stride.  Wider sliding windows can overlap at several distances. */
    if (a.size != b.size || a.size > stride)
        return r;
    if (diff == INT64_MIN && a.stride == -1)
        return r;
    r.kind = DEP_DISTANCE;
    r.distance = diff / a.stride;
    r.reason = NULL;
    return r;
}

static u64 type_size(IrType type)
{
    switch (type) {
    case IRT_I8:
        return 1;
    case IRT_I16:
        return 2;
    case IRT_I32:
    case IRT_F32:
        return 4;
    case IRT_I64:
    case IRT_PTR:
    case IRT_F64:
        return 8;
    case IRT_F80:
    case IRT_F128:
        return 16;
    default:
        return 0;
    }
}

static bool is_pinned(const IrInst *in)
{
    return (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) != 0 ||
           in->op == IR_ATOMICRMW || in->op == IR_CMPXCHG ||
           in->op == IR_VA_START || in->op == IR_STACKSAVE ||
           in->op == IR_STACKRESTORE || in->op == IR_ASM;
}

static bool supported_body_op(const IrInst *in)
{
    if (is_pinned(in) || in->op == IR_CALL || in->nedges)
        return false;
    switch (in->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_ICMP:
    case IR_PTRADD:
    case IR_LOAD:
    case IR_STORE:
        return true;
    default:
        return false;
    }
}

static bool clone_operand_available(const IrFunc *f, const Loop *loop,
                                    const IrDomTree *dom,
                                    BlockId source_preheader, BlockId body,
                                    BlockId destination_preheader, ValueId iv,
                                    IrOperand op)
{
    const IrValInfo *vi;

    if (op.kind != IROP_VALUE || op.a == iv.v)
        return true;
    if (!op.a || op.a > f->nvals)
        return false;
    vi = &f->vals[op.a - 1];
    if (!vi->def_block.v)
        return true;
    if (vi->def_block.v == body.v)
        return true; /* verifier guarantees definition-before-use */
    /* The old header and dedicated preheader become unreachable.  Only the
     * induction parameter has an explicit replacement in the fused loop. */
    return vi->def_block.v != source_preheader.v &&
           !loop_contains(loop, vi->def_block) &&
           ir_dominates(dom, vi->def_block, destination_preheader);
}

static bool operand_defined_in_loop(const IrFunc *f, const Loop *loop,
                                    IrOperand op)
{
    const IrValInfo *vi;

    if (op.kind != IROP_VALUE || !op.a || op.a > f->nvals)
        return false;
    vi = &f->vals[op.a - 1];
    return vi->def_block.v && loop_contains(loop, vi->def_block);
}

static bool loop_has_direct_liveout(const IrFunc *f, const Loop *loop)
{
    u32 bi, oi, ei, ai;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        if (loop_contains(loop, (BlockId){bi + 1}))
            continue;
        for (in = f->blocks[bi].first; in; in = in->next) {
            for (oi = 0; oi < in->nops; oi++)
                if (operand_defined_in_loop(f, loop, in->ops[oi]))
                    return true;
            for (ei = 0; ei < in->nedges; ei++)
                for (ai = 0; ai < in->edges[ei].nargs; ai++)
                    if (operand_defined_in_loop(f, loop,
                                                in->edges[ei].args[ai]))
                        return true;
        }
    }
    return false;
}

static const IrInst *terminator(const IrBlock *b)
{
    return b->last && b->last->nedges ? b->last : NULL;
}

static u32 pred_count(const IrFunc *f, BlockId target)
{
    u32 bi, ei, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;
        for (in = f->blocks[bi].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++)
                n += in->edges[ei].target.v == target.v;
    }
    return n;
}

static bool trip_equal(const TripInfo *a, const TripInfo *b)
{
    /* LOOP_TRIP_RUNTIME describes a recurrence but does not yet prove that
     * modular execution reaches its runtime bound.  Fusing two such loops
     * could make the second loop observable when the first never terminates. */
    return a->kind == LOOP_TRIP_CONSTANT && b->kind == LOOP_TRIP_CONSTANT &&
           a->constant == b->constant &&
           a->induction.type == b->induction.type &&
           a->induction.pred == b->induction.pred &&
           a->induction.subtract_step == b->induction.subtract_step &&
           operand_equal(a->induction.start, b->induction.start) &&
           operand_equal(a->induction.step, b->induction.step) &&
           operand_equal(a->induction.bound, b->induction.bound);
}

static bool access_for_inst(const IrFunc *f, const IrInst *in,
                            const LoopInduction *induction, DepAccess *out,
                            const char **reason)
{
    IrOperand ptr;
    IrType type;
    i64 step;
    u32 bits;

    if (in->op == IR_LOAD) {
        ptr = in->ops[0];
        type = (IrType)in->type;
    } else if (in->op == IR_STORE) {
        ptr = in->ops[1];
        type = (IrType)in->ops[0].type;
    } else {
        return false;
    }
    if (!dep_access_from_ptr(f, ptr, induction->iv, type_size(type),
                             (EffTypeId)in->subop, out, reason))
        return false;
    bits = affine_type_bits(induction->type);
    if (induction->step.kind != IROP_ICONST ||
        !bits_as_i64(induction->step.a, bits,
                     signed_loop_predicate(induction->pred), &step) ||
        (induction->subtract_step && step == INT64_MIN)) {
        *reason = "dep_nonaffine";
        return false;
    }
    if (induction->subtract_step)
        step = -step;
    /* OPT-H-02: dep_query measures distance in iteration order, while the
     * affine recognizer's coefficient is per induction-value unit. Include
     * the signed induction step so descending loops reverse the dependence
     * sign and non-unit loops retain their true ordinal distance. */
    if (!checked_mul(out->stride, step, &out->stride) || !out->stride) {
        *reason = "dep_nonaffine";
        return false;
    }
    return true;
}

static bool fusion_dependences_ok(IrModule *m, IrFunc *f, const IrBlock *abody,
                                  const LoopInduction *ainduction,
                                  const IrBlock *bbody,
                                  const LoopInduction *binduction,
                                  const OptConfig *cfg)
{
    AliasConfig acfg = {
        .func = f,
        .no_strict_aliasing = cfg->no_strict_aliasing,
    };
    AliasCtx *alias = alias_build(m, &acfg);
    const IrInst *ai, *bi;
    bool ok = true;

    for (ai = abody->first; ok && ai && !ai->nedges; ai = ai->next) {
        DepAccess aa;
        const char *reason;

        if (ai->op != IR_LOAD && ai->op != IR_STORE)
            continue;
        if (!access_for_inst(f, ai, ainduction, &aa, &reason)) {
            OPT_BAIL(cfg, "fusion", "dep_nonaffine");
            ok = false;
            break;
        }
        for (bi = bbody->first; bi && !bi->nedges; bi = bi->next) {
            DepAccess ba;
            DepResult dep;

            if ((bi->op != IR_LOAD && bi->op != IR_STORE) ||
                (ai->op == IR_LOAD && bi->op == IR_LOAD))
                continue;
            if (!access_for_inst(f, bi, binduction, &ba, &reason)) {
                OPT_BAIL(cfg, "fusion", "dep_nonaffine");
                ok = false;
                break;
            }
            dep = dep_query(alias, aa, ba);
            if (dep.kind == DEP_UNKNOWN) {
                if (dep.reason &&
                    strcmp(dep.reason, "dep_bases_may_alias") == 0)
                    OPT_BAIL(cfg, "fusion", "dep_bases_may_alias");
                else
                    OPT_BAIL(cfg, "fusion", "dep_nonaffine");
                ok = false;
                break;
            }
            if (dep.kind == DEP_DISTANCE && dep.distance < 0) {
                OPT_BAIL(cfg, "fusion", "fuse_negative_dep");
                ok = false;
                break;
            }
        }
    }
    alias_free(alias);
    return ok;
}

static IrOperand remap_operand(const IrFunc *f, IrOperand op,
                               const IrOperand *map, u32 nmap)
{
    if (op.kind == IROP_VALUE && op.a < nmap && map[op.a].kind != IROP_NONE)
        return map[op.a];
    (void)f;
    return op;
}

static bool clone_inst(IrBuilder *b, const IrInst *in, IrOperand *map, u32 nmap)
{
    IrOperand x = in->nops > 0 ? remap_operand(b->f, in->ops[0], map, nmap)
                               : (IrOperand){0};
    IrOperand y = in->nops > 1 ? remap_operand(b->f, in->ops[1], map, nmap)
                               : (IrOperand){0};
    ValueId result = VALUE_INVALID;

    ir_builder_set_span(b, ir_inst_span(b->m, in));
    switch (in->op) {
    case IR_ICMP:
        result = ir_build_icmp(b, (IrIcmp)in->subop, x, y);
        ir_block(b->f, b->block)->last->flags = in->flags;
        break;
    case IR_PTRADD:
        result = ir_build_ptradd(b, x, y);
        break;
    case IR_LOAD:
        result = ir_build_load_typed(b, (IrType)in->type, x, in->align,
                                     in->flags, (EffTypeId)in->subop);
        break;
    case IR_STORE:
        ir_build_store_typed(b, x, y, in->align, in->flags,
                             (EffTypeId)in->subop);
        break;
    default:
        result =
            ir_build2_flags(b, (IrOp)in->op, (IrType)in->type, x, y, in->flags);
        break;
    }
    if (in->result.v) {
        if (!result.v || in->result.v >= nmap)
            return false;
        map[in->result.v] = ir_op_value(b->f, result);
    }
    return true;
}

static bool fuse_pair(IrModule *m, IrFunc *f, const IrDomTree *dom,
                      const Loop *a, const Loop *b, const OptConfig *cfg)
{
    TripInfo ta, tb;
    const char *reason;
    IrBlock *ah, *abody, *bpre, *bh, *bbody, *bexit;
    IrInst *aterm, *bterm, *prev, *in;
    BlockId abody_id, bbody_id, bexit_id;
    IrOperand *map;
    IrOperand *back_args;
    u32 nback, i;
    IrBuilder builder;

    if (loop_block_count(a) != 2 || loop_block_count(b) != 2 ||
        loop_latch_count(a) != 1 || loop_latch_count(b) != 1 ||
        loop_exit_count(a) != 1 || loop_exit_count(b) != 1 ||
        loop_exit_target(a, 0).v != loop_preheader(b).v ||
        pred_count(f, loop_preheader(b)) != 1) {
        OPT_BAIL(cfg, "fusion", "fuse_intervening");
        return false;
    }
    if (!loop_trip_analyze(f, a, cfg->fwrapv, &ta, &reason) ||
        !loop_trip_analyze(f, b, cfg->fwrapv, &tb, &reason) ||
        !trip_equal(&ta, &tb)) {
        OPT_BAIL(cfg, "fusion", "fuse_trip_mismatch");
        return false;
    }
    ah = ir_block(f, loop_header(a));
    bh = ir_block(f, loop_header(b));
    bpre = ir_block(f, loop_preheader(b));
    if (ah->nparams != 1 || bh->nparams != 1 || bpre->nparams ||
        !terminator(bpre) || bpre->ninsts != 1 || bpre->last->op != IR_BR ||
        bpre->last->edges[0].target.v != loop_header(b).v) {
        OPT_BAIL(cfg, "fusion", "fuse_intervening");
        return false;
    }
    aterm = ah->last;
    bterm = bh->last;
    if (!aterm || !bterm || aterm->op != IR_CONDBR || bterm->op != IR_CONDBR) {
        OPT_BAIL(cfg, "fusion", "fuse_intervening");
        return false;
    }
    abody_id = aterm->edges[ta.induction.continue_edge].target;
    bbody_id = bterm->edges[tb.induction.continue_edge].target;
    bexit_id = bterm->edges[1 - tb.induction.continue_edge].target;
    abody = ir_block(f, abody_id);
    bbody = ir_block(f, bbody_id);
    bexit = ir_block(f, bexit_id);
    if (abody_id.v != loop_latch(a, 0).v || bbody_id.v != loop_latch(b, 0).v ||
        abody->nparams || bbody->nparams || bexit->nparams ||
        bterm->edges[1 - tb.induction.continue_edge].nargs) {
        OPT_BAIL(cfg, "fusion", "fuse_intervening");
        return false;
    }
    if (loop_has_direct_liveout(f, b)) {
        OPT_BAIL(cfg, "fusion", "fuse_intervening");
        return false;
    }
    for (in = abody->first; in && !in->nedges; in = in->next)
        if (!supported_body_op(in)) {
            OPT_BAIL(cfg, "fusion", "fuse_intervening");
            return false;
        }
    for (in = bbody->first; in && !in->nedges; in = in->next)
        if (!supported_body_op(in)) {
            OPT_BAIL(cfg, "fusion", "fuse_intervening");
            return false;
        } else
            for (i = 0; i < in->nops; i++)
                if (!clone_operand_available(f, b, dom, loop_preheader(b),
                                             bbody_id, loop_preheader(a),
                                             tb.induction.iv, in->ops[i])) {
                    OPT_BAIL(cfg, "fusion", "fuse_intervening");
                    return false;
                }
    if (!fusion_dependences_ok(m, f, abody, &ta.induction, bbody, &tb.induction,
                               cfg))
        return false;

    /* Save the A backedge before opening its block for appends. */
    in = abody->last;
    if (!in || in->op != IR_BR || in->nedges != 1 ||
        in->edges[0].target.v != loop_header(a).v) {
        OPT_BAIL(cfg, "fusion", "fuse_intervening");
        return false;
    }
    nback = in->edges[0].nargs;
    back_args = arena_alloc(m->arena, (nback ? nback : 1) * sizeof(*back_args),
                            _Alignof(IrOperand));
    if (nback)
        memcpy(back_args, in->edges[0].args, nback * sizeof(*back_args));
    prev = NULL;
    for (aterm = abody->first; aterm && aterm != in; aterm = aterm->next)
        prev = aterm;
    if (prev)
        prev->next = NULL;
    else
        abody->first = NULL;
    abody->last = prev;
    abody->ninsts--;

    map = cgf_xmalloc(((size_t)f->nvals + 1) * sizeof(*map));
    memset(map, 0, ((size_t)f->nvals + 1) * sizeof(*map));
    map[tb.induction.iv.v] = ir_op_value(f, ta.induction.iv);
    ir_builder_at(&builder, m, f, abody_id);
    for (in = bbody->first; in && !in->nedges; in = in->next)
        if (!clone_inst(&builder, in, map, f->nvals + 1))
            CGF_ICE("fusion: preflighted instruction clone failed");
    free(map);
    ir_build_br(&builder, loop_header(a), back_args, nback);
    /* A's zero-trip/exit edge now skips the dead second loop. */
    i = 1 - ta.induction.continue_edge;
    aterm = ah->last;
    aterm->edges[i].target = bexit_id;
    aterm->edges[i].args = NULL;
    aterm->edges[i].nargs = 0;
    ir_func_remove_unreachable(f);
    ir_func_renumber(m->arena, f);
    return true;
}

bool opt_fusion(IrModule *m, const OptConfig *cfg)
{
    u32 fi;
    bool changed = false;

    for (fi = 0; fi < m->nfuncs; fi++) {
        IrFunc *f = &m->funcs[fi];
        OptConfig fc = *cfg;
        bool again = true;

        if (opt_func_has_vector_ir(f))
            continue;
        fc.current_func = f->name;
        while (again) {
            Arena scratch;
            IrDomTree *dom;
            LoopTree *tree;
            u32 ai, bi;

            again = false;
            arena_init(&scratch);
            dom = ir_domtree_build(&scratch, f);
            tree = loop_tree_build(&scratch, f, dom);
            if (!loop_tree_irreducible(tree))
                for (ai = 0; ai < loop_tree_count(tree) && !again; ai++)
                    for (bi = 0; bi < loop_tree_count(tree); bi++) {
                        const Loop *a = loop_tree_at(tree, ai);
                        const Loop *b = loop_tree_at(tree, bi);

                        if (a == b || loop_parent(a) != loop_parent(b))
                            continue;
                        if (fuse_pair(m, f, dom, a, b, &fc)) {
                            changed = again = true;
                            break;
                        }
                    }
            arena_free_all(&scratch);
        }
    }
    return changed;
}
