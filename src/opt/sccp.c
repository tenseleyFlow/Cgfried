#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

typedef enum { LAT_TOP, LAT_CONST, LAT_OVERDEFINED } LatKind;

typedef struct {
    LatKind kind;
    IrOperand value;
} Lattice;

typedef struct {
    IrInst *term;
    u32 edge;
    u32 next_incoming; /* edge index + 1, zero = end */
} EdgeRef;

typedef struct {
    IrInst *inst;
    u32 block;
    u32 edge_base;
} InstRef;

typedef struct {
    u32 inst;
    u32 block;
    u32 edge; /* valid only for an edge-argument use */
    u32 next; /* use index + 1, zero = end */
    bool edge_arg;
} UseRef;

typedef struct {
    IrModule *m;
    IrFunc *f;
    const OptConfig *cfg;
    Lattice *values; /* ValueId-indexed, with slot zero unused. */
    bool *blocks;
    EdgeRef *edges;
    bool *edge_exec;
    u32 nedges;
    u32 *incoming_heads; /* BlockId-indexed by block-1. */
    InstRef *insts;
    u32 *block_first_inst;
    UseRef *uses;
    u32 *use_heads; /* ValueId-indexed, with slot zero unused. */
    u32 *edge_work;
    u32 edge_head;
    u32 edge_tail;
    u32 *block_work;
    u32 block_head;
    u32 block_tail;
    u32 *value_work;
    bool *value_queued;
    u32 value_head;
    u32 value_tail;
    IrOperand *fold_ops;
    bool logged_undef_branch;
} Sccp;

const Pass OPT_PASS_SCCP = {"sccp", opt_sccp, PASS_PINNED_EXACT};

static bool operand_eq(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static Lattice lattice_of(const Sccp *s, IrOperand op)
{
    Lattice l;

    memset(&l, 0, sizeof(l));
    if (op.kind == IROP_VALUE) {
        if (op.a >= 1 && op.a <= s->f->nvals)
            return s->values[op.a];
        l.kind = LAT_OVERDEFINED;
        return l;
    }
    if (op.kind == IROP_ICONST || op.kind == IROP_FCONST ||
        op.kind == IROP_SYMBOL || op.kind == IROP_UNDEF) {
        l.kind = LAT_CONST;
        l.value = op;
        return l;
    }
    l.kind = LAT_OVERDEFINED;
    return l;
}

static Lattice lattice_join(Lattice a, Lattice b)
{
    if (a.kind == LAT_TOP)
        return b;
    if (b.kind == LAT_TOP)
        return a;
    if (a.kind == LAT_OVERDEFINED || b.kind == LAT_OVERDEFINED) {
        a.kind = LAT_OVERDEFINED;
        return a;
    }
    if (!operand_eq(a.value, b.value))
        a.kind = LAT_OVERDEFINED;
    return a;
}

static void queue_value(Sccp *s, u32 value)
{
    if (!s->value_queued[value]) {
        s->value_queued[value] = true;
        s->value_work[s->value_tail++] = value;
    }
}

static bool update_value(Sccp *s, ValueId id, Lattice incoming)
{
    Lattice old, next;

    if (!id.v || id.v > s->f->nvals)
        return false;
    old = s->values[id.v];
    next = lattice_join(old, incoming);
    if (old.kind == next.kind &&
        (old.kind != LAT_CONST || operand_eq(old.value, next.value)))
        return false;
    s->values[id.v] = next;
    queue_value(s, id.v);
    return true;
}

static void mark_edge(Sccp *s, u32 number)
{
    if (number >= s->nedges)
        CGF_ICE("sccp: CFG edge is absent from the edge table");
    if (!s->edge_exec[number]) {
        s->edge_exec[number] = true;
        s->edge_work[s->edge_tail++] = number;
    }
}

static void mark_all_edges(Sccp *s, const IrInst *term, u32 edge_base)
{
    u32 i;

    for (i = 0; i < term->nedges; i++)
        mark_edge(s, edge_base + i);
}

static void note_undef_branch(Sccp *s)
{
    if (!s->logged_undef_branch) {
        OPT_BAIL(s->cfg, "sccp", "undef_branch");
        s->logged_undef_branch = true;
    }
}

static u64 int_mask(IrType type)
{
    static const u8 bits[] = {8, 16, 32, 64};

    if (type > IRT_I64)
        return ~(u64)0;
    return bits[type] == 64 ? ~(u64)0 : (((u64)1 << bits[type]) - 1);
}

static void eval_terminator(Sccp *s, IrInst *term, u32 edge_base)
{
    Lattice cond;
    u32 i;

    if (!term->nedges)
        return;
    if (term->op == IR_BR) {
        mark_edge(s, edge_base);
        return;
    }
    if ((term->op != IR_CONDBR && term->op != IR_SWITCH) || !term->nops) {
        mark_all_edges(s, term, edge_base);
        return;
    }
    cond = lattice_of(s, term->ops[0]);
    if (cond.kind == LAT_TOP)
        return;
    if (cond.kind == LAT_OVERDEFINED) {
        mark_all_edges(s, term, edge_base);
        return;
    }
    /* Undef has per-use freedom. Choosing one successor would turn that
     * freedom into a compiler choice, so every successor is executable. */
    if (cond.value.kind == IROP_UNDEF) {
        note_undef_branch(s);
        mark_all_edges(s, term, edge_base);
        return;
    }
    if (cond.value.kind != IROP_ICONST) {
        mark_all_edges(s, term, edge_base);
        return;
    }
    if (term->op == IR_CONDBR) {
        mark_edge(s, edge_base + (cond.value.a ? 0 : 1));
        return;
    }
    for (i = 1; i < term->nedges; i++)
        if ((cond.value.a & int_mask((IrType)cond.value.type)) ==
            ((u64)term->edges[i].case_val &
             int_mask((IrType)cond.value.type))) {
            mark_edge(s, edge_base + i);
            return;
        }
    mark_edge(s, edge_base);
}

static void eval_inst(Sccp *s, IrInst *in)
{
    IrInst temp;
    IrOperand folded;
    Lattice result;
    bool any_top = false;
    bool any_overdefined = false;
    u32 i;

    if (!in->result.v)
        return;
    temp = *in;
    temp.ops = s->fold_ops;
    for (i = 0; i < in->nops; i++) {
        Lattice op = lattice_of(s, in->ops[i]);

        s->fold_ops[i] = in->ops[i];
        if (op.kind == LAT_CONST)
            s->fold_ops[i] = op.value;
        else if (op.kind == LAT_TOP)
            any_top = true;
        else
            any_overdefined = true;
    }
    memset(&result, 0, sizeof(result));
    if (opt_fold_inst(&temp, &folded, s->cfg)) {
        /* The shared folder may return a surviving SSA operand (for
         * example a constant-select arm), not only a literal. Meet the
         * returned operand through the same lattice instead of falsely
         * declaring a ValueId itself to be a constant. */
        result = lattice_of(s, folded);
    } else if (any_overdefined || !any_top) {
        result.kind = LAT_OVERDEFINED;
    } else {
        result.kind = LAT_TOP;
    }
    update_value(s, in->result, result);
}

static void scan_block(Sccp *s, u32 block)
{
    IrInst *in;
    u32 inst = s->block_first_inst[block];

    for (in = s->f->blocks[block].first; in; in = in->next, inst++) {
        eval_inst(s, in);
        if (in->nedges)
            eval_terminator(s, in, s->insts[inst].edge_base);
    }
}

static Lattice block_param_value(const Sccp *s, BlockId target, u32 param)
{
    Lattice result;
    u32 cursor;

    memset(&result, 0, sizeof(result));
    cursor = s->incoming_heads[target.v - 1];
    while (cursor) {
        u32 i = cursor - 1;
        const EdgeRef *ref = &s->edges[i];
        const IrEdge *edge;

        cursor = ref->next_incoming;
        if (!s->edge_exec[i])
            continue;
        edge = &ref->term->edges[ref->edge];
        if (param >= edge->nargs)
            continue;
        result = lattice_join(result, lattice_of(s, edge->args[param]));
    }
    return result;
}

static void update_block_params(Sccp *s, BlockId target)
{
    IrBlock *block = ir_block(s->f, target);
    u32 i;

    if (!block)
        return;
    for (i = 0; i < block->nparams; i++)
        update_value(s, block->params[i], block_param_value(s, target, i));
}

static void process_edge(Sccp *s, u32 number)
{
    const EdgeRef *ref = &s->edges[number];
    BlockId target = ref->term->edges[ref->edge].target;

    if (!target.v || target.v > s->f->nblocks)
        return;
    update_block_params(s, target);
    if (!s->blocks[target.v - 1]) {
        s->blocks[target.v - 1] = true;
        s->block_work[s->block_tail++] = target.v - 1;
    }
}

static void process_value(Sccp *s, u32 value)
{
    u32 cursor = s->use_heads[value];

    while (cursor) {
        const UseRef *use = &s->uses[cursor - 1];

        cursor = use->next;
        if (use->edge_arg) {
            const EdgeRef *ref;

            if (!s->edge_exec[use->edge])
                continue;
            ref = &s->edges[use->edge];
            update_block_params(s, ref->term->edges[ref->edge].target);
        } else if (s->blocks[use->block]) {
            const InstRef *ref = &s->insts[use->inst];

            eval_inst(s, ref->inst);
            if (ref->inst->nedges)
                eval_terminator(s, ref->inst, ref->edge_base);
        }
    }
}

static void analyze(Sccp *s)
{
    u32 i;

    for (i = 0; i < s->f->nparams; i++) {
        Lattice over;

        memset(&over, 0, sizeof(over));
        over.kind = LAT_OVERDEFINED;
        update_value(s, s->f->param_vals[i], over);
    }
    if (s->f->nblocks) {
        s->blocks[0] = true;
        s->block_work[s->block_tail++] = 0;
    }
    while (s->edge_head < s->edge_tail || s->block_head < s->block_tail ||
           s->value_head < s->value_tail) {
        while (s->edge_head < s->edge_tail)
            process_edge(s, s->edge_work[s->edge_head++]);
        while (s->block_head < s->block_tail)
            scan_block(s, s->block_work[s->block_head++]);
        if (s->value_head < s->value_tail) {
            u32 value = s->value_work[s->value_head++];

            s->value_queued[value] = false;
            process_value(s, value);
        }
    }
}

static bool replace_operand(IrOperand *op, const Lattice *values, u32 nvals,
                            bool preserve_annot)
{
    IrOperand replacement;

    if (op->kind != IROP_VALUE || op->a < 1 || op->a > nvals ||
        values[op->a].kind != LAT_CONST)
        return false;
    replacement = values[op->a].value;
    if (preserve_annot)
        ir_arg_carry_provenance(&replacement, op);
    *op = replacement;
    return true;
}

static int constant_successor(const IrInst *term)
{
    u64 value, mask;
    u32 i;

    if ((term->op != IR_CONDBR && term->op != IR_SWITCH) || !term->nops ||
        term->ops[0].kind != IROP_ICONST)
        return -1;
    value = term->ops[0].a;
    if (term->op == IR_CONDBR)
        return value ? 0 : 1;
    mask = int_mask((IrType)term->ops[0].type);
    for (i = 1; i < term->nedges; i++)
        if ((value & mask) == ((u64)term->edges[i].case_val & mask))
            return (int)i;
    return 0;
}

static bool rewrite_func(Sccp *s)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < s->f->nblocks; bi++) {
        IrInst *in;

        for (in = s->f->blocks[bi].first; in; in = in->next) {
            u32 i, j;
            int chosen;

            for (i = 0; i < in->nops; i++)
                changed |= replace_operand(&in->ops[i], s->values, s->f->nvals,
                                           in->op == IR_CALL);
            for (i = 0; i < in->nedges; i++)
                for (j = 0; j < in->edges[i].nargs; j++)
                    changed |= replace_operand(&in->edges[i].args[j], s->values,
                                               s->f->nvals, false);
            chosen = constant_successor(in);
            if (chosen < 0)
                continue;
            if (chosen)
                in->edges[0] = in->edges[chosen];
            /* case_val has no meaning on br and the printer therefore omits
             * it.  Clear the copied switch payload so print/parse remains
             * structurally identical. */
            in->edges[0].case_val = 0;
            in->op = IR_BR;
            in->ops = NULL;
            in->nops = 0;
            in->nedges = 1;
            changed = true;
        }
    }
    if (changed) {
        /* The verifier rejects values and edges stranded in orphan blocks.
         * Prune immediately after CFG folding, before the pass manager's
         * verification boundary, then canonicalize value ids exactly once. */
        ir_func_remove_unreachable(s->f);
        ir_func_renumber(s->m->arena, s->f);
    }
    return changed;
}

static void add_use(Sccp *s, u32 *at, u32 value, u32 inst, u32 block, u32 edge,
                    bool edge_arg)
{
    UseRef *use;

    if (value < 1 || value > s->f->nvals)
        CGF_ICE("sccp: use references invalid value %u", value);
    use = &s->uses[(*at)++];
    use->inst = inst;
    use->block = block;
    use->edge = edge;
    use->edge_arg = edge_arg;
    use->next = s->use_heads[value];
    s->use_heads[value] = *at;
}

static bool sccp_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    OptConfig fc = *cfg;
    Sccp s;
    u32 bi, i, j;
    u32 nedges = 0, ninsts = 0, nuses = 0, max_ops = 0;
    u32 edge_at = 0, inst_at = 0, use_at = 0;
    bool changed;

    fc.current_func = f->name;
    arena_init(&scratch);
    memset(&s, 0, sizeof(s));
    s.m = m;
    s.f = f;
    s.cfg = &fc;
    for (bi = 0; bi < f->nblocks; bi++)
        for (IrInst *in = f->blocks[bi].first; in; in = in->next) {
            ninsts++;
            nedges += in->nedges;
            if (in->nops > max_ops)
                max_ops = in->nops;
            for (i = 0; i < in->nops; i++)
                if (in->ops[i].kind == IROP_VALUE)
                    nuses++;
            for (i = 0; i < in->nedges; i++)
                for (j = 0; j < in->edges[i].nargs; j++)
                    if (in->edges[i].args[j].kind == IROP_VALUE)
                        nuses++;
        }
    s.nedges = nedges;
    s.values = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*s.values),
                           _Alignof(Lattice));
    s.blocks =
        arena_alloc(&scratch, (f->nblocks ? f->nblocks : 1) * sizeof(*s.blocks),
                    _Alignof(bool));
    s.edges = arena_alloc(&scratch, (nedges ? nedges : 1) * sizeof(*s.edges),
                          _Alignof(EdgeRef));
    s.edge_exec = arena_alloc(
        &scratch, (nedges ? nedges : 1) * sizeof(*s.edge_exec), _Alignof(bool));
    s.incoming_heads = arena_alloc(
        &scratch, (f->nblocks ? f->nblocks : 1) * sizeof(*s.incoming_heads),
        _Alignof(u32));
    s.insts = arena_alloc(&scratch, (ninsts ? ninsts : 1) * sizeof(*s.insts),
                          _Alignof(InstRef));
    s.block_first_inst = arena_alloc(
        &scratch, (f->nblocks ? f->nblocks : 1) * sizeof(*s.block_first_inst),
        _Alignof(u32));
    s.uses = arena_alloc(&scratch, (nuses ? nuses : 1) * sizeof(*s.uses),
                         _Alignof(UseRef));
    s.use_heads = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*s.use_heads),
                              _Alignof(u32));
    s.edge_work = arena_alloc(
        &scratch, (nedges ? nedges : 1) * sizeof(*s.edge_work), _Alignof(u32));
    s.block_work = arena_alloc(
        &scratch, (f->nblocks ? f->nblocks : 1) * sizeof(*s.block_work),
        _Alignof(u32));
    s.value_work = arena_alloc(
        &scratch, (2 * f->nvals + 1) * sizeof(*s.value_work), _Alignof(u32));
    s.value_queued = arena_alloc(
        &scratch, (f->nvals + 1) * sizeof(*s.value_queued), _Alignof(bool));
    s.fold_ops =
        arena_alloc(&scratch, (max_ops ? max_ops : 1) * sizeof(*s.fold_ops),
                    _Alignof(IrOperand));
    memset(s.values, 0, (f->nvals + 1) * sizeof(*s.values));
    memset(s.blocks, 0, (f->nblocks ? f->nblocks : 1) * sizeof(*s.blocks));
    memset(s.edge_exec, 0, (nedges ? nedges : 1) * sizeof(*s.edge_exec));
    memset(s.incoming_heads, 0,
           (f->nblocks ? f->nblocks : 1) * sizeof(*s.incoming_heads));
    memset(s.use_heads, 0, (f->nvals + 1) * sizeof(*s.use_heads));
    memset(s.value_queued, 0, (f->nvals + 1) * sizeof(*s.value_queued));
    for (bi = 0; bi < f->nblocks; bi++) {
        s.block_first_inst[bi] = inst_at;
        for (IrInst *in = f->blocks[bi].first; in; in = in->next) {
            s.insts[inst_at].inst = in;
            s.insts[inst_at].block = bi;
            s.insts[inst_at].edge_base = edge_at;
            for (i = 0; i < in->nops; i++)
                if (in->ops[i].kind == IROP_VALUE)
                    add_use(&s, &use_at, (u32)in->ops[i].a, inst_at, bi, 0,
                            false);
            for (i = 0; i < in->nedges; i++) {
                IrEdge *edge = &in->edges[i];
                EdgeRef *ref = &s.edges[edge_at];

                ref->term = in;
                ref->edge = i;
                if (edge->target.v >= 1 && edge->target.v <= f->nblocks) {
                    ref->next_incoming = s.incoming_heads[edge->target.v - 1];
                    s.incoming_heads[edge->target.v - 1] = edge_at + 1;
                }
                for (j = 0; j < edge->nargs; j++)
                    if (edge->args[j].kind == IROP_VALUE)
                        add_use(&s, &use_at, (u32)edge->args[j].a, inst_at, bi,
                                edge_at, true);
                edge_at++;
            }
            inst_at++;
        }
    }
    if (edge_at != nedges || inst_at != ninsts || use_at != nuses)
        CGF_ICE("sccp: sparse-index construction mismatch");
    analyze(&s);
    changed = rewrite_func(&s);
    arena_free_all(&scratch);
    return changed;
}

bool opt_sccp(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= sccp_func(m, &m->funcs[i], cfg);
    return changed;
}
