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
    if (!in || in->nops != 2)
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
           in->op == IR_STACKRESTORE;
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

static bool access_for_inst(const IrFunc *f, const IrInst *in, ValueId iv,
                            DepAccess *out, const char **reason)
{
    IrOperand ptr;
    IrType type;

    if (in->op == IR_LOAD) {
        ptr = in->ops[0];
        type = (IrType)in->type;
    } else if (in->op == IR_STORE) {
        ptr = in->ops[1];
        type = (IrType)in->ops[0].type;
    } else {
        return false;
    }
    return dep_access_from_ptr(f, ptr, iv, type_size(type),
                               (EffTypeId)in->subop, out, reason);
}

static bool fusion_dependences_ok(IrModule *m, IrFunc *f, const IrBlock *abody,
                                  ValueId aiv, const IrBlock *bbody,
                                  ValueId biv, const OptConfig *cfg)
{
    AliasConfig acfg = {f, cfg->no_strict_aliasing};
    AliasCtx *alias = alias_build(m, &acfg);
    const IrInst *ai, *bi;
    bool ok = true;

    for (ai = abody->first; ok && ai && !ai->nedges; ai = ai->next) {
        DepAccess aa;
        const char *reason;

        if (ai->op != IR_LOAD && ai->op != IR_STORE)
            continue;
        if (!access_for_inst(f, ai, aiv, &aa, &reason)) {
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
            if (!access_for_inst(f, bi, biv, &ba, &reason)) {
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
    if (!fusion_dependences_ok(m, f, abody, ta.induction.iv, bbody,
                               tb.induction.iv, cfg))
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
