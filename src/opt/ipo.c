#include "opt/opt.h"

#include <stdlib.h>
#include <string.h>

/* The callgraph is deliberately independent of the module arena: clients
 * rebuild it after an IPO mutation and ipo_callgraph_free has real meaning. */
struct Callgraph {
    u32 nnodes;
    bool *edges; /* caller-major adjacency matrix */
    bool *unknown;
    bool *address_taken;
    u32 nsccs;
    u32 *scc_of;
    u32 *scc_offsets;
    u32 *scc_members;
};

typedef struct Callgraph Callgraph;

static void *zalloc(size_t n, size_t size)
{
    void *p = cgf_xmalloc((n ? n : 1) * (size ? size : 1));

    memset(p, 0, (n ? n : 1) * (size ? size : 1));
    return p;
}

static i32 func_for_symbol(const IrModule *m, u32 sym)
{
    u32 i;

    if (sym >= m->nsyms)
        return -1;
    for (i = 0; i < m->nfuncs; i++)
        if (m->funcs[i].name == m->syms[sym] ||
            strcmp(m->funcs[i].name, m->syms[sym]) == 0)
            return (i32)i;
    return -1;
}

static void mark_symbol_operand(const IrModule *m, Callgraph *g,
                                const IrOperand *op)
{
    i32 fi;

    if (op->kind != IROP_SYMBOL)
        return;
    fi = func_for_symbol(m, op->sym);
    if (fi >= 0)
        g->address_taken[fi] = true;
}

typedef struct {
    Callgraph *g;
    i32 next;
    i32 *index;
    i32 *low;
    u32 *stack;
    u32 nstack;
    bool *onstack;
    u32 *raw_members;
    u32 *raw_offsets;
} Tarjan;

static void tarjan_visit(Tarjan *t, u32 v)
{
    u32 w;

    t->index[v] = t->next;
    t->low[v] = t->next++;
    t->stack[t->nstack++] = v;
    t->onstack[v] = true;
    for (w = 0; w < t->g->nnodes; w++) {
        if (!t->g->edges[v * t->g->nnodes + w])
            continue;
        if (t->index[w] < 0) {
            tarjan_visit(t, w);
            if (t->low[w] < t->low[v])
                t->low[v] = t->low[w];
        } else if (t->onstack[w] && t->index[w] < t->low[v]) {
            t->low[v] = t->index[w];
        }
    }
    if (t->low[v] == t->index[v]) {
        u32 begin = t->raw_offsets[t->g->nsccs];
        u32 end;

        do {
            w = t->stack[--t->nstack];
            t->onstack[w] = false;
            t->g->scc_of[w] = t->g->nsccs;
            t->raw_members[t->raw_offsets[t->g->nsccs + 1]++] = w;
        } while (w != v);
        end = t->raw_offsets[t->g->nsccs + 1];
        /* Tarjan's pop order is traversal-dependent. Public member order is
         * module order, keeping dumps and tests deterministic. */
        for (v = begin + 1; v < end; v++) {
            u32 x = t->raw_members[v];
            u32 p = v;

            while (p > begin && t->raw_members[p - 1] > x) {
                t->raw_members[p] = t->raw_members[p - 1];
                p--;
            }
            t->raw_members[p] = x;
        }
        t->g->nsccs++;
        if (t->g->nsccs < t->g->nnodes)
            t->raw_offsets[t->g->nsccs + 1] = t->raw_offsets[t->g->nsccs];
    }
}

Callgraph *ipo_callgraph_build(IrModule *m)
{
    Callgraph *g = zalloc(1, sizeof(*g));
    Tarjan t;
    u32 fi, bi;

    g->nnodes = m->nfuncs;
    g->edges = zalloc((size_t)g->nnodes * g->nnodes, sizeof(bool));
    g->unknown = zalloc(g->nnodes, sizeof(bool));
    g->address_taken = zalloc(g->nnodes, sizeof(bool));
    g->scc_of = zalloc(g->nnodes, sizeof(u32));
    g->scc_offsets = zalloc((size_t)g->nnodes + 1, sizeof(u32));
    g->scc_members = zalloc(g->nnodes, sizeof(u32));

    for (fi = 0; fi < m->nfuncs; fi++) {
        IrFunc *f = &m->funcs[fi];

        for (bi = 0; bi < f->nblocks; bi++) {
            IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                u32 oi, ei;

                if (in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
                    in->callee < m->nfuncs)
                    g->edges[fi * g->nnodes + in->callee] = true;
                if (in->op == IR_CALL && in->subop == FUNCREF_INDIRECT)
                    g->unknown[fi] = true;
                for (oi = 0; oi < in->nops; oi++)
                    mark_symbol_operand(m, g, &in->ops[oi]);
                for (ei = 0; ei < in->nedges; ei++)
                    for (oi = 0; oi < in->edges[ei].nargs; oi++)
                        mark_symbol_operand(m, g, &in->edges[ei].args[oi]);
            }
        }
    }
    /* Function-pointer tables retain an address without an instruction. */
    for (fi = 0; fi < m->nglobals; fi++) {
        u32 ri;

        for (ri = 0; ri < m->globals[fi].nrelocs; ri++) {
            i32 target = func_for_symbol(m, m->globals[fi].relocs[ri].symbol);

            if (target >= 0)
                g->address_taken[target] = true;
        }
    }

    memset(&t, 0, sizeof(t));
    t.g = g;
    t.index = zalloc(g->nnodes, sizeof(i32));
    t.low = zalloc(g->nnodes, sizeof(i32));
    t.stack = zalloc(g->nnodes, sizeof(u32));
    t.onstack = zalloc(g->nnodes, sizeof(bool));
    t.raw_members = g->scc_members;
    t.raw_offsets = g->scc_offsets;
    for (fi = 0; fi < g->nnodes; fi++)
        t.index[fi] = -1;
    for (fi = 0; fi < g->nnodes; fi++)
        if (t.index[fi] < 0)
            tarjan_visit(&t, fi);
    free(t.index);
    free(t.low);
    free(t.stack);
    free(t.onstack);
    return g;
}

void ipo_callgraph_free(Callgraph *g)
{
    if (!g)
        return;
    free(g->edges);
    free(g->unknown);
    free(g->address_taken);
    free(g->scc_of);
    free(g->scc_offsets);
    free(g->scc_members);
    free(g);
}

u32 ipo_callgraph_node_count(const Callgraph *g)
{
    return g->nnodes;
}

u32 ipo_callgraph_edge_count(const Callgraph *g, u32 caller)
{
    u32 i, n = 0;

    if (caller >= g->nnodes)
        return 0;
    for (i = 0; i < g->nnodes; i++)
        n += g->edges[caller * g->nnodes + i];
    return n;
}

u32 ipo_callgraph_edge(const Callgraph *g, u32 caller, u32 ordinal)
{
    u32 i;

    if (caller >= g->nnodes)
        return UINT32_MAX;
    for (i = 0; i < g->nnodes; i++)
        if (g->edges[caller * g->nnodes + i] && ordinal-- == 0)
            return i;
    return UINT32_MAX;
}

bool ipo_callgraph_has_unknown_callees(const Callgraph *g, u32 node)
{
    return node < g->nnodes && g->unknown[node];
}

bool ipo_callgraph_address_taken(const Callgraph *g, u32 node)
{
    return node < g->nnodes && g->address_taken[node];
}

u32 ipo_callgraph_scc_count(const Callgraph *g)
{
    return g->nsccs;
}

u32 ipo_callgraph_scc_of(const Callgraph *g, u32 node)
{
    return node < g->nnodes ? g->scc_of[node] : UINT32_MAX;
}

u32 ipo_callgraph_scc_size(const Callgraph *g, u32 scc)
{
    return scc < g->nsccs ? g->scc_offsets[scc + 1] - g->scc_offsets[scc] : 0;
}

u32 ipo_callgraph_scc_member(const Callgraph *g, u32 scc, u32 ordinal)
{
    u32 begin;

    if (scc >= g->nsccs)
        return UINT32_MAX;
    begin = g->scc_offsets[scc];
    if (ordinal >= g->scc_offsets[scc + 1] - begin)
        return UINT32_MAX;
    return g->scc_members[begin + ordinal];
}

/* Tarjan emits a component after every descendant component, exactly the
 * bottom-up order required by IPO and the inliner. */
u32 ipo_callgraph_bottom_up_scc(const Callgraph *g, u32 ordinal)
{
    return ordinal < g->nsccs ? ordinal : UINT32_MAX;
}

static bool linkage_allows_ipo(const IrFunc *f, const OptConfig *cfg)
{
    switch ((IrLinkage)f->linkage) {
    case IRLINK_INTERNAL:
        return true;
    case IRLINK_EXTERNAL:
        OPT_BAIL(cfg, "ipo", "ipo_external_linkage");
        return false;
    case IRLINK_COMMON:
        OPT_BAIL(cfg, "ipo", "ipo_common_symbol");
        return false;
    default:
        CGF_ICE("ipo: unhandled function linkage %u", f->linkage);
    }
}

static bool operand_uses_value(const IrOperand *op, ValueId v)
{
    return op->kind == IROP_VALUE && op->a == v.v;
}

static bool func_uses_value(const IrFunc *f, ValueId v)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 i, e;

            for (i = 0; i < in->nops; i++)
                if (operand_uses_value(&in->ops[i], v))
                    return true;
            for (e = 0; e < in->nedges; e++)
                for (i = 0; i < in->edges[e].nargs; i++)
                    if (operand_uses_value(&in->edges[e].args[i], v))
                        return true;
        }
    }
    return false;
}

static bool value_has_annotated_use(const IrFunc *f, ValueId v)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 i, e;

            for (i = 0; i < in->nops; i++)
                if (operand_uses_value(&in->ops[i], v) && in->ops[i].b)
                    return true;
            for (e = 0; e < in->nedges; e++)
                for (i = 0; i < in->edges[e].nargs; i++)
                    if (operand_uses_value(&in->edges[e].args[i], v) &&
                        in->edges[e].args[i].b)
                        return true;
        }
    }
    return false;
}

static void replace_value(IrFunc *f, ValueId v, IrOperand with)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 i, e;

            for (i = 0; i < in->nops; i++)
                if (operand_uses_value(&in->ops[i], v)) {
                    u64 annotation = in->ops[i].b;

                    in->ops[i] = with;
                    /* `.b` belongs to the destination when this is an ABI
                     * call argument. Ordinary operands have zero there;
                     * f80/f128 constants instead keep their high bits. */
                    if (with.kind != IROP_FCONST)
                        in->ops[i].b = annotation;
                }
            for (e = 0; e < in->nedges; e++)
                for (i = 0; i < in->edges[e].nargs; i++)
                    if (operand_uses_value(&in->edges[e].args[i], v)) {
                        u64 annotation = in->edges[e].args[i].b;

                        in->edges[e].args[i] = with;
                        if (with.kind != IROP_FCONST)
                            in->edges[e].args[i].b = annotation;
                    }
        }
    }
}

static bool func_reads_common(const IrModule *m, const IrFunc *f)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 oi;

            for (oi = 0; oi < in->nops; oi++) {
                u32 gi;

                if (in->ops[oi].kind != IROP_SYMBOL)
                    continue;
                for (gi = 0; gi < m->nglobals; gi++)
                    if (m->globals[gi].linkage == IRLINK_COMMON &&
                        in->ops[oi].sym < m->nsyms &&
                        strcmp(m->globals[gi].name, m->syms[in->ops[oi].sym]) ==
                            0)
                        return true;
            }
        }
    }
    return false;
}

static bool const_same(IrOperand a, IrOperand b)
{
    if (a.kind != b.kind || a.type != b.type || a.a != b.a)
        return false;
    return a.kind != IROP_FCONST || a.b == b.b;
}

static bool specialization_constant(IrOperand op)
{
    return op.kind == IROP_ICONST || op.kind == IROP_FCONST;
}

static void drop_call_arg(IrModule *m, u32 callee, u32 arg)
{
    u32 fi, bi;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next) {
                IrOperand *ops;

                if (in->op != IR_CALL || in->subop != FUNCREF_INTERNAL ||
                    in->callee != callee)
                    continue;
                ops = arena_alloc(m->arena, (in->nops - 1) * sizeof(*ops),
                                  _Alignof(IrOperand));
                if (arg)
                    memcpy(ops, in->ops, arg * sizeof(*ops));
                if (arg + 1 < in->nops)
                    memcpy(ops + arg, in->ops + arg + 1,
                           (in->nops - arg - 1) * sizeof(*ops));
                in->ops = ops;
                in->nops--;
            }
        }
}

static void drop_func_arg(IrModule *m, u32 fi, u32 arg)
{
    IrFunc *f = &m->funcs[fi];
    u8 *types = arena_alloc(m->arena, f->nparams - 1, 1);
    ValueId *vals = arena_alloc(m->arena, (f->nparams - 1) * sizeof(*vals),
                                _Alignof(ValueId));
    u64 *annots = NULL;

    if (f->param_annots)
        annots = arena_alloc(m->arena, (f->nparams - 1) * sizeof(*annots),
                             _Alignof(u64));
    if (arg) {
        memcpy(types, f->param_types, arg);
        memcpy(vals, f->param_vals, arg * sizeof(*vals));
        if (annots)
            memcpy(annots, f->param_annots, arg * sizeof(*annots));
    }
    if (arg + 1 < f->nparams) {
        memcpy(types + arg, f->param_types + arg + 1, f->nparams - arg - 1);
        memcpy(vals + arg, f->param_vals + arg + 1,
               (f->nparams - arg - 1) * sizeof(*vals));
        if (annots)
            memcpy(annots + arg, f->param_annots + arg + 1,
                   (f->nparams - arg - 1) * sizeof(*annots));
    }
    f->param_types = types;
    f->param_vals = vals;
    f->param_annots = annots;
    f->nparams--;
    drop_call_arg(m, fi, arg);
}

static u32 direct_call_count(const IrModule *m, u32 callee)
{
    u32 fi, bi, n = 0;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                n += in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
                     in->callee == callee;
        }
    return n;
}

static bool all_calls_constant(const IrModule *m, u32 callee, u32 arg,
                               IrOperand *constant)
{
    bool seen = false;
    u32 fi, bi;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next) {
                IrOperand op;

                if (in->op != IR_CALL || in->subop != FUNCREF_INTERNAL ||
                    in->callee != callee)
                    continue;
                if (arg >= in->nops)
                    CGF_ICE("ipo: malformed call during specialization");
                op = in->ops[arg];
                if (!specialization_constant(op))
                    return false;
                if (!seen) {
                    *constant = op;
                    seen = true;
                } else if (!const_same(*constant, op)) {
                    return false;
                }
            }
        }
    return seen;
}

static bool call_result_ignored(const IrModule *m, const IrFunc *caller,
                                const IrInst *call)
{
    (void)m;
    return !call->result.v || !func_uses_value(caller, call->result);
}

static bool all_call_results_ignored(const IrModule *m, u32 callee)
{
    bool seen = false;
    u32 fi, bi;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next) {
                if (in->op != IR_CALL || in->subop != FUNCREF_INTERNAL ||
                    in->callee != callee)
                    continue;
                seen = true;
                if (!call_result_ignored(m, &m->funcs[fi], in))
                    return false;
            }
        }
    return seen;
}

static void voidify(IrModule *m, u32 callee)
{
    IrFunc *f = &m->funcs[callee];
    u32 fi, bi;

    f->ret = IRT_VOID;
    f->abi_ret = IR_ABIRET_NONE;
    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in = f->blocks[bi].last;

        if (in && in->op == IR_RET) {
            in->ops = NULL;
            in->nops = 0;
        }
    }
    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if (in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
                    in->callee == callee) {
                    in->type = IRT_VOID;
                    in->result = VALUE_INVALID;
                }
        }
}

static bool eliminate_dead_functions(IrModule *m, const Callgraph *g,
                                     const OptConfig *cfg)
{
    bool *keep = zalloc(m->nfuncs, sizeof(bool));
    u32 *map = zalloc(m->nfuncs, sizeof(u32));
    bool changed = false;
    u32 i, out = 0;

    for (i = 0; i < m->nfuncs; i++) {
        keep[i] = true;
        if (!linkage_allows_ipo(&m->funcs[i], cfg))
            continue;
        if (g->address_taken[i]) {
            OPT_BAIL(cfg, "ipo", "ipo_addr_taken");
            continue;
        }
        if (direct_call_count(m, i) == 0) {
            keep[i] = false;
            changed = true;
        }
    }
    if (!changed) {
        free(keep);
        free(map);
        return false;
    }
    for (i = 0; i < m->nfuncs; i++)
        if (keep[i]) {
            map[i] = out;
            if (out != i)
                m->funcs[out] = m->funcs[i];
            out++;
        }
    m->nfuncs = out;
    for (i = 0; i < m->nfuncs; i++) {
        u32 bi;

        for (bi = 0; bi < m->funcs[i].nblocks; bi++) {
            IrInst *in;

            for (in = m->funcs[i].blocks[bi].first; in; in = in->next)
                if (in->op == IR_CALL && in->subop == FUNCREF_INTERNAL) {
                    if (!keep[in->callee])
                        CGF_ICE("ipo: deleted function still has a caller");
                    in->callee = map[in->callee];
                }
        }
    }
    free(keep);
    free(map);
    return true;
}

bool opt_ipo(IrModule *m, const OptConfig *cfg)
{
    Callgraph *g = ipo_callgraph_build(m);
    bool changed = false;
    u32 order;

    /* SCC order matters even though v0.1.0 specializes whole functions:
     * dropping callee arguments first lets callers expose newly-dead args. */
    for (order = 0; order < g->nsccs; order++) {
        u32 si = ipo_callgraph_bottom_up_scc(g, order);
        u32 mi;

        for (mi = 0; mi < ipo_callgraph_scc_size(g, si); mi++) {
            u32 fi = ipo_callgraph_scc_member(g, si, mi);
            IrFunc *f = &m->funcs[fi];
            i32 ai;

            if (!linkage_allows_ipo(f, cfg))
                continue;
            if (g->address_taken[fi]) {
                OPT_BAIL(cfg, "ipo", "ipo_addr_taken");
                continue;
            }
            if (f->variadic) {
                OPT_BAIL(cfg, "ipo", "ipo_variadic_signature");
                continue;
            }
            if (f->unprototyped) {
                OPT_BAIL(cfg, "ipo", "ipo_unprototyped_signature");
                continue;
            }
            if (func_reads_common(m, f))
                OPT_BAIL(cfg, "ipo", "ipo_common_symbol");
            if (g->unknown[fi])
                OPT_BAIL(cfg, "ipo", "ipo_indirect_callers");
            for (ai = (i32)f->nparams - 1; ai >= 0; ai--) {
                IrOperand constant = {0};

                if (ai == 0 && f->abi_ret != IR_ABIRET_NONE) {
                    OPT_BAIL(cfg, "ipo", "ipo_abi_return_param");
                    continue;
                }

                if (!func_uses_value(f, f->param_vals[ai])) {
                    drop_func_arg(m, fi, (u32)ai);
                    changed = true;
                } else if (all_calls_constant(m, fi, (u32)ai, &constant)) {
                    if (constant.kind == IROP_FCONST &&
                        value_has_annotated_use(f, f->param_vals[ai])) {
                        OPT_BAIL(cfg, "ipo", "ipo_fconst_abi_annotation");
                        continue;
                    }
                    replace_value(f, f->param_vals[ai], constant);
                    drop_func_arg(m, fi, (u32)ai);
                    changed = true;
                }
            }
            if (f->ret != IRT_VOID && f->abi_ret == IR_ABIRET_NONE &&
                all_call_results_ignored(m, fi)) {
                voidify(m, fi);
                changed = true;
            }
        }
    }
    ipo_callgraph_free(g);
    /* Dead functions are compacted last because their indices are call
     * operands and are also the callgraph's node identity. */
    g = ipo_callgraph_build(m);
    changed |= eliminate_dead_functions(m, g, cfg);
    ipo_callgraph_free(g);
    if (changed)
        for (order = 0; order < m->nfuncs; order++)
            ir_func_renumber(m->arena, &m->funcs[order]);
    return changed;
}

const Pass OPT_PASS_IPO = {"ipo", opt_ipo, PASS_PINNED_DELETE_FUNCS};
