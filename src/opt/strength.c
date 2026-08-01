#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

const Pass OPT_PASS_STRENGTH = {"strength", opt_strength, PASS_PINNED_EXACT};

typedef struct InsertPoint {
    IrBlock *block;
    IrInst *term;
    IrInst *before;
} InsertPoint;

typedef struct IvMatch {
    const Loop *loop;
    BlockId header;
    BlockId preheader;
    BlockId latch;
    u32 param_index;
    ValueId iv;
    IrOperand start;
    IrOperand step;
    bool subtract_step;
    bool signed_no_wrap;
} IvMatch;

typedef struct MulMatch {
    BlockId block;
    IrInst *inst;
    IrOperand scale;
} MulMatch;

static InsertPoint open_before_terminator(IrFunc *f, BlockId bid)
{
    InsertPoint p;
    IrInst *in;

    memset(&p, 0, sizeof(p));
    p.block = ir_block(f, bid);
    if (!p.block || !p.block->last || p.block->last->op < IR_RET ||
        p.block->last->op > IR_UNREACHABLE)
        CGF_ICE("strength: block has no terminator");
    p.term = p.block->last;
    for (in = p.block->first; in && in->next != p.term; in = in->next)
        ;
    p.before = in;
    if (p.before) {
        p.before->next = NULL;
        p.block->last = p.before;
    } else {
        p.block->first = NULL;
        p.block->last = NULL;
    }
    p.block->ninsts--;
    return p;
}

static void close_before_terminator(InsertPoint *p)
{
    if (p->block->last)
        p->block->last->next = p->term;
    else
        p->block->first = p->term;
    p->block->last = p->term;
    p->term->next = NULL;
    p->block->ninsts++;
}

static IrInst *value_inst(IrFunc *f, ValueId value)
{
    IrValInfo *info;
    IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    info = &f->vals[value.v - 1];
    if (info->def_kind != VDEF_INST || !info->def_block.v)
        return NULL;
    for (in = ir_block(f, info->def_block)->first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

static bool operand_is_value(IrOperand op, ValueId value)
{
    return op.kind == IROP_VALUE && op.a == value.v;
}

static bool operand_invariant(const IrFunc *f, const Loop *loop, IrOperand op)
{
    const IrValInfo *info;

    if (op.kind == IROP_ICONST || op.kind == IROP_SYMBOL)
        return true;
    /* Undef is deliberately per-read.  Capturing it in an accumulator would
     * turn multiple independent choices into one choice. */
    if (op.kind != IROP_VALUE || op.a == 0 || op.a > f->nvals)
        return false;
    info = &f->vals[op.a - 1];
    return !info->def_block.v || !loop_contains(loop, info->def_block);
}

static IrEdge *find_edge(IrFunc *f, BlockId from, BlockId to)
{
    IrInst *term = ir_block(f, from)->last;
    IrEdge *found = NULL;
    u32 ei;

    if (!term)
        return NULL;
    for (ei = 0; ei < term->nedges; ei++) {
        if (term->edges[ei].target.v != to.v)
            continue;
        if (found)
            return NULL;
        found = &term->edges[ei];
    }
    return found;
}

static bool match_iv(IrFunc *f, const Loop *loop, u32 pi, IvMatch *out)
{
    BlockId header = loop_header(loop);
    BlockId preheader = loop_preheader(loop);
    BlockId latch;
    IrBlock *head = ir_block(f, header);
    IrEdge *preedge, *backedge;
    IrInst *update;
    ValueId iv;
    IrOperand back, step;
    bool subtract_step;

    if (!preheader.v || loop_latch_count(loop) != 1 || pi >= head->nparams)
        return false;
    latch = loop_latch(loop, 0);
    preedge = find_edge(f, preheader, header);
    backedge = find_edge(f, latch, header);
    if (!preedge || !backedge || pi >= preedge->nargs || pi >= backedge->nargs)
        return false;
    iv = head->params[pi];
    back = backedge->args[pi];
    if (back.kind != IROP_VALUE)
        return false;
    update = value_inst(f, (ValueId){(u32)back.a});
    if (!update || f->vals[back.a - 1].def_block.v != latch.v ||
        update->nops != 2 || update->type != ir_value_type(f, iv))
        return false;
    if (update->op == IR_IADD) {
        if (operand_is_value(update->ops[0], iv) &&
            operand_invariant(f, loop, update->ops[1])) {
            step = update->ops[1];
        } else if (operand_is_value(update->ops[1], iv) &&
                   operand_invariant(f, loop, update->ops[0])) {
            step = update->ops[0];
        } else {
            return false;
        }
        subtract_step = false;
    } else if (update->op == IR_ISUB && operand_is_value(update->ops[0], iv) &&
               operand_invariant(f, loop, update->ops[1])) {
        step = update->ops[1];
        subtract_step = true;
    } else {
        return false;
    }
    if (!operand_invariant(f, loop, preedge->args[pi]))
        return false;
    memset(out, 0, sizeof(*out));
    out->loop = loop;
    out->header = header;
    out->preheader = preheader;
    out->latch = latch;
    out->param_index = pi;
    out->iv = iv;
    out->start = preedge->args[pi];
    out->subtract_step = subtract_step;
    out->step = step;
    out->signed_no_wrap = (update->flags & IRF_NSW) != 0;
    return true;
}
static bool find_mul(IrFunc *f, const IvMatch *iv, MulMatch *out)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        BlockId bid = {bi + 1};
        IrInst *in;

        if (!loop_contains(iv->loop, bid))
            continue;
        for (in = f->blocks[bi].first; in; in = in->next) {
            IrOperand scale;

            if (in->op != IR_IMUL || in->type != ir_value_type(f, iv->iv) ||
                in->nops != 2)
                continue;
            if (operand_is_value(in->ops[0], iv->iv))
                scale = in->ops[1];
            else if (operand_is_value(in->ops[1], iv->iv))
                scale = in->ops[0];
            else
                continue;
            if (!operand_invariant(f, iv->loop, scale))
                continue;
            out->block = bid;
            out->inst = in;
            out->scale = scale;
            return true;
        }
    }
    return false;
}

static void append_edge_arg(IrModule *m, IrEdge *edge, IrOperand arg)
{
    IrOperand *args = arena_alloc(m->arena, (edge->nargs + 1) * sizeof(*args),
                                  _Alignof(IrOperand));

    if (edge->nargs)
        memcpy(args, edge->args, edge->nargs * sizeof(*args));
    args[edge->nargs] = arg;
    edge->args = args;
    edge->nargs++;
}

static IrOperand build_mul(IrBuilder *b, IrType type, IrOperand x, IrOperand y)
{
    if (x.kind == IROP_ICONST && y.kind == IROP_ICONST) {
        u64 bits = type == IRT_I8    ? 8
                   : type == IRT_I16 ? 16
                   : type == IRT_I32 ? 32
                                     : 64;
        u64 mask = bits == 64 ? UINT64_MAX : ((1ull << bits) - 1);

        return ir_op_iconst(type, (i64)((x.a * y.a) & mask));
    }
    return ir_op_value(b->f, ir_build2(b, IR_IMUL, type, x, y));
}

static bool remove_inst(IrFunc *f, BlockId bid, IrInst *victim)
{
    IrBlock *block = ir_block(f, bid);
    IrInst *prev = NULL, *in;

    for (in = block->first; in && in != victim; in = in->next)
        prev = in;
    if (!in)
        return false;
    if (prev)
        prev->next = in->next;
    else
        block->first = in->next;
    if (block->last == in)
        block->last = prev;
    block->ninsts--;
    return true;
}

static void replace_value(IrFunc *f, ValueId old, ValueId replacement)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 oi, ei, ai;

            for (oi = 0; oi < in->nops; oi++)
                if (operand_is_value(in->ops[oi], old))
                    in->ops[oi] = ir_op_value(f, replacement);
            for (ei = 0; ei < in->nedges; ei++)
                for (ai = 0; ai < in->edges[ei].nargs; ai++)
                    if (operand_is_value(in->edges[ei].args[ai], old))
                        in->edges[ei].args[ai] = ir_op_value(f, replacement);
        }
    }
}

static bool commit_reduction(IrModule *m, IrFunc *f, const IvMatch *iv,
                             const MulMatch *mul)
{
    IrType type = ir_value_type(f, iv->iv);
    IrEdge *preedge = find_edge(f, iv->preheader, iv->header);
    IrEdge *backedge = find_edge(f, iv->latch, iv->header);
    InsertPoint preip, latchip;
    IrBuilder b;
    IrOperand start, delta, next;
    ValueId accum;

    if (!preedge || !backedge)
        return false;

    preip = open_before_terminator(f, iv->preheader);
    ir_builder_at(&b, m, f, iv->preheader);
    start = build_mul(&b, type, iv->start, mul->scale);
    delta = iv->step;
    if (iv->subtract_step) {
        ValueId neg =
            ir_build2(&b, IR_ISUB, type, ir_op_iconst(type, 0), delta);

        delta = ir_op_value(f, neg);
    }
    delta = build_mul(&b, type, delta, mul->scale);
    close_before_terminator(&preip);

    accum = ir_block_param(m, f, iv->header, type);
    append_edge_arg(m, preedge, start);

    latchip = open_before_terminator(f, iv->latch);
    ir_builder_at(&b, m, f, iv->latch);
    next = ir_op_value(
        f, ir_build2(&b, IR_IADD, type, ir_op_value(f, accum), delta));
    close_before_terminator(&latchip);
    append_edge_arg(m, backedge, next);

    replace_value(f, mul->inst->result, accum);
    if (!remove_inst(f, mul->block, mul->inst))
        CGF_ICE("strength: matched multiply disappeared");
    ir_func_renumber(m->arena, f);
    return true;
}

static bool has_wrap_sensitive_shape(const IrFunc *f, const Loop *loop)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        if (!loop_contains(loop, (BlockId){bi + 1}))
            continue;
        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == IR_ZEXT || in->op == IR_SEXT || in->op == IR_TRUNC)
                return true;
    }
    return false;
}

static bool strength_function(IrModule *m, IrFunc *f, const OptConfig *cfg,
                              const LoopTree *tree, bool log_bails)
{
    u32 li;
    bool saw_loop = false;
    bool saw_wrap = false;

    if (loop_tree_irreducible(tree)) {
        if (log_bails)
            OPT_BAIL(cfg, "strength", "loop_irreducible");
        return false;
    }
    /* One structural commit per invocation keeps all Loop pointers and the
     * dominator tree fresh.  The pass-manager fixpoint finds the next affine
     * expression on the next invocation. */
    for (li = 0; li < loop_tree_count(tree); li++) {
        const Loop *loop = loop_tree_at(tree, li);
        IrBlock *header;
        bool modular_iv = false;
        u32 pi;

        if (!loop)
            continue;
        saw_loop = true;
        header = ir_block(f, loop_header(loop));
        for (pi = 0; pi < header->nparams; pi++) {
            IvMatch iv;
            MulMatch mul;

            if (!match_iv(f, loop, pi, &iv))
                continue;
            if (find_mul(f, &iv, &mul))
                return commit_reduction(m, f, &iv, &mul);
            if (cfg->fwrapv || !iv.signed_no_wrap)
                modular_iv = true;
        }
        if (has_wrap_sensitive_shape(f, loop) && modular_iv)
            saw_wrap = true;
    }
    if (log_bails && saw_loop) {
        if (saw_wrap)
            OPT_BAIL(cfg, "strength", "sr_wrap");
        else
            OPT_BAIL(cfg, "strength", "sr_nonaffine");
    }
    return false;
}

bool opt_strength(IrModule *m, const OptConfig *cfg)
{
    u32 fi;
    bool any_changed = false;

    for (fi = 0; fi < m->nfuncs; fi++) {
        IrFunc *f = &m->funcs[fi];
        OptConfig local = *cfg;
        bool reduced_in_func = false;

        if (opt_func_has_vector_ir(f))
            continue;
        local.current_func = f->name;
        for (;;) {
            Arena scratch;
            IrDomTree *dom;
            LoopTree *tree;
            bool canonicalized, changed;
            char why[256];

            arena_init(&scratch);
            dom = ir_domtree_build(&scratch, f);
            tree = loop_tree_build(&scratch, f, dom);
            canonicalized = loop_canonicalize(m, f, tree);
            arena_free_all(&scratch);
            if (canonicalized)
                ir_func_renumber(m->arena, f);

            arena_init(&scratch);
            dom = ir_domtree_build(&scratch, f);
            tree = loop_tree_build(&scratch, f, dom);
            changed = strength_function(m, f, &local, tree, !reduced_in_func);
            arena_free_all(&scratch);

            if (canonicalized || changed) {
                any_changed = true;
                if (changed)
                    reduced_in_func = true;
                arena_init(&scratch);
                dom = ir_domtree_build(&scratch, f);
                tree = loop_tree_build(&scratch, f, dom);
                if (cfg->verify_after_each &&
                    !loop_tree_verify_canonical(tree, f, why, sizeof(why)))
                    CGF_ICE("strength: broke canonical loop form: %s", why);
                arena_free_all(&scratch);
            }
            if (!changed)
                break;
        }
    }
    return any_changed;
}
