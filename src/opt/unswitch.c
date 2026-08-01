#include "opt/opt.h"

#include <limits.h>
#include <string.h>

#include "util/arena.h"

typedef struct UnswitchPlan {
    BlockId preheader;
    BlockId header;
    BlockId branch_block;
    ValueId condition;
    ValueId *dag;
    u32 ndag;
    u32 loop_insts;
} UnswitchPlan;

const Pass OPT_PASS_UNSWITCH = {
    "unswitch",
    opt_unswitch,
    PASS_PINNED_METADATA_CLONES,
};

static u32 func_inst_count(const IrFunc *f)
{
    u32 bi, count = 0;

    for (bi = 0; bi < f->nblocks; bi++)
        count += f->blocks[bi].ninsts;
    return count;
}

static const IrInst *value_inst(const IrFunc *f, ValueId value)
{
    const IrValInfo *info;
    const IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    info = &f->vals[value.v - 1];
    if (info->def_kind != VDEF_INST || !info->def_block.v ||
        info->def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[info->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

static u32 integer_width(IrType type)
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

static u64 type_mask(IrType type)
{
    u32 width = integer_width(type);

    if (!width)
        return 0;
    return width == 64 ? UINT64_MAX : (1ull << width) - 1;
}

static bool speculatable_op(const IrInst *in)
{
    /* NSW makes overflow poison-like optimization provenance: evaluating it
     * on a newly introduced zero-trip path is not semantics-preserving unless
     * the arithmetic range is proved.  This pass has no such range proof. */
    if (in->flags & IRF_NSW)
        return false;
    switch (in->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
    case IR_ICMP:
    case IR_FCMP:
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
    case IR_FNEG:
    case IR_PTRADD:
    case IR_SELECT:
        if (in->op == IR_SHL || in->op == IR_LSHR || in->op == IR_ASHR)
            return in->nops == 2 && in->ops[1].kind == IROP_ICONST &&
                   in->ops[1].a < integer_width((IrType)in->type);
        return true;
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM:
        /* A constant nonzero divisor proves the ordinary trap absent.  For
         * signed division/remainder, INT_MIN / -1 is also a trap unless the
         * numerator itself proves that pair impossible. */
        if (in->nops != 2 || in->ops[1].kind != IROP_ICONST ||
            in->ops[1].a == 0)
            return false;
        if (in->op == IR_SDIV || in->op == IR_SREM) {
            u64 mask = type_mask((IrType)in->type);
            u32 width = integer_width((IrType)in->type);
            u64 minimum;

            if (!width)
                return false;
            minimum = 1ull << (width - 1);

            if ((in->ops[1].a & mask) == mask &&
                (in->ops[0].kind != IROP_ICONST ||
                 (in->ops[0].a & mask) == minimum))
                return false;
        }
        return true;
    default:
        return false;
    }
}

static bool plan_value(const IrFunc *f, const IrDomTree *dom, const Loop *loop,
                       BlockId preheader, ValueId value, u8 *state,
                       ValueId *order, u32 *norder)
{
    const IrValInfo *info;
    const IrInst *in;
    u32 oi;

    if (!value.v || value.v > f->nvals)
        return false;
    if (state[value.v] == 2)
        return true;
    if (state[value.v] == 1)
        return false;
    info = &f->vals[value.v - 1];
    if (info->def_kind == VDEF_FPARAM)
        return true;
    if (!info->def_block.v)
        return false;
    if (!loop_contains(loop, info->def_block))
        return ir_dominates(dom, info->def_block, preheader);
    if (info->def_kind != VDEF_INST)
        return false;
    in = value_inst(f, value);
    if (!in || !speculatable_op(in) || in->nedges || !in->result.v)
        return false;

    state[value.v] = 1;
    for (oi = 0; oi < in->nops; oi++) {
        IrOperand op = in->ops[oi];

        if (op.kind == IROP_UNDEF || op.kind == IROP_NONE)
            return false;
        if (op.kind == IROP_VALUE &&
            !plan_value(f, dom, loop, preheader, (ValueId){(u32)op.a}, state,
                        order, norder))
            return false;
    }
    state[value.v] = 2;
    order[(*norder)++] = value;
    return true;
}

static bool analyze_candidate(const IrFunc *f, const IrDomTree *dom,
                              const Loop *loop, const OptConfig *cfg,
                              Arena *scratch, UnswitchPlan *plan)
{
    TripInfo trip;
    const char *trip_reason;
    u8 *state;
    ValueId *order;
    u32 bi;

    memset(plan, 0, sizeof(*plan));
    if (!loop_preheader(loop).v || loop_latch_count(loop) != 1 ||
        !loop_trip_analyze(f, loop, cfg->fwrapv, &trip, &trip_reason))
        return false;
    (void)trip;
    plan->preheader = loop_preheader(loop);
    plan->header = loop_header(loop);
    state = arena_alloc(scratch, (size_t)(f->nvals + 1) * sizeof(*state),
                        _Alignof(u8));
    order = arena_alloc(scratch, (size_t)(f->nvals + 1) * sizeof(*order),
                        _Alignof(ValueId));

    for (bi = 0; bi < loop_block_count(loop); bi++) {
        BlockId block = loop_block(loop, bi);
        const IrInst *term = f->blocks[block.v - 1].last;
        ValueId condition;
        u32 li;

        plan->loop_insts += f->blocks[block.v - 1].ninsts;
        if (!term || term->op != IR_CONDBR || term->nops != 1 ||
            term->nedges != 2 || term->ops[0].kind != IROP_VALUE)
            continue;
        condition.v = (u32)term->ops[0].a;
        if (condition.v == trip.induction.compare.v)
            continue;
        memset(state, 0, (size_t)(f->nvals + 1) * sizeof(*state));
        plan->ndag = 0;
        if (!plan_value(f, dom, loop, plan->preheader, condition, state, order,
                        &plan->ndag))
            continue;
        /* A function parameter or already-dominating value needs no cloned
         * DAG.  An internal condition must finish the planned DAG itself. */
        if (loop_contains(loop, f->vals[condition.v - 1].def_block) &&
            (!plan->ndag || order[plan->ndag - 1].v != condition.v))
            continue;
        plan->branch_block = block;
        plan->condition = condition;
        plan->dag = order;
        for (li = bi + 1; li < loop_block_count(loop); li++)
            plan->loop_insts += f->blocks[loop_block(loop, li).v - 1].ninsts;
        return true;
    }
    return false;
}

static void *grow(Arena *arena, const void *old, u32 nold, u32 nnew,
                  size_t elem, size_t align)
{
    void *copy = arena_alloc(arena, (size_t)nnew * elem, align);

    if (nold)
        memcpy(copy, old, (size_t)nold * elem);
    return copy;
}

static ValueId add_value(IrModule *m, IrFunc *f, IrType type, BlockId block)
{
    ValueId value;

    if (f->nvals == f->cap_vals) {
        u32 cap = f->cap_vals ? f->cap_vals * 2 : 16;

        f->vals = grow(m->arena, f->vals, f->nvals, cap, sizeof(*f->vals),
                       _Alignof(IrValInfo));
        f->cap_vals = cap;
    }
    memset(&f->vals[f->nvals], 0, sizeof(f->vals[f->nvals]));
    f->vals[f->nvals].type = (u8)type;
    f->vals[f->nvals].def_kind = VDEF_INST;
    f->vals[f->nvals].def_block = block;
    value.v = ++f->nvals;
    return value;
}

static IrOperand map_operand(const IrOperand *map, u32 nmap, IrOperand op)
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

static IrOperand copy_condition_dag(IrModule *m, IrFunc *f,
                                    const UnswitchPlan *plan)
{
    u32 old_nvals = f->nvals;
    IrOperand *map = arena_alloc(
        m->arena, (size_t)(old_nvals + 1) * sizeof(*map), _Alignof(IrOperand));
    IrBlock *pre = &f->blocks[plan->preheader.v - 1];
    IrInst *term = pre->last;
    IrInst *prev = NULL;
    IrInst *in;
    u32 i;

    memset(map, 0, (size_t)(old_nvals + 1) * sizeof(*map));
    for (in = pre->first; in && in != term; in = in->next)
        prev = in;
    if (prev)
        prev->next = NULL;
    else
        pre->first = NULL;
    pre->last = prev;
    pre->ninsts--;

    for (i = 0; i < plan->ndag; i++) {
        const IrInst *old = value_inst(f, plan->dag[i]);
        IrInst *copy;
        u32 oi;

        if (!old)
            CGF_ICE("unswitch: planned condition value disappeared");
        copy = arena_alloc(m->arena, sizeof(*copy), _Alignof(IrInst));
        memcpy(copy, old, sizeof(*copy));
        copy->next = NULL;
        copy->edges = NULL;
        copy->nedges = 0;
        copy->ops =
            old->nops
                ? arena_alloc(m->arena, (size_t)old->nops * sizeof(*copy->ops),
                              _Alignof(IrOperand))
                : NULL;
        for (oi = 0; oi < old->nops; oi++)
            copy->ops[oi] = map_operand(map, old_nvals + 1, old->ops[oi]);
        copy->result = add_value(m, f, (IrType)old->type, plan->preheader);
        map[old->result.v] = ir_op_value(f, copy->result);
        append_inst(pre, copy);
    }
    append_inst(pre, term);
    return map_operand(map, old_nvals + 1, ir_op_value(f, plan->condition));
}

static IrOperand *copy_ops(IrModule *m, const IrOperand *ops, u32 n)
{
    IrOperand *copy;

    if (!n)
        return NULL;
    copy =
        arena_alloc(m->arena, (size_t)n * sizeof(*copy), _Alignof(IrOperand));
    memcpy(copy, ops, (size_t)n * sizeof(*copy));
    return copy;
}

static void make_path_constant(IrModule *m, IrInst *term, bool value)
{
    IrOperand *condition =
        arena_alloc(m->arena, sizeof(*condition), _Alignof(IrOperand));

    *condition = ir_op_iconst(IRT_I32, value ? 1 : 0);
    term->ops = condition;
    term->nops = 1;
}

static void commit_unswitch(IrModule *m, IrFunc *f, const Loop *loop,
                            const UnswitchPlan *plan)
{
    LoopCloneMap clone;
    const char *reason;
    BlockId *region =
        arena_alloc(m->arena, (size_t)loop_block_count(loop) * sizeof(*region),
                    _Alignof(BlockId));
    IrOperand hoisted;
    IrBlock *pre;
    IrInst *preterm;
    IrInst *original_term;
    IrInst *clone_term;
    IrEdge incoming;
    u32 i;

    for (i = 0; i < loop_block_count(loop); i++)
        region[i] = loop_block(loop, i);
    if (!loop_clone_region(m, f, region, loop_block_count(loop), plan->header,
                           LOOP_CLONE_PATH_EXCLUSIVE, "unswitch", &clone,
                           &reason))
        CGF_ICE("unswitch: analyzed region failed to clone (%s)",
                reason ? reason : "unknown");

    /* Static duplication of volatile/atomic instructions is legal here.
     * The pre-loop condition selects exactly one complete loop version, so
     * every dynamic execution observes exactly the original count and order.
     * Sharing or deleting either version's pinned operations would be the
     * actual miscompile. */
    hoisted = copy_condition_dag(m, f, plan);
    original_term = f->blocks[plan->branch_block.v - 1].last;
    clone_term =
        f->blocks[loop_clone_block(&clone, plan->branch_block).v - 1].last;
    make_path_constant(m, original_term, true);
    make_path_constant(m, clone_term, false);

    pre = &f->blocks[plan->preheader.v - 1];
    preterm = pre->last;
    incoming = preterm->edges[0];
    preterm->op = IR_CONDBR;
    preterm->nops = 1;
    preterm->ops =
        arena_alloc(m->arena, sizeof(*preterm->ops), _Alignof(IrOperand));
    preterm->ops[0] = hoisted;
    preterm->nedges = 2;
    preterm->edges =
        arena_alloc(m->arena, 2 * sizeof(*preterm->edges), _Alignof(IrEdge));
    memset(preterm->edges, 0, 2 * sizeof(*preterm->edges));
    preterm->edges[0] = incoming;
    preterm->edges[0].args = copy_ops(m, incoming.args, incoming.nargs);
    preterm->edges[1] = incoming;
    preterm->edges[1].target = clone.entry;
    preterm->edges[1].args = copy_ops(m, incoming.args, incoming.nargs);
    ir_func_renumber(m->arena, f);
}

static bool run_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;
    UnswitchPlan plan;
    u32 initial_insts = func_inst_count(f);
    u32 i, max_depth = 0;
    bool changed = false;

    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    if (loop_tree_irreducible(tree)) {
        arena_free_all(&scratch);
        return false;
    }
    if (loop_canonicalize(m, f, tree)) {
        changed = true;
        ir_func_renumber(m->arena, f);
        arena_free_all(&scratch);
        arena_init(&scratch);
        dom = ir_domtree_build(&scratch, f);
        tree = loop_tree_build(&scratch, f, dom);
        initial_insts = func_inst_count(f);
    }
    for (i = 0; i < loop_tree_count(tree); i++)
        if (loop_depth(loop_tree_at(tree, i)) > max_depth)
            max_depth = loop_depth(loop_tree_at(tree, i));
    for (;;) {
        bool saw_unspeculatable = false;

        for (i = 0; i < loop_tree_count(tree); i++) {
            const Loop *loop = loop_tree_at(tree, i);
            u64 projected;

            if (loop_depth(loop) != max_depth)
                continue;
            if (!analyze_candidate(f, dom, loop, cfg, &scratch, &plan)) {
                /* A loop containing a non-trip condbr but no safe candidate
                 * is useful bisection evidence, not a silent miss. */
                u32 bi;

                for (bi = 0; bi < loop_block_count(loop); bi++) {
                    const IrInst *term =
                        f->blocks[loop_block(loop, bi).v - 1].last;

                    if (term && term->op == IR_CONDBR &&
                        loop_block(loop, bi).v != loop_header(loop).v)
                        saw_unspeculatable = true;
                }
                continue;
            }
            projected = (u64)func_inst_count(f) + plan.loop_insts + plan.ndag;
            if (projected > (u64)initial_insts * 2) {
                OPT_BAIL(cfg, "unswitch", "unswitch_growth");
                arena_free_all(&scratch);
                return changed;
            }
            commit_unswitch(m, f, loop, &plan);
            arena_free_all(&scratch);
            return true;
        }
        if (saw_unspeculatable)
            OPT_BAIL(cfg, "unswitch", "unswitch_unspeculatable");
        if (!max_depth)
            break;
        max_depth--;
    }
    arena_free_all(&scratch);
    return changed;
}

bool opt_unswitch(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 fi;

    if (cfg->disable_unswitch ||
        (cfg->level != OPT_O3 && cfg->level != OPT_OFAST))
        return false;
    for (fi = 0; fi < m->nfuncs; fi++) {
        OptConfig fc = *cfg;

        if (opt_func_has_vector_ir(&m->funcs[fi]))
            continue;
        fc.current_func = m->funcs[fi].name;
        changed |= run_func(m, &m->funcs[fi], &fc);
    }
    return changed;
}
