#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

typedef enum {
    BAIL_NONE,
    BAIL_ADDR_TAKEN,
    BAIL_VOLATILE_ACCESS,
    BAIL_NONSCALAR,
    BAIL_MIXED_ACCESS_TYPE,
} BailReason;

typedef struct {
    IrInst *inst;
    ValueId ptr;
    BlockId block;
    u32 ord;
    IrType type;
    u64 size;
    BailReason bail;
    bool promotable;
} AllocaInfo;

typedef struct {
    u32 alloca_index;
    BlockId block;
    u32 loc;
    IrOperand value;
} PendingLoad;

struct OptMem2RegInfo {
    UndefUse *uses;
    u32 nuses;
    u32 cap_uses;
};

const Pass OPT_PASS_MEM2REG = {"mem2reg", opt_mem2reg, PASS_PINNED_EXACT};

const UndefUse *opt_mem2reg_undef_log(const IrFunc *f, u32 *n)
{
    const struct OptMem2RegInfo *info = f ? f->opt_mem2reg_info : NULL;

    if (n)
        *n = info ? info->nuses : 0;
    return info ? info->uses : NULL;
}

static const IrLocalSlot *slot_for(const IrFunc *f, ValueId addr)
{
    u32 i;

    for (i = 0; i < f->nlocal_slots; i++)
        if (f->local_slots[i].addr.v == addr.v)
            return &f->local_slots[i];
    return NULL;
}

static void log_undef(IrModule *m, IrFunc *f, const AllocaInfo *alloca,
                      BlockId block, u32 loc)
{
    struct OptMem2RegInfo *info = f->opt_mem2reg_info;
    const IrLocalSlot *slot = slot_for(f, alloca->ptr);

    if (!info) {
        info = arena_alloc(m->arena, sizeof(*info),
                           _Alignof(struct OptMem2RegInfo));
        memset(info, 0, sizeof(*info));
        f->opt_mem2reg_info = info;
    }
    if (info->nuses == info->cap_uses) {
        u32 nc = info->cap_uses ? info->cap_uses * 2 : 8;
        UndefUse *nu =
            arena_alloc(m->arena, nc * sizeof(*nu), _Alignof(UndefUse));

        if (info->nuses)
            memcpy(nu, info->uses, info->nuses * sizeof(*nu));
        info->uses = nu;
        info->cap_uses = nc;
    }
    info->uses[info->nuses].alloca_ord = alloca->ord;
    info->uses[info->nuses].block = block;
    info->uses[info->nuses].loc = ir_debug_loc(m, loc);
    info->uses[info->nuses].name = slot ? slot->name : NULL;
    if (slot)
        info->uses[info->nuses].decl_loc = slot->decl_span;
    else
        memset(&info->uses[info->nuses].decl_loc, 0, sizeof(Span));
    info->nuses++;
}

static bool operand_is_value(IrOperand op, ValueId v)
{
    return op.kind == IROP_VALUE && op.a == v.v;
}

static u64 scalar_size(IrType t)
{
    static const u8 sizes[] = {1, 2, 4, 8, 4, 8, 16, 16, 8, 0};

    return t < IRT_VOID ? sizes[t] : 0;
}

static void note_access(AllocaInfo *a, IrType t, bool *have_type, bool *mixed)
{
    if (!*have_type) {
        a->type = t;
        *have_type = true;
    } else if (a->type != t) {
        *mixed = true;
    }
}

static BailReason analyze_alloca(const IrFunc *f, AllocaInfo *a)
{
    bool addr_taken = false;
    bool volatile_access = false;
    bool have_type = false;
    bool mixed = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 oi, ei, ai;
            int direct_op = -1;

            if (in->op == IR_LOAD && in->nops >= 1 &&
                operand_is_value(in->ops[0], a->ptr)) {
                direct_op = 0;
                note_access(a, (IrType)in->type, &have_type, &mixed);
                if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                    volatile_access = true;
            } else if (in->op == IR_STORE && in->nops >= 2 &&
                       operand_is_value(in->ops[1], a->ptr)) {
                direct_op = 1;
                note_access(a, (IrType)in->ops[0].type, &have_type, &mixed);
                if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                    volatile_access = true;
            } else if ((in->op == IR_ATOMICRMW || in->op == IR_CMPXCHG) &&
                       in->nops >= 1 && operand_is_value(in->ops[0], a->ptr)) {
                /* Atomic memory traffic is pinned by the same named
                 * non-ordinary-access bailout as volatile load/store. */
                direct_op = 0;
                volatile_access = true;
            }
            for (oi = 0; oi < in->nops; oi++)
                if ((int)oi != direct_op &&
                    operand_is_value(in->ops[oi], a->ptr))
                    addr_taken = true;
            for (ei = 0; ei < in->nedges; ei++)
                for (ai = 0; ai < in->edges[ei].nargs; ai++)
                    if (operand_is_value(in->edges[ei].args[ai], a->ptr))
                        addr_taken = true;
        }
    }
    if (addr_taken)
        return BAIL_ADDR_TAKEN;
    if (volatile_access)
        return BAIL_VOLATILE_ACCESS;
    if (mixed)
        return BAIL_MIXED_ACCESS_TYPE;
    if (a->block.v != 1 || a->inst->nops != 1 ||
        a->inst->ops[0].kind != IROP_ICONST || !have_type ||
        a->inst->ops[0].a == 0 || scalar_size(a->type) != a->inst->ops[0].a)
        return BAIL_NONSCALAR;
    a->size = a->inst->ops[0].a;
    return BAIL_NONE;
}

static void emit_bail(const OptConfig *cfg, BailReason reason)
{
    switch (reason) {
    case BAIL_ADDR_TAKEN:
        OPT_BAIL(cfg, "mem2reg", "addr_taken");
        break;
    case BAIL_VOLATILE_ACCESS:
        OPT_BAIL(cfg, "mem2reg", "volatile_access");
        break;
    case BAIL_NONSCALAR:
        OPT_BAIL(cfg, "mem2reg", "nonscalar");
        break;
    case BAIL_MIXED_ACCESS_TYPE:
        OPT_BAIL(cfg, "mem2reg", "mixed_access_type");
        break;
    case BAIL_NONE:
        break;
    }
}

static int alloca_for_ptr(const AllocaInfo *allocas, u32 nallocas, IrOperand op)
{
    u32 i;

    if (op.kind != IROP_VALUE)
        return -1;
    for (i = 0; i < nallocas; i++)
        if (allocas[i].promotable && allocas[i].ptr.v == op.a)
            return (int)i;
    return -1;
}

static IrOperand resolve_operand(IrOperand op, const IrOperand *replacement,
                                 u32 nold)
{
    u32 steps = 0;

    while (op.kind == IROP_VALUE && op.a >= 1 && op.a <= nold &&
           replacement[op.a].kind != IROP_NONE) {
        op = replacement[op.a];
        if (++steps > nold)
            CGF_ICE("mem2reg: cyclic value replacement");
    }
    return op;
}

static IrOperand resolve_inst_operand(IrOperand op,
                                      const IrOperand *replacement, u32 nold,
                                      bool preserve_annot)
{
    u64 annot = op.b;

    op = resolve_operand(op, replacement, nold);
    if (preserve_annot)
        op.b = annot;
    return op;
}

static bool operand_may_undef(IrOperand op, const bool *may_undef, u32 nvals)
{
    if (op.kind == IROP_UNDEF)
        return true;
    return op.kind == IROP_VALUE && op.a >= 1 && op.a <= nvals &&
           may_undef[op.a];
}

static void append_phi_args(IrModule *m, IrEdge *edge,
                            const AllocaInfo *allocas, u32 nallocas,
                            const bool *phis, u32 nblocks,
                            const IrOperand *current)
{
    u32 target = edge->target.v - 1;
    u32 add = 0, i, at;
    IrOperand *args;

    for (i = 0; i < nallocas; i++)
        if (allocas[i].promotable && phis[i * nblocks + target])
            add++;
    if (!add)
        return;
    args = arena_alloc(m->arena, (edge->nargs + add) * sizeof(*args),
                       _Alignof(IrOperand));
    if (edge->nargs)
        memcpy(args, edge->args, edge->nargs * sizeof(*args));
    at = edge->nargs;
    for (i = 0; i < nallocas; i++) {
        IrOperand value;

        if (!allocas[i].promotable || !phis[i * nblocks + target])
            continue;
        value = current[i];
        if (value.kind == IROP_NONE)
            value = ir_op_undef(allocas[i].type);
        args[at++] = value;
    }
    edge->args = args;
    edge->nargs += add;
}

static bool promote_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    OptConfig fc = *cfg;
    AllocaInfo *allocas;
    PendingLoad *pending;
    u32 nallocas = 0, nloads = 0, npending = 0, ai = 0, bi, i, j;
    u32 nblocks = f->nblocks;
    bool *pred, *df, *defs, *phis, *use_before_def, *live_in;
    u32 *pred_count, *idom, *order, *stack, *work;
    ValueId *phi_vals;
    IrOperand *replacement, *out_values;
    IrDomTree *dom;
    u32 old_nvals;
    bool changed = false;

    fc.current_func = f->name;
    if (f->calls_setjmp) {
        OPT_BAIL(&fc, "mem2reg", "setjmp_caller");
        return false;
    }
    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            if (in->op == IR_ALLOCA)
                nallocas++;
            else if (in->op == IR_LOAD)
                nloads++;
        }
    }
    if (!nallocas || !nblocks)
        return false;
    arena_init(&scratch);
    allocas = arena_alloc(&scratch, nallocas * sizeof(*allocas),
                          _Alignof(AllocaInfo));
    memset(allocas, 0, nallocas * sizeof(*allocas));
    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            if (in->op != IR_ALLOCA)
                continue;
            allocas[ai].inst = in;
            allocas[ai].ptr = in->result;
            allocas[ai].block.v = bi + 1;
            allocas[ai].ord = ai;
            ai++;
        }
    }
    for (i = 0; i < nallocas; i++) {
        allocas[i].bail = analyze_alloca(f, &allocas[i]);
        allocas[i].promotable = allocas[i].bail == BAIL_NONE;
        if (allocas[i].promotable)
            changed = true;
        else
            emit_bail(&fc, allocas[i].bail);
    }
    if (!changed) {
        arena_free_all(&scratch);
        return false;
    }

    if ((size_t)nblocks > SIZE_MAX / (size_t)nblocks ||
        (size_t)nallocas > SIZE_MAX / (size_t)nblocks)
        CGF_ICE("mem2reg: CFG too large");
    pred = arena_alloc(&scratch, (size_t)nblocks * nblocks, 1);
    df = arena_alloc(&scratch, (size_t)nblocks * nblocks, 1);
    defs = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    phis = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    use_before_def = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    live_in = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    phi_vals =
        arena_alloc(&scratch, (size_t)nallocas * nblocks * sizeof(*phi_vals),
                    _Alignof(ValueId));
    pred_count =
        arena_alloc(&scratch, nblocks * sizeof(*pred_count), _Alignof(u32));
    idom = arena_alloc(&scratch, nblocks * sizeof(*idom), _Alignof(u32));
    order = arena_alloc(&scratch, nblocks * sizeof(*order), _Alignof(u32));
    stack = arena_alloc(&scratch, nblocks * sizeof(*stack), _Alignof(u32));
    work = arena_alloc(&scratch, nblocks * sizeof(*work), _Alignof(u32));
    pending = arena_alloc(&scratch, (nloads ? nloads : 1) * sizeof(*pending),
                          _Alignof(PendingLoad));
    memset(pred, 0, (size_t)nblocks * nblocks);
    memset(df, 0, (size_t)nblocks * nblocks);
    memset(defs, 0, (size_t)nallocas * nblocks);
    memset(phis, 0, (size_t)nallocas * nblocks);
    memset(use_before_def, 0, (size_t)nallocas * nblocks);
    memset(live_in, 0, (size_t)nallocas * nblocks);
    memset(phi_vals, 0, (size_t)nallocas * nblocks * sizeof(*phi_vals));
    memset(pred_count, 0, nblocks * sizeof(*pred_count));

    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 ei;

            if (in->op == IR_STORE && in->nops >= 2) {
                int aidx = alloca_for_ptr(allocas, nallocas, in->ops[1]);

                if (aidx >= 0)
                    defs[(u32)aidx * nblocks + bi] = true;
            }
            for (ei = 0; ei < in->nedges; ei++) {
                u32 to = in->edges[ei].target.v - 1;

                if (!pred[bi * nblocks + to]) {
                    pred[bi * nblocks + to] = true;
                    pred_count[to]++;
                }
            }
        }
    }

    /* Pruned SSA placement: a frontier gets a parameter only if the slot is
     * live on entry. Loads before the first local definition generate use;
     * any definition kills successor liveness. */
    for (ai = 0; ai < nallocas; ai++) {
        if (!allocas[ai].promotable)
            continue;
        for (bi = 0; bi < nblocks; bi++) {
            IrInst *in;
            bool seen_def = false;

            for (in = f->blocks[bi].first; in; in = in->next) {
                if (in->op == IR_STORE && in->nops >= 2 &&
                    operand_is_value(in->ops[1], allocas[ai].ptr))
                    seen_def = true;
                else if (in->op == IR_LOAD && in->nops >= 1 && !seen_def &&
                         operand_is_value(in->ops[0], allocas[ai].ptr))
                    use_before_def[(size_t)ai * nblocks + bi] = true;
            }
        }
    }
    {
        bool moved;

        do {
            moved = false;
            for (ai = 0; ai < nallocas; ai++) {
                if (!allocas[ai].promotable)
                    continue;
                for (bi = nblocks; bi-- > 0;) {
                    size_t at = (size_t)ai * nblocks + bi;
                    bool out = false;
                    bool next;
                    u32 succ;

                    for (succ = 0; succ < nblocks; succ++)
                        if (pred[bi * nblocks + succ] &&
                            live_in[(size_t)ai * nblocks + succ]) {
                            out = true;
                            break;
                        }
                    next = use_before_def[at] || (!defs[at] && out);
                    if (next != live_in[at]) {
                        live_in[at] = next;
                        moved = true;
                    }
                }
            }
        } while (moved);
    }

    dom = ir_domtree_build(&scratch, f);
    idom[0] = UINT32_MAX;
    for (bi = 1; bi < nblocks; bi++) {
        BlockId b = {(u32)(bi + 1)};
        BlockId p = ir_idom(dom, b);

        idom[bi] = p.v ? p.v - 1 : UINT32_MAX;
    }
    for (bi = 0; bi < nblocks; bi++) {
        u32 stop;

        if (pred_count[bi] < 2)
            continue;
        stop = idom[bi];
        for (i = 0; i < nblocks; i++) {
            u32 runner;

            if (!pred[i * nblocks + bi])
                continue;
            runner = i;
            while (runner != stop && runner != UINT32_MAX) {
                df[runner * nblocks + bi] = true;
                runner = idom[runner];
            }
        }
    }

    old_nvals = f->nvals;
    for (ai = 0; ai < nallocas; ai++) {
        bool *queued;
        u32 head = 0, tail = 0;

        if (!allocas[ai].promotable)
            continue;
        queued = arena_alloc(&scratch, nblocks, 1);
        memset(queued, 0, nblocks);
        for (bi = 0; bi < nblocks; bi++)
            if (defs[ai * nblocks + bi]) {
                work[tail++] = bi;
                queued[bi] = true;
            }
        while (head < tail) {
            u32 x = work[head++];

            for (bi = 0; bi < nblocks; bi++) {
                size_t at = (size_t)ai * nblocks + bi;

                if (!df[x * nblocks + bi] || !live_in[at] || phis[at])
                    continue;
                phis[at] = true;
                phi_vals[at] =
                    ir_block_param(m, f, (BlockId){bi + 1}, allocas[ai].type);
                if (!defs[at] && !queued[bi]) {
                    work[tail++] = bi;
                    queued[bi] = true;
                }
            }
        }
    }

    replacement = arena_alloc(&scratch, (old_nvals + 1) * sizeof(*replacement),
                              _Alignof(IrOperand));
    out_values =
        arena_alloc(&scratch, (size_t)nblocks * nallocas * sizeof(*out_values),
                    _Alignof(IrOperand));
    memset(replacement, 0, (old_nvals + 1) * sizeof(*replacement));
    memset(out_values, 0, (size_t)nblocks * nallocas * sizeof(*out_values));

    /* Deterministic iterative preorder: push children in reverse block order.
     */
    {
        u32 sp = 0, norder = 0;

        stack[sp++] = 0;
        while (sp) {
            u32 b = stack[--sp];

            order[norder++] = b;
            for (i = nblocks; i-- > 1;)
                if (idom[i] == b)
                    stack[sp++] = i;
        }
        if (norder != nblocks)
            CGF_ICE("mem2reg: dominator preorder missed a block");
    }

    for (i = 0; i < nblocks; i++) {
        u32 bidx = order[i];
        IrOperand *current = &out_values[(size_t)bidx * nallocas];
        IrBlock *blk = &f->blocks[bidx];
        IrInst *in = blk->first;
        IrInst *prev = NULL;

        if (idom[bidx] != UINT32_MAX)
            memcpy(current, &out_values[(size_t)idom[bidx] * nallocas],
                   nallocas * sizeof(*current));
        for (ai = 0; ai < nallocas; ai++)
            if (allocas[ai].promotable && phis[ai * nblocks + bidx])
                current[ai] =
                    ir_op_value(f, phi_vals[(size_t)ai * nblocks + bidx]);

        while (in) {
            IrInst *next = in->next;
            bool remove = false;
            int aidx = -1;
            u32 oi, ei, xi;

            for (oi = 0; oi < in->nops; oi++)
                in->ops[oi] = resolve_inst_operand(
                    in->ops[oi], replacement, old_nvals,
                    in->op == IR_CALL && in->ops[oi].b != 0);
            for (ei = 0; ei < in->nedges; ei++)
                for (xi = 0; xi < in->edges[ei].nargs; xi++)
                    in->edges[ei].args[xi] = resolve_operand(
                        in->edges[ei].args[xi], replacement, old_nvals);

            if (in->op == IR_ALLOCA) {
                for (ai = 0; ai < nallocas; ai++)
                    if (allocas[ai].promotable && allocas[ai].inst == in) {
                        remove = true;
                        break;
                    }
            } else if (in->op == IR_LOAD && in->nops >= 1) {
                aidx = alloca_for_ptr(allocas, nallocas, in->ops[0]);
                if (aidx >= 0) {
                    IrOperand value = current[aidx];

                    if (value.kind == IROP_NONE) {
                        value = ir_op_undef(allocas[aidx].type);
                    }
                    replacement[in->result.v] = value;
                    pending[npending].alloca_index = (u32)aidx;
                    pending[npending].block.v = bidx + 1;
                    pending[npending].loc = in->loc;
                    pending[npending].value = value;
                    npending++;
                    remove = true;
                }
            } else if (in->op == IR_STORE && in->nops >= 2) {
                aidx = alloca_for_ptr(allocas, nallocas, in->ops[1]);
                if (aidx >= 0) {
                    current[aidx] =
                        resolve_operand(in->ops[0], replacement, old_nvals);
                    remove = true;
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    blk->first = next;
                if (blk->last == in)
                    blk->last = prev;
                blk->ninsts--;
            } else {
                prev = in;
            }
            in = next;
        }
        for (in = blk->first; in; in = in->next)
            for (j = 0; j < in->nedges; j++)
                append_phi_args(m, &in->edges[j], allocas, nallocas, phis,
                                nblocks, current);
    }

    /* Loads can feed uses in blocks visited later; one final transitive
     * rewrite covers every surviving operand before canonical renumbering. */
    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 ei, xi;

            for (i = 0; i < in->nops; i++)
                in->ops[i] = resolve_inst_operand(
                    in->ops[i], replacement, old_nvals,
                    in->op == IR_CALL && in->ops[i].b != 0);
            for (ei = 0; ei < in->nedges; ei++)
                for (xi = 0; xi < in->edges[ei].nargs; xi++)
                    in->edges[ei].args[xi] = resolve_operand(
                        in->edges[ei].args[xi], replacement, old_nvals);
        }
    }

    /* Propagate undef provenance through block parameters and ordinary SSA
     * results. Logging the removed LOAD (the consumer), rather than its seed
     * edge, preserves per-read freedom and gives Sprint 40 the useful site. */
    {
        u32 nvals = f->nvals;
        bool *may_undef = arena_alloc(&scratch, (nvals + 1) * sizeof(bool), 1);
        bool moved;

        memset(may_undef, 0, (nvals + 1) * sizeof(bool));
        do {
            moved = false;
            for (bi = 0; bi < nblocks; bi++) {
                IrBlock *blk = &f->blocks[bi];
                u32 pi;

                for (pi = 0; pi < blk->nparams; pi++) {
                    ValueId param = blk->params[pi];
                    u32 from;

                    if (may_undef[param.v])
                        continue;
                    for (from = 0; from < nblocks && !may_undef[param.v];
                         from++) {
                        IrInst *term;

                        for (term = f->blocks[from].first; term;
                             term = term->next) {
                            u32 ei;

                            for (ei = 0; ei < term->nedges; ei++) {
                                IrEdge *edge = &term->edges[ei];

                                if (edge->target.v == bi + 1 &&
                                    pi < edge->nargs &&
                                    operand_may_undef(edge->args[pi], may_undef,
                                                      nvals)) {
                                    may_undef[param.v] = true;
                                    moved = true;
                                    break;
                                }
                            }
                            if (may_undef[param.v])
                                break;
                        }
                    }
                }
                {
                    IrInst *in;

                    for (in = blk->first; in; in = in->next) {
                        u32 oi;

                        if (!in->result.v || may_undef[in->result.v])
                            continue;
                        for (oi = 0; oi < in->nops; oi++)
                            if (operand_may_undef(in->ops[oi], may_undef,
                                                  nvals)) {
                                may_undef[in->result.v] = true;
                                moved = true;
                                break;
                            }
                    }
                }
            }
        } while (moved);
        for (i = 0; i < npending; i++) {
            IrOperand value =
                resolve_operand(pending[i].value, replacement, old_nvals);

            if (operand_may_undef(value, may_undef, nvals))
                log_undef(m, f, &allocas[pending[i].alloca_index],
                          pending[i].block, pending[i].loc);
        }
    }
    ir_func_renumber(m->arena, f);
    arena_free_all(&scratch);
    return true;
}

bool opt_mem2reg(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= promote_func(m, &m->funcs[i], cfg);
    return changed;
}
