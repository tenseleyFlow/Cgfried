#include "opt/alias.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "opt/dep.h"
#include "opt/opt.h"
#include "util/base.h"

typedef struct {
    bool set;
    i64 lo;
    i64 hi;
} OffRange;

struct AllocSite {
    const IrInst *call;
    u32 obj;
    u32 id;
    bool owns_result;
    u32 out_param;
};

struct AliasCtx {
    IrModule *m;
    IrFunc *f;
    bool no_strict_aliasing;

    u32 nobj;
    u32 nwords;
    u32 unknown_obj;
    u32 first_alloca_obj;
    u32 first_restrict_obj;
    u32 first_alloc_site_obj;

    u64 *vpts;       /* [value id][word] */
    u64 *mpts;       /* pointer contents of abstract objects */
    u64 *stored_pts; /* explicit store/out-param edges; excludes call poison */
    u64 *spts;       /* [module symbol][word] */
    u64 *unknown_pts;
    OffRange *voff;
    OffRange *moff;  /* pointer-content offset hull per abstract object */
    u32 *alloca_obj; /* value id -> object id + 1 */
    bool *escaped;
    AllocSite *alloc_sites;
    u32 nalloc_sites;
};

static void *zalloc(size_t n, size_t size)
{
    void *p;

    if (size && n > SIZE_MAX / size)
        CGF_ICE("alias: allocation size overflow");
    p = cgf_xmalloc((n ? n : 1) * (size ? size : 1));
    memset(p, 0, n * size);
    return p;
}

static u64 *vrow(AliasCtx *c, u32 v)
{
    return c->vpts + (size_t)v * c->nwords;
}

static const u64 *cvrow(const AliasCtx *c, u32 v)
{
    return c->vpts + (size_t)v * c->nwords;
}

static u64 *mrow(AliasCtx *c, u32 obj)
{
    return c->mpts + (size_t)obj * c->nwords;
}

static u64 *stored_row(AliasCtx *c, u32 obj)
{
    return c->stored_pts + (size_t)obj * c->nwords;
}

static u64 *srow(AliasCtx *c, u32 sym)
{
    return c->spts + (size_t)sym * c->nwords;
}

static void bit_add(u64 *bits, u32 id)
{
    bits[id / 64] |= 1ull << (id % 64);
}

static bool bit_add_changed(u64 *bits, u32 id)
{
    u64 mask = 1ull << (id % 64);
    bool had = (bits[id / 64] & mask) != 0;

    bits[id / 64] |= mask;
    return !had;
}

static bool bits_or(u64 *dst, const u64 *src, u32 nwords)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < nwords; i++) {
        u64 old = dst[i];

        dst[i] |= src[i];
        changed |= old != dst[i];
    }
    return changed;
}

static bool bits_empty(const u64 *bits, u32 nwords)
{
    u32 i;

    for (i = 0; i < nwords; i++)
        if (bits[i])
            return false;
    return true;
}

static bool bits_has(const u64 *bits, u32 id)
{
    return (bits[id / 64] & (1ull << (id % 64))) != 0;
}

static bool bits_intersect(const u64 *a, const u64 *b, u32 nwords)
{
    u32 i;

    for (i = 0; i < nwords; i++)
        if (a[i] & b[i])
            return true;
    return false;
}

static u32 bits_singleton(const u64 *bits, u32 nwords)
{
    u32 i, found = UINT32_MAX;

    for (i = 0; i < nwords; i++) {
        u64 x = bits[i];

        while (x) {
            u32 bit = 0;
            u64 y = x;

            while ((y & 1) == 0) {
                bit++;
                y >>= 1;
            }
            if (found != UINT32_MAX)
                return UINT32_MAX;
            found = i * 64 + bit;
            x &= x - 1;
        }
    }
    return found;
}

static const u64 *operand_pts(const AliasCtx *c, IrOperand o)
{
    if (o.kind == IROP_VALUE && o.a > 0 && o.a <= c->f->nvals) {
        const u64 *row = cvrow(c, (u32)o.a);

        /* ptr->i64->ptr bitcasts preserve provenance.  Integer SSA values
         * without such provenance keep the honest UNKNOWN fallback. */
        if (!bits_empty(row, c->nwords))
            return row;
    }
    if (o.kind == IROP_SYMBOL && o.sym < c->m->nsyms && o.type == IRT_PTR)
        return c->spts + (size_t)o.sym * c->nwords;
    return c->unknown_pts;
}

static bool union_operand(AliasCtx *c, u64 *dst, IrOperand o)
{
    return bits_or(dst, operand_pts(c, o), c->nwords);
}

static i64 sat_add(i64 a, i64 b, bool *overflow)
{
    if (b > 0 && a > INT64_MAX - b) {
        *overflow = true;
        return INT64_MAX;
    }
    if (b < 0 && a < INT64_MIN - b) {
        *overflow = true;
        return INT64_MIN;
    }
    return a + b;
}

static bool off_join(OffRange *dst, OffRange src)
{
    i64 lo, hi;

    if (!src.set)
        return false;
    if (!dst->set) {
        *dst = src;
        return true;
    }
    lo = dst->lo < src.lo ? dst->lo : src.lo;
    hi = dst->hi > src.hi ? dst->hi : src.hi;
    if (lo == dst->lo && hi == dst->hi)
        return false;
    dst->lo = lo;
    dst->hi = hi;
    return true;
}

static OffRange operand_off(const AliasCtx *c, IrOperand o)
{
    if (o.kind == IROP_SYMBOL && o.type == IRT_PTR)
        return (OffRange){true, (i64)o.a, (i64)o.a};
    if (o.kind == IROP_VALUE && o.a > 0 && o.a <= c->f->nvals &&
        c->voff[o.a].set)
        return c->voff[o.a];
    return (OffRange){true, INT64_MIN, INT64_MAX};
}

static const IrInst *value_inst(const IrFunc *f, IrOperand operand)
{
    const IrValInfo *info;
    const IrInst *in;

    if (operand.kind != IROP_VALUE || operand.a == 0 || operand.a > f->nvals)
        return NULL;
    info = &f->vals[operand.a - 1];
    if (info->def_kind != VDEF_INST || info->def_block.v == 0 ||
        info->def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[info->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == operand.a)
            return in;
    return NULL;
}

static bool fold_integer_operand(const AliasCtx *c, IrOperand operand,
                                 IrOperand *out, u32 depth)
{
    const IrInst *def;
    IrInst folded;
    IrOperand ops[3];
    OptConfig cfg;
    u32 i;

    if (operand.kind == IROP_ICONST) {
        *out = operand;
        return true;
    }
    if (depth > c->f->nvals || !(def = value_inst(c->f, operand)) ||
        def->nops > CGF_ARRAY_LEN(ops))
        return false;
    switch (def->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
        break;
    default:
        return false;
    }
    folded = *def;
    for (i = 0; i < def->nops; i++)
        if (!fold_integer_operand(c, def->ops[i], &ops[i], depth + 1))
            return false;
    folded.ops = ops;
    opt_config_init(&cfg, OPT_O0);
    return opt_fold_inst(&folded, out, &cfg) && out->kind == IROP_ICONST;
}

static OffRange shifted_off(const AliasCtx *c, OffRange base, IrOperand off)
{
    bool overflow = false;
    IrOperand constant;
    i64 k, off_lo, off_hi;

    if (!base.set)
        return base;
    if (fold_integer_operand(c, off, &constant, 0)) {
        off_lo = (i64)constant.a;
        off_hi = off_lo;
    } else if (!dep_affine_range(c->f, off, &off_lo, &off_hi)) {
        return (OffRange){true, INT64_MIN, INT64_MAX};
    }
    k = off_lo;
    base.lo = sat_add(base.lo, k, &overflow);
    base.hi = sat_add(base.hi, off_hi, &overflow);
    if (overflow)
        return (OffRange){true, INT64_MIN, INT64_MAX};
    return base;
}

static bool is_ptr_value(const IrFunc *f, ValueId v)
{
    return v.v && v.v <= f->nvals && f->vals[v.v - 1].type == IRT_PTR;
}

static bool inst_belongs_to_func(const IrFunc *f, const IrInst *needle)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in == needle)
                return true;
    }
    return false;
}

static u32 call_first_arg(const IrInst *call)
{
    return call->subop == FUNCREF_INDIRECT ? 1 : 0;
}

static void validate_alloc_seeds(const AliasConfig *cfg)
{
    u32 i;

    if (cfg->nalloc_seeds && !cfg->alloc_seeds)
        CGF_ICE("alias: allocation seed count has no array");
    for (i = 0; i < cfg->nalloc_seeds; i++) {
        const AliasAllocSeed *seed = &cfg->alloc_seeds[i];
        const IrInst *call = seed->call;
        u32 first_arg, j;

        if (!call || call->op != IR_CALL ||
            !inst_belongs_to_func(cfg->func, call))
            CGF_ICE("alias: allocation seed is not a call in @%s",
                    cfg->func->name);
        if (!seed->owns_result && seed->out_param == ALIAS_NO_OUT_PARAM)
            CGF_ICE("alias: allocation seed publishes no owning pointer");
        if (seed->owns_result && (!call->result.v || call->type != IRT_PTR))
            CGF_ICE("alias: owned call result is not a pointer");
        first_arg = call_first_arg(call);
        if (seed->out_param != ALIAS_NO_OUT_PARAM &&
            (call->nops < first_arg ||
             seed->out_param >= call->nops - first_arg ||
             call->ops[first_arg + seed->out_param].type != IRT_PTR))
            CGF_ICE("alias: allocation out-parameter is not a pointer");
        for (j = 0; j < i; j++)
            if (cfg->alloc_seeds[j].call == call)
                CGF_ICE("alias: duplicate allocation seed in @%s",
                        cfg->func->name);
    }
}

static AllocSite *site_for_call(AliasCtx *c, const IrInst *call)
{
    u32 i;

    for (i = 0; i < c->nalloc_sites; i++)
        if (c->alloc_sites[i].call == call)
            return &c->alloc_sites[i];
    return NULL;
}

static void count_objects(const IrFunc *f, u32 *nalloca, u32 *nrestrict)
{
    u32 i, bi;

    *nalloca = 0;
    *nrestrict = 0;
    for (i = 0; i < f->nparams; i++)
        if (f->param_types[i] == IRT_PTR && f->param_annots &&
            ir_param_is_restrict(f->param_annots[i]))
            (*nrestrict)++;
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == IR_ALLOCA)
                (*nalloca)++;
    }
}

static void init_roots(AliasCtx *c, const AliasConfig *cfg)
{
    u32 i, bi;
    u32 next_alloca = c->first_alloca_obj;
    u32 next_restrict = c->first_restrict_obj;

    bit_add(c->unknown_pts, c->unknown_obj);
    for (i = 0; i < c->m->nsyms; i++)
        bit_add(srow(c, i), 1 + i);
    for (i = 0; i < c->f->nparams; i++) {
        ValueId v = c->f->param_vals[i];

        if (c->f->param_types[i] != IRT_PTR)
            continue;
        if (c->f->param_annots && ir_param_is_restrict(c->f->param_annots[i]))
            bit_add(vrow(c, v.v), next_restrict++);
        else
            bit_add(vrow(c, v.v), c->unknown_obj);
        c->voff[v.v] = (OffRange){true, 0, 0};
    }
    for (bi = 0; bi < c->f->nblocks; bi++) {
        const IrInst *in;

        for (in = c->f->blocks[bi].first; in; in = in->next) {
            if (in->op != IR_ALLOCA || !in->result.v)
                continue;
            bit_add(vrow(c, in->result.v), next_alloca);
            c->alloca_obj[in->result.v] = next_alloca + 1;
            c->voff[in->result.v] = (OffRange){true, 0, 0};
            next_alloca++;
        }
    }
    for (i = 0; i < cfg->nalloc_seeds; i++) {
        const AliasAllocSeed *seed = &cfg->alloc_seeds[i];
        AllocSite *site = &c->alloc_sites[i];

        site->call = seed->call;
        site->obj = c->first_alloc_site_obj + i;
        site->id = i + 1;
        site->owns_result = seed->owns_result;
        site->out_param = seed->out_param;
        if (seed->owns_result) {
            bit_add(vrow(c, seed->call->result.v), site->obj);
            c->voff[seed->call->result.v] = (OffRange){true, 0, 0};
        }
    }
}

static bool propagate_edges(AliasCtx *c)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < c->f->nblocks; bi++) {
        const IrInst *in;
        u32 ei;

        for (in = c->f->blocks[bi].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++) {
                const IrEdge *e = &in->edges[ei];
                const IrBlock *to;
                u32 ai;

                if (!e->target.v || e->target.v > c->f->nblocks)
                    continue;
                to = &c->f->blocks[e->target.v - 1];
                for (ai = 0; ai < e->nargs && ai < to->nparams; ai++) {
                    ValueId p = to->params[ai];

                    if (!is_ptr_value(c->f, p))
                        continue;
                    changed |= union_operand(c, vrow(c, p.v), e->args[ai]);
                    changed |=
                        off_join(&c->voff[p.v], operand_off(c, e->args[ai]));
                }
            }
    }
    return changed;
}

static bool propagate_store(AliasCtx *c, IrOperand val, IrOperand ptr)
{
    const u64 *targets = operand_pts(c, ptr);
    const u64 *values = operand_pts(c, val);
    bool changed = false;
    bool unknown_target = bits_has(targets, c->unknown_obj);
    u32 obj;

    for (obj = 0; obj < c->nobj; obj++) {
        if (!unknown_target && !bits_has(targets, obj))
            continue;
        changed |= bits_or(mrow(c, obj), values, c->nwords);
        changed |= bits_or(stored_row(c, obj), values, c->nwords);
        changed |= off_join(&c->moff[obj], operand_off(c, val));
    }
    return changed;
}

static bool propagate_load(AliasCtx *c, ValueId result, IrOperand ptr)
{
    const u64 *targets = operand_pts(c, ptr);
    u64 *dst = vrow(c, result.v);
    bool changed = false;
    bool unknown_target = bits_has(targets, c->unknown_obj);
    u32 obj;

    if (unknown_target) {
        changed |= bits_or(dst, c->unknown_pts, c->nwords);
        changed |= off_join(&c->voff[result.v],
                            (OffRange){true, INT64_MIN, INT64_MAX});
    }
    for (obj = 0; obj < c->nobj; obj++) {
        if (unknown_target || bits_has(targets, obj)) {
            changed |= bits_or(dst, mrow(c, obj), c->nwords);
            changed |= off_join(&c->voff[result.v], c->moff[obj]);
        }
    }
    return changed;
}

static bool poison_call_memory(AliasCtx *c, const IrInst *call,
                               const AllocSite *site)
{
    bool changed = false;
    u32 first_arg = call_first_arg(call);
    u64 *reachable = zalloc(c->nwords, sizeof(*reachable));
    u64 *exact_publish = zalloc(c->nwords, sizeof(*exact_publish));
    u32 oi, obj;

    /* A call can reach every global object, every object named by a pointer
     * argument, and the transitive pointer contents of either.  Compute that
     * closure before poisoning: a global may contain the address of a local
     * slot, so direct arguments alone are not a sound boundary. */
    for (obj = 1; obj < c->first_alloca_obj; obj++)
        bit_add(reachable, obj);
    for (oi = first_arg; oi < call->nops; oi++) {
        const u64 *targets;

        if (call->ops[oi].type != IRT_PTR)
            continue;
        targets = operand_pts(c, call->ops[oi]);
        bits_or(reachable, targets, c->nwords);
        if (site && site->out_param != ALIAS_NO_OUT_PARAM &&
            oi - first_arg == site->out_param &&
            bits_singleton(targets, c->nwords) != UINT32_MAX &&
            !bits_has(targets, c->unknown_obj))
            bits_or(exact_publish, targets, c->nwords);
    }
    for (;;) {
        bool grew = false;

        if (bits_has(reachable, c->unknown_obj)) {
            for (obj = 0; obj < c->nobj; obj++)
                bit_add(reachable, obj);
            break;
        }
        for (obj = 0; obj < c->nobj; obj++)
            if (bits_has(reachable, obj))
                grew |= bits_or(reachable, stored_row(c, obj), c->nwords);
        if (!grew)
            break;
    }
    for (obj = 0; obj < c->nobj; obj++)
        if (bits_has(reachable, obj) && !bits_has(exact_publish, obj)) {
            changed |= bits_or(mrow(c, obj), c->unknown_pts, c->nwords);
            changed |=
                off_join(&c->moff[obj], (OffRange){true, INT64_MIN, INT64_MAX});
        }
    free(exact_publish);
    free(reachable);
    return changed;
}

static bool propagate_insts(AliasCtx *c)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < c->f->nblocks; bi++) {
        const IrInst *in;

        for (in = c->f->blocks[bi].first; in; in = in->next) {
            if (in->op == IR_PTRADD && in->result.v && in->nops == 2) {
                changed |= union_operand(c, vrow(c, in->result.v), in->ops[0]);
                changed |= off_join(
                    &c->voff[in->result.v],
                    shifted_off(c, operand_off(c, in->ops[0]), in->ops[1]));
            } else if (in->op == IR_BITCAST && in->result.v && in->nops == 1 &&
                       ((in->type == IRT_PTR && in->ops[0].type == IRT_I64) ||
                        (in->type == IRT_I64 && in->ops[0].type == IRT_PTR))) {
                if (in->ops[0].type == IRT_PTR ||
                    !bits_empty(operand_pts(c, in->ops[0]), c->nwords)) {
                    changed |=
                        union_operand(c, vrow(c, in->result.v), in->ops[0]);
                    changed |= off_join(&c->voff[in->result.v],
                                        operand_off(c, in->ops[0]));
                } else {
                    changed |= bits_or(vrow(c, in->result.v), c->unknown_pts,
                                       c->nwords);
                    changed |= off_join(&c->voff[in->result.v],
                                        (OffRange){true, INT64_MIN, INT64_MAX});
                }
            } else if (in->op == IR_SELECT && in->result.v &&
                       in->type == IRT_PTR && in->nops == 3) {
                changed |= union_operand(c, vrow(c, in->result.v), in->ops[1]);
                changed |= union_operand(c, vrow(c, in->result.v), in->ops[2]);
                changed |= off_join(&c->voff[in->result.v],
                                    operand_off(c, in->ops[1]));
                changed |= off_join(&c->voff[in->result.v],
                                    operand_off(c, in->ops[2]));
            } else if (in->op == IR_LOAD && in->result.v &&
                       in->type == IRT_PTR && in->nops == 1) {
                changed |= propagate_load(c, in->result, in->ops[0]);
            } else if (in->op == IR_STORE && in->nops == 2 &&
                       in->ops[0].type == IRT_PTR) {
                changed |= propagate_store(c, in->ops[0], in->ops[1]);
            } else if (in->op == IR_MEMCPY && in->nops >= 2) {
                /* A byte copy may transport pointer representations.  Without
                 * typed aggregate fields the only sound flow-insensitive
                 * content summary is UNKNOWN at every possible destination. */
                const u64 *targets = operand_pts(c, in->ops[0]);
                bool unknown_target = bits_has(targets, c->unknown_obj);
                u32 obj;

                for (obj = 0; obj < c->nobj; obj++)
                    if (unknown_target || bits_has(targets, obj)) {
                        changed |=
                            bits_or(mrow(c, obj), c->unknown_pts, c->nwords);
                        changed |= bits_or(stored_row(c, obj), c->unknown_pts,
                                           c->nwords);
                        changed |=
                            off_join(&c->moff[obj],
                                     (OffRange){true, INT64_MIN, INT64_MAX});
                    }
            } else if (in->op == IR_CALL) {
                AllocSite *site = site_for_call(c, in);

                if (in->result.v && in->type == IRT_PTR &&
                    !(site && site->owns_result)) {
                    changed |= bits_or(vrow(c, in->result.v), c->unknown_pts,
                                       c->nwords);
                    changed |= off_join(&c->voff[in->result.v],
                                        (OffRange){true, INT64_MIN, INT64_MAX});
                }
                if (site && site->out_param != ALIAS_NO_OUT_PARAM) {
                    u32 first_arg = call_first_arg(in);
                    const u64 *targets =
                        operand_pts(c, in->ops[first_arg + site->out_param]);
                    bool unknown_target = bits_has(targets, c->unknown_obj);
                    u32 obj;

                    for (obj = 0; obj < c->nobj; obj++)
                        if (unknown_target || bits_has(targets, obj)) {
                            changed |= bit_add_changed(mrow(c, obj), site->obj);
                            changed |=
                                bit_add_changed(stored_row(c, obj), site->obj);
                            changed |=
                                off_join(&c->moff[obj], (OffRange){true, 0, 0});
                        }
                }
                /* No mod/ref summaries yet: globals and objects reachable
                 * through pointer arguments may receive arbitrary pointers.
                 * Unpassed local allocas are not reachable by a call. */
                changed |= poison_call_memory(c, in, site);
            }
        }
    }
    return changed;
}

static void mark_operand_escaped(AliasCtx *c, IrOperand o)
{
    const u64 *pts = operand_pts(c, o);
    u32 obj;

    for (obj = 0; obj < c->nobj; obj++)
        if (bits_has(pts, obj))
            c->escaped[obj] = true;
}

static void mark_escapes(AliasCtx *c)
{
    u32 bi;

    for (bi = 0; bi < c->f->nblocks; bi++) {
        const IrInst *in;

        for (in = c->f->blocks[bi].first; in; in = in->next) {
            u32 oi;

            if (in->op == IR_STORE && in->nops == 2 &&
                in->ops[0].type == IRT_PTR)
                mark_operand_escaped(c, in->ops[0]);
            else if (in->op == IR_CALL) {
                oi = in->subop == FUNCREF_INDIRECT ? 1 : 0;
                for (; oi < in->nops; oi++)
                    if (in->ops[oi].type == IRT_PTR)
                        mark_operand_escaped(c, in->ops[oi]);
            } else if (in->op == IR_RET && in->nops == 1 &&
                       in->ops[0].type == IRT_PTR) {
                mark_operand_escaped(c, in->ops[0]);
            }
        }
    }
}

AliasCtx *alias_build(IrModule *m, const AliasConfig *cfg)
{
    AliasCtx *c;
    u32 nalloca, nrestrict;
    u32 iteration, cap;

    if (!m || !cfg || !cfg->func)
        CGF_ICE("alias_build: module and function are required");
    validate_alloc_seeds(cfg);
    c = zalloc(1, sizeof(*c));
    c->m = m;
    c->f = cfg->func;
    c->no_strict_aliasing = cfg->no_strict_aliasing;
    count_objects(c->f, &nalloca, &nrestrict);
    c->unknown_obj = 0;
    c->first_alloca_obj = 1 + m->nsyms;
    c->first_restrict_obj = c->first_alloca_obj + nalloca;
    c->first_alloc_site_obj = c->first_restrict_obj + nrestrict;
    c->nalloc_sites = cfg->nalloc_seeds;
    c->nobj = c->first_alloc_site_obj + c->nalloc_sites;
    if (c->nobj == 0)
        c->nobj = 1;
    c->nwords = (c->nobj + 63) / 64;
    c->vpts = zalloc((size_t)c->f->nvals + 1, c->nwords * sizeof(u64));
    c->mpts = zalloc(c->nobj, c->nwords * sizeof(u64));
    c->stored_pts = zalloc(c->nobj, c->nwords * sizeof(u64));
    c->spts = zalloc(m->nsyms, c->nwords * sizeof(u64));
    c->unknown_pts = zalloc(c->nwords, sizeof(u64));
    c->voff = zalloc((size_t)c->f->nvals + 1, sizeof(OffRange));
    c->moff = zalloc(c->nobj, sizeof(OffRange));
    c->alloca_obj = zalloc((size_t)c->f->nvals + 1, sizeof(u32));
    c->escaped = zalloc(c->nobj, sizeof(bool));
    c->alloc_sites = zalloc(c->nalloc_sites, sizeof(AllocSite));
    init_roots(c, cfg);

    /* Every constraint is monotone over finite bitsets/range hulls.  The cap
     * is a corruption guard, not a precision bailout: one new bit or one
     * range-end widening must occur on each changing iteration. */
    cap = (c->f->nvals + c->nobj + 1) * (c->nobj + 2) * 2;
    for (iteration = 0; iteration < cap; iteration++) {
        bool changed = propagate_edges(c);

        changed |= propagate_insts(c);
        if (!changed)
            break;
    }
    if (iteration == cap)
        CGF_ICE("alias: points-to solver did not converge in @%s", c->f->name);
    /* A pointer with no discovered source is an unknown pointer, never an
     * empty proof set. */
    for (iteration = 1; iteration <= c->f->nvals; iteration++)
        if (c->f->vals[iteration - 1].type == IRT_PTR &&
            bits_empty(vrow(c, iteration), c->nwords)) {
            bits_or(vrow(c, iteration), c->unknown_pts, c->nwords);
            c->voff[iteration] = (OffRange){true, INT64_MIN, INT64_MAX};
        }
    mark_escapes(c);
    return c;
}

void alias_free(AliasCtx *c)
{
    if (!c)
        return;
    free(c->alloc_sites);
    free(c->escaped);
    free(c->alloca_obj);
    free(c->moff);
    free(c->voff);
    free(c->unknown_pts);
    free(c->spts);
    free(c->stored_pts);
    free(c->mpts);
    free(c->vpts);
    free(c);
}

PtsSet alias_points_to(AliasCtx *c, IrOperand ptr)
{
    const u64 *bits = operand_pts(c, ptr);
    PtsSet out;

    out.words = bits;
    out.nwords = c->nwords;
    out.has_unknown = bits_has(bits, c->unknown_obj);
    return out;
}

bool alias_offset_range(AliasCtx *c, IrOperand ptr, i64 *lo, i64 *hi)
{
    OffRange r = operand_off(c, ptr);

    if (!r.set || (r.lo == INT64_MIN && r.hi == INT64_MAX))
        return false;
    if (lo)
        *lo = r.lo;
    if (hi)
        *hi = r.hi;
    return true;
}

bool alias_pts_must_be_nonheap(const AliasCtx *c, PtsSet pts)
{
    bool any = false;
    u32 obj;

    if (!c || !pts.words || pts.nwords != c->nwords || pts.has_unknown)
        return false;
    for (obj = 0; obj < c->nobj; obj++) {
        if (!bits_has(pts.words, obj))
            continue;
        any = true;
        if (obj == c->unknown_obj || obj >= c->first_restrict_obj)
            return false;
    }
    return any;
}

const AllocSite *alias_alloc_site(const AliasCtx *c, const IrInst *call)
{
    u32 i;

    if (!c || !call)
        return NULL;
    for (i = 0; i < c->nalloc_sites; i++)
        if (c->alloc_sites[i].call == call)
            return &c->alloc_sites[i];
    return NULL;
}

u32 alias_alloc_site_count(const AliasCtx *c)
{
    return c ? c->nalloc_sites : 0;
}

const AllocSite *alias_alloc_site_at(const AliasCtx *c, u32 index)
{
    if (!c || index >= c->nalloc_sites)
        return NULL;
    return &c->alloc_sites[index];
}

u32 alias_alloc_site_id(const AllocSite *site)
{
    return site ? site->id : 0;
}

const IrInst *alias_alloc_site_call(const AllocSite *site)
{
    return site ? site->call : NULL;
}

bool alias_pts_has_alloc_site(const AliasCtx *c, PtsSet pts,
                              const AllocSite *site)
{
    u32 i;

    if (!c || !pts.words || pts.nwords != c->nwords || !site)
        return false;
    for (i = 0; i < c->nalloc_sites; i++)
        if (site == &c->alloc_sites[i])
            return bits_has(pts.words, site->obj);
    return false;
}

const AllocSite *alias_pts_unique_alloc_site(const AliasCtx *c, PtsSet pts)
{
    u32 obj;

    if (!c || !pts.words || pts.nwords != c->nwords)
        return NULL;
    obj = bits_singleton(pts.words, pts.nwords);
    if (obj == UINT32_MAX || obj < c->first_alloc_site_obj ||
        obj - c->first_alloc_site_obj >= c->nalloc_sites)
        return NULL;
    return &c->alloc_sites[obj - c->first_alloc_site_obj];
}

u32 alias_reachable_alloc_sites(const AliasCtx *c, IrOperand root,
                                const AllocSite **out, u32 capacity,
                                bool *has_unknown)
{
    u64 *reachable;
    u32 count = 0;
    u32 obj, i;

    if (has_unknown)
        *has_unknown = false;
    if (!c)
        return 0;
    reachable = zalloc(c->nwords, sizeof(*reachable));
    bits_or(reachable, operand_pts(c, root), c->nwords);
    for (;;) {
        bool changed = false;

        for (obj = 0; obj < c->nobj; obj++)
            if (bits_has(reachable, obj))
                changed |=
                    bits_or(reachable, c->stored_pts + (size_t)obj * c->nwords,
                            c->nwords);
        if (!changed)
            break;
    }
    if (has_unknown)
        *has_unknown = bits_has(reachable, c->unknown_obj);
    for (i = 0; i < c->nalloc_sites; i++) {
        if (!bits_has(reachable, c->alloc_sites[i].obj))
            continue;
        if (out && count < capacity)
            out[count] = &c->alloc_sites[i];
        count++;
    }
    free(reachable);
    return count;
}

bool alias_escapes(AliasCtx *c, IrOperand base)
{
    const u64 *pts = operand_pts(c, base);
    u32 obj;

    if (bits_has(pts, c->unknown_obj))
        return true;
    for (obj = 0; obj < c->nobj; obj++)
        if (bits_has(pts, obj) && c->escaped[obj])
            return true;
    return false;
}

MemLoc alias_memloc(AliasCtx *c, IrOperand ptr, u64 size, EffTypeId etype)
{
    MemLoc out;

    (void)c;
    out.base = ptr;
    out.off_lo = 0;
    out.off_hi = 0;
    out.size = size;
    out.etype = etype;
    return out;
}

typedef struct {
    i64 lo;
    i64 hi;
    bool known;
} Footprint;

static Footprint footprint(AliasCtx *c, MemLoc loc)
{
    OffRange base = operand_off(c, loc.base);
    Footprint f = {0};
    bool overflow = false;
    i64 size_tail;

    if (!base.set || base.lo == INT64_MIN || base.hi == INT64_MAX ||
        loc.off_lo == INT64_MIN || loc.off_hi == INT64_MAX || loc.size == 0 ||
        loc.size - 1 > (u64)INT64_MAX)
        return f;
    size_tail = (i64)(loc.size - 1);
    f.lo = sat_add(base.lo, loc.off_lo, &overflow);
    f.hi = sat_add(base.hi, loc.off_hi, &overflow);
    f.hi = sat_add(f.hi, size_tail, &overflow);
    f.known = !overflow && f.lo <= f.hi;
    return f;
}

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static bool type_disjoint(EffTypeId a, EffTypeId b)
{
    if (a == ETYPE_UNKNOWN || b == ETYPE_UNKNOWN || a == ETYPE_CHAR ||
        b == ETYPE_CHAR || a == ETYPE_UNION || b == ETYPE_UNION ||
        a == ETYPE_AGGREGATE || b == ETYPE_AGGREGATE)
        return false;
    return a != b;
}

AliasResult alias_query(AliasCtx *c, MemLoc a, MemLoc b)
{
    const u64 *ap = operand_pts(c, a.base);
    const u64 *bp = operand_pts(c, b.base);
    bool a_unknown = bits_has(ap, c->unknown_obj);
    bool b_unknown = bits_has(bp, c->unknown_obj);
    u32 ao = bits_singleton(ap, c->nwords);
    u32 bo = bits_singleton(bp, c->nwords);
    Footprint af = footprint(c, a);
    Footprint bf = footprint(c, b);
    bool same_object = ao != UINT32_MAX && ao == bo &&
                       (ao != c->unknown_obj || operand_equal(a.base, b.base));

    if (a.size == 0 || b.size == 0)
        return ALIAS_NO;
    if (same_object && af.known && bf.known && af.lo == bf.lo && af.hi == bf.hi)
        return ALIAS_MUST;

    /* Proven provenance/range facts are independent of TBAA.  Character and
     * union wildcards suppress only the type-based proof below. */
    if (!a_unknown && !b_unknown && !bits_intersect(ap, bp, c->nwords))
        return ALIAS_NO;
    if (same_object && af.known && bf.known && (af.hi < bf.lo || bf.hi < af.lo))
        return ALIAS_NO;
    if (operand_equal(a.base, b.base) && af.known && bf.known &&
        (af.hi < bf.lo || bf.hi < af.lo))
        return ALIAS_NO;
    if (!c->no_strict_aliasing && type_disjoint(a.etype, b.etype))
        return ALIAS_NO;
    return ALIAS_MAY;
}

bool alias_covers(AliasCtx *c, MemLoc outer, MemLoc inner)
{
    const u64 *op = operand_pts(c, outer.base);
    const u64 *ip = operand_pts(c, inner.base);
    u32 oo = bits_singleton(op, c->nwords);
    u32 io = bits_singleton(ip, c->nwords);
    Footprint of = footprint(c, outer);
    Footprint inf = footprint(c, inner);

    if (oo == UINT32_MAX || io == UINT32_MAX || oo != io)
        return false;
    if (oo == c->unknown_obj && !operand_equal(outer.base, inner.base))
        return false;
    return of.known && inf.known && of.lo <= inf.lo && of.hi >= inf.hi;
}
