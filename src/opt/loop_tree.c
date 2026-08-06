#include "opt/opt.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/arena.h"

typedef struct LoopExit {
    BlockId source;
    BlockId target;
} LoopExit;

struct Loop {
    BlockId header;
    BlockId preheader;
    struct Loop *parent;
    u32 depth;
    bool *members;
    BlockId *blocks;
    u32 nblocks;
    BlockId *latches;
    u32 nlatches;
    LoopExit *exits;
    u32 nexits;
    u32 universe;
};

struct LoopTree {
    const IrFunc *func;
    Loop *loops;
    u32 nloops;
    u32 nblocks;
    bool irreducible;
};

static u32 successor_count(const IrBlock *block)
{
    const IrInst *in;
    u32 count = 0;

    for (in = block->first; in; in = in->next)
        count += in->nedges;
    return count;
}

static BlockId successor_at(const IrBlock *block, u32 ordinal)
{
    const IrInst *in;

    for (in = block->first; in; in = in->next) {
        if (ordinal < in->nedges)
            return in->edges[ordinal].target;
        ordinal -= in->nedges;
    }
    return BLOCK_INVALID;
}

static bool has_edge(const IrFunc *f, BlockId from, BlockId to)
{
    const IrBlock *block;
    const IrInst *in;
    u32 ei;

    if (!from.v || from.v > f->nblocks)
        return false;
    block = &f->blocks[from.v - 1];
    for (in = block->first; in; in = in->next)
        for (ei = 0; ei < in->nedges; ei++)
            if (in->edges[ei].target.v == to.v)
                return true;
    return false;
}

static bool dfs_irreducible(Arena *arena, const IrFunc *f, const IrDomTree *dom)
{
    u8 *color;
    BlockId *stack;
    u32 *next;
    u32 sp = 0;

    if (!f->nblocks)
        return false;
    color = arena_alloc(arena, f->nblocks * sizeof(*color), _Alignof(u8));
    stack = arena_alloc(arena, f->nblocks * sizeof(*stack), _Alignof(BlockId));
    next = arena_alloc(arena, f->nblocks * sizeof(*next), _Alignof(u32));
    memset(color, 0, f->nblocks * sizeof(*color));
    stack[0] = (BlockId){1};
    next[0] = 0;
    color[0] = 1;
    sp = 1;
    while (sp) {
        BlockId block = stack[sp - 1];
        u32 count = successor_count(&f->blocks[block.v - 1]);

        if (next[sp - 1] >= count) {
            color[block.v - 1] = 2;
            sp--;
            continue;
        }
        {
            BlockId succ =
                successor_at(&f->blocks[block.v - 1], next[sp - 1]++);

            if (!succ.v || succ.v > f->nblocks)
                continue;
            if (color[succ.v - 1] == 1) {
                if (!ir_dominates(dom, succ, block))
                    return true;
            } else if (color[succ.v - 1] == 0) {
                color[succ.v - 1] = 1;
                stack[sp] = succ;
                next[sp] = 0;
                sp++;
            }
        }
    }
    return false;
}

static Loop *find_header_loop(LoopTree *tree, BlockId header)
{
    u32 i;

    for (i = 0; i < tree->nloops; i++)
        if (tree->loops[i].header.v == header.v)
            return &tree->loops[i];
    return NULL;
}

static void add_latch(Loop *loop, BlockId latch)
{
    u32 i;

    for (i = 0; i < loop->nlatches; i++)
        if (loop->latches[i].v == latch.v)
            return;
    loop->latches[loop->nlatches++] = latch;
}

static void collect_natural_body(Arena *arena, const IrFunc *f, Loop *loop,
                                 BlockId latch)
{
    BlockId *stack =
        arena_alloc(arena, f->nblocks * sizeof(*stack), _Alignof(BlockId));
    u32 sp = 0;

    loop->members[loop->header.v - 1] = true;
    if (!loop->members[latch.v - 1]) {
        loop->members[latch.v - 1] = true;
        if (latch.v != loop->header.v)
            stack[sp++] = latch;
    }
    while (sp) {
        BlockId block = stack[--sp];
        u32 pi;

        for (pi = 1; pi <= f->nblocks; pi++) {
            BlockId pred = {pi};

            if (!has_edge(f, pred, block) || loop->members[pi - 1])
                continue;
            loop->members[pi - 1] = true;
            if (pi != loop->header.v)
                stack[sp++] = pred;
        }
    }
}

static bool strict_subset(const Loop *inner, const Loop *outer, u32 nblocks)
{
    u32 bi;
    bool strict = false;

    for (bi = 0; bi < nblocks; bi++) {
        if (inner->members[bi] && !outer->members[bi])
            return false;
        if (outer->members[bi] && !inner->members[bi])
            strict = true;
    }
    return strict;
}

static BlockId detect_preheader(const IrFunc *f, const Loop *loop)
{
    BlockId pred = BLOCK_INVALID;
    u32 pi;

    for (pi = 1; pi <= f->nblocks; pi++) {
        BlockId candidate = {pi};

        if (loop->members[pi - 1] || !has_edge(f, candidate, loop->header))
            continue;
        if (pred.v && pred.v != candidate.v)
            return BLOCK_INVALID;
        pred = candidate;
    }
    if (pred.v) {
        const IrInst *term = f->blocks[pred.v - 1].last;

        if (!term || term->op != IR_BR || term->nedges != 1 ||
            term->edges[0].target.v != loop->header.v)
            return BLOCK_INVALID;
    }
    return pred;
}

static bool has_any_outside_predecessor(const IrFunc *f, const Loop *loop)
{
    u32 pi;

    for (pi = 1; pi <= f->nblocks; pi++)
        if (!loop->members[pi - 1] && has_edge(f, (BlockId){pi}, loop->header))
            return true;
    return false;
}

static void finish_loop(Arena *arena, const IrFunc *f, Loop *loop)
{
    u32 max_exits = 0;
    u32 bi;

    loop->universe = f->nblocks;
    loop->blocks = arena_alloc(arena, f->nblocks * sizeof(*loop->blocks),
                               _Alignof(BlockId));
    for (bi = 0; bi < f->nblocks; bi++)
        max_exits += successor_count(&f->blocks[bi]);
    loop->exits =
        arena_alloc(arena, (max_exits ? max_exits : 1) * sizeof(*loop->exits),
                    _Alignof(LoopExit));
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;
        u32 ei;

        if (!loop->members[bi])
            continue;
        loop->blocks[loop->nblocks++] = (BlockId){bi + 1};
        for (in = f->blocks[bi].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++) {
                BlockId target = in->edges[ei].target;
                u32 xi;

                if (!target.v || target.v > f->nblocks ||
                    loop->members[target.v - 1])
                    continue;
                for (xi = 0; xi < loop->nexits; xi++)
                    if (loop->exits[xi].source.v == bi + 1 &&
                        loop->exits[xi].target.v == target.v)
                        break;
                if (xi == loop->nexits)
                    loop->exits[loop->nexits++] = (LoopExit){{bi + 1}, target};
            }
    }
    loop->preheader = detect_preheader(f, loop);
}

LoopTree *loop_tree_build(Arena *arena, const IrFunc *f, const IrDomTree *dt)
{
    LoopTree *tree = arena_alloc(arena, sizeof(*tree), _Alignof(LoopTree));
    u32 max_loops = 0;
    u32 bi, li;

    memset(tree, 0, sizeof(*tree));
    tree->func = f;
    tree->nblocks = f->nblocks;
    for (bi = 0; bi < f->nblocks; bi++)
        max_loops += successor_count(&f->blocks[bi]);
    tree->loops =
        arena_alloc(arena, (max_loops ? max_loops : 1) * sizeof(*tree->loops),
                    _Alignof(Loop));
    memset(tree->loops, 0, (max_loops ? max_loops : 1) * sizeof(*tree->loops));

    for (bi = 0; bi < f->nblocks; bi++) {
        BlockId source = {bi + 1};
        const IrInst *in;
        u32 ei;

        for (in = f->blocks[bi].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++) {
                BlockId header = in->edges[ei].target;
                Loop *loop;

                if (!header.v || header.v > f->nblocks ||
                    !ir_dominates(dt, header, source))
                    continue;
                loop = find_header_loop(tree, header);
                if (!loop) {
                    loop = &tree->loops[tree->nloops++];
                    memset(loop, 0, sizeof(*loop));
                    loop->header = header;
                    loop->members = arena_alloc(
                        arena, f->nblocks * sizeof(bool), _Alignof(bool));
                    memset(loop->members, 0, f->nblocks * sizeof(bool));
                    loop->latches = arena_alloc(
                        arena, f->nblocks * sizeof(BlockId), _Alignof(BlockId));
                }
                add_latch(loop, source);
                collect_natural_body(arena, f, loop, source);
            }
    }
    for (li = 0; li < tree->nloops; li++)
        finish_loop(arena, f, &tree->loops[li]);
    for (li = 0; li < tree->nloops; li++) {
        Loop *loop = &tree->loops[li];
        Loop *parent = NULL;
        u32 oi;

        for (oi = 0; oi < tree->nloops; oi++) {
            Loop *candidate = &tree->loops[oi];

            if (candidate == loop ||
                !strict_subset(loop, candidate, f->nblocks))
                continue;
            if (!parent || candidate->nblocks < parent->nblocks)
                parent = candidate;
        }
        loop->parent = parent;
    }
    for (li = 0; li < tree->nloops; li++) {
        const Loop *p = tree->loops[li].parent;
        u32 depth = 0;

        while (p) {
            depth++;
            p = p->parent;
            if (depth > tree->nloops)
                CGF_ICE("loop_tree: cyclic nesting relation");
        }
        tree->loops[li].depth = depth;
    }
    tree->irreducible = dfs_irreducible(arena, f, dt);
    for (li = 0; li < tree->nloops; li++)
        if (!has_any_outside_predecessor(f, &tree->loops[li]))
            tree->irreducible = true;
    return tree;
}

u32 loop_tree_count(const LoopTree *lt)
{
    return lt ? lt->nloops : 0;
}

const Loop *loop_tree_at(const LoopTree *lt, u32 ordinal)
{
    return lt && ordinal < lt->nloops ? &lt->loops[ordinal] : NULL;
}

bool loop_tree_irreducible(const LoopTree *lt)
{
    return lt && lt->irreducible;
}

const Loop *loop_tree_innermost(const LoopTree *lt, BlockId block)
{
    const Loop *best = NULL;
    u32 i;

    if (!lt || !block.v || block.v > lt->nblocks)
        return NULL;
    for (i = 0; i < lt->nloops; i++)
        if (lt->loops[i].members[block.v - 1] &&
            (!best || lt->loops[i].depth > best->depth))
            best = &lt->loops[i];
    return best;
}

BlockId loop_header(const Loop *loop)
{
    return loop ? loop->header : BLOCK_INVALID;
}

BlockId loop_preheader(const Loop *loop)
{
    return loop ? loop->preheader : BLOCK_INVALID;
}

const Loop *loop_parent(const Loop *loop)
{
    return loop ? loop->parent : NULL;
}

u32 loop_depth(const Loop *loop)
{
    return loop ? loop->depth : 0;
}

u32 loop_block_count(const Loop *loop)
{
    return loop ? loop->nblocks : 0;
}

BlockId loop_block(const Loop *loop, u32 ordinal)
{
    return loop && ordinal < loop->nblocks ? loop->blocks[ordinal]
                                           : BLOCK_INVALID;
}

bool loop_contains(const Loop *loop, BlockId block)
{
    return loop && block.v && block.v <= loop->universe
               ? loop->members[block.v - 1]
               : false;
}

u32 loop_latch_count(const Loop *loop)
{
    return loop ? loop->nlatches : 0;
}

BlockId loop_latch(const Loop *loop, u32 ordinal)
{
    return loop && ordinal < loop->nlatches ? loop->latches[ordinal]
                                            : BLOCK_INVALID;
}

u32 loop_exit_count(const Loop *loop)
{
    return loop ? loop->nexits : 0;
}

BlockId loop_exit_source(const Loop *loop, u32 ordinal)
{
    return loop && ordinal < loop->nexits ? loop->exits[ordinal].source
                                          : BLOCK_INVALID;
}

BlockId loop_exit_target(const Loop *loop, u32 ordinal)
{
    return loop && ordinal < loop->nexits ? loop->exits[ordinal].target
                                          : BLOCK_INVALID;
}

static bool block_name_exists(const IrFunc *f, const char *name)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++)
        if (f->blocks[bi].name && strcmp(f->blocks[bi].name, name) == 0)
            return true;
    return false;
}

static const char *unique_block_name(IrModule *m, const IrFunc *f,
                                     const char *base, const char *suffix)
{
    size_t cap = strlen(base) + strlen(suffix) + 32;
    char *buf = cgf_xmalloc(cap);
    const char *name;
    u32 ordinal = 0;

    do {
        if (ordinal == 0)
            snprintf(buf, cap, "%s.%s", base, suffix);
        else
            snprintf(buf, cap, "%s.%s%u", base, suffix, ordinal);
        if (ordinal == UINT32_MAX)
            CGF_ICE("loop_tree: exhausted canonical block names");
        ordinal++;
    } while (block_name_exists(f, buf));
    name = arena_strdup(m->arena, buf);
    free(buf);
    return name;
}

static void redirect_edges(IrFunc *f, const Loop *loop, BlockId old_target,
                           BlockId new_target, bool sources_inside)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;
        bool inside = bi < loop->universe && loop->members[bi];
        u32 ei;

        if (bi + 1 == new_target.v || inside != sources_inside)
            continue;
        for (in = f->blocks[bi].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++)
                if (in->edges[ei].target.v == old_target.v)
                    in->edges[ei].target = new_target;
    }
}

static BlockId new_forwarder(IrModule *m, IrFunc *f, BlockId target,
                             const char *suffix)
{
    const IrBlock *old = ir_block(f, target);
    IrType *types = NULL;
    IrOperand *args = NULL;
    const char *base = old->name ? old->name : "loop";
    const char *name;
    BlockId block;
    IrBuilder builder;
    u32 nparams = old->nparams;
    u32 i;

    if (nparams) {
        types = cgf_xmalloc(nparams * sizeof(*types));
        args = cgf_xmalloc(nparams * sizeof(*args));
        for (i = 0; i < nparams; i++)
            types[i] = ir_value_type(f, old->params[i]);
    }
    name = unique_block_name(m, f, base, suffix);
    block = ir_block_new(m, f, name);
    for (i = 0; i < nparams; i++) {
        ValueId param = ir_block_param(m, f, block, types[i]);

        args[i] = ir_op_value(f, param);
    }
    ir_builder_at(&builder, m, f, block);
    ir_build_br(&builder, target, args, nparams);
    free(types);
    free(args);
    return block;
}

static bool has_outside_predecessor(const IrFunc *f, const Loop *loop,
                                    BlockId target)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++)
        if (!loop_contains(loop, (BlockId){bi + 1}) &&
            has_edge(f, (BlockId){bi + 1}, target))
            return true;
    return false;
}

static bool canonicalize_boundaries(IrModule *m, IrFunc *f,
                                    const LoopTree *tree)
{
    u32 li;

    for (li = 0; li < tree->nloops; li++) {
        const Loop *loop = &tree->loops[li];
        u32 xi;

        if (!loop->preheader.v) {
            BlockId preheader = new_forwarder(m, f, loop->header, "preheader");

            redirect_edges(f, loop, loop->header, preheader, false);
            return true;
        }
        for (xi = 0; xi < loop->nexits; xi++) {
            BlockId target = loop->exits[xi].target;
            BlockId split;
            u32 earlier;

            for (earlier = 0; earlier < xi; earlier++)
                if (loop->exits[earlier].target.v == target.v)
                    break;
            if (earlier != xi || !has_outside_predecessor(f, loop, target))
                continue;
            split = new_forwarder(m, f, target, "loopexit");
            redirect_edges(f, loop, target, split, true);
            return true;
        }
    }
    return false;
}

static bool operand_names_value(const IrOperand *op, ValueId value)
{
    return op->kind == IROP_VALUE && op->a == value.v;
}

static bool block_uses_value(const IrBlock *block, ValueId value)
{
    const IrInst *in;
    u32 oi, ei, ai;

    for (in = block->first; in; in = in->next) {
        for (oi = 0; oi < in->nops; oi++)
            if (operand_names_value(&in->ops[oi], value))
                return true;
        for (ei = 0; ei < in->nedges; ei++)
            for (ai = 0; ai < in->edges[ei].nargs; ai++)
                if (operand_names_value(&in->edges[ei].args[ai], value))
                    return true;
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

static void replace_block_uses(IrFunc *f, BlockId block, ValueId old,
                               ValueId replacement)
{
    IrInst *in;
    IrOperand op = ir_op_value(f, replacement);
    u32 oi, ei, ai;

    /* Routing a use through an LCSSA block parameter must not lose what the
     * use SITE said about it. Overwriting the operand outright dropped both
     * the ABI annotation and argflags, so a call argument that LCSSA
     * rewrote stopped being 'anon' -- and `printf("...", f(), g())` is
     * exactly the shape that produces. */
    for (in = f->blocks[block.v - 1].first; in; in = in->next) {
        for (oi = 0; oi < in->nops; oi++)
            if (operand_names_value(&in->ops[oi], old)) {
                IrOperand old_op = in->ops[oi];

                in->ops[oi] = op;
                ir_arg_carry_provenance(&in->ops[oi], &old_op);
            }
        for (ei = 0; ei < in->nedges; ei++)
            for (ai = 0; ai < in->edges[ei].nargs; ai++)
                if (operand_names_value(&in->edges[ei].args[ai], old)) {
                    IrOperand old_op = in->edges[ei].args[ai];

                    in->edges[ei].args[ai] = op;
                    ir_arg_carry_provenance(&in->edges[ei].args[ai], &old_op);
                }
    }
}

static bool lcssa_value(IrModule *m, IrFunc *f, const Loop *loop, ValueId value,
                        Arena *scratch)
{
    bool *need;
    BlockId *queue;
    ValueId *params;
    u32 head = 0, tail = 0, bi;
    bool used = false;

    arena_free_all(scratch);
    arena_init(scratch);
    need = arena_alloc(scratch, f->nblocks * sizeof(*need), _Alignof(bool));
    queue =
        arena_alloc(scratch, f->nblocks * sizeof(*queue), _Alignof(BlockId));
    params =
        arena_alloc(scratch, f->nblocks * sizeof(*params), _Alignof(ValueId));

    memset(need, 0, f->nblocks * sizeof(*need));
    memset(params, 0, f->nblocks * sizeof(*params));
    for (bi = 0; bi < f->nblocks; bi++) {
        BlockId block = {bi + 1};

        if (loop_contains(loop, block) ||
            !block_uses_value(&f->blocks[bi], value))
            continue;
        need[bi] = true;
        queue[tail++] = block;
        used = true;
    }
    if (!used)
        return false;

    /* Backward closure places parameters on every outside block from the
     * live-out uses back to the dedicated exits.  A valid SSA input cannot
     * reach such a use along a path that bypasses the loop definition. */
    while (head < tail) {
        BlockId block = queue[head++];
        u32 pi;

        for (pi = 1; pi <= f->nblocks; pi++) {
            BlockId pred = {pi};

            if (!has_edge(f, pred, block) || loop_contains(loop, pred) ||
                need[pi - 1])
                continue;
            need[pi - 1] = true;
            queue[tail++] = pred;
        }
    }
    for (bi = 0; bi < f->nblocks; bi++)
        if (need[bi])
            params[bi] = ir_block_param(m, f, (BlockId){bi + 1},
                                        ir_value_type(f, value));
    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;
        u32 ei;

        for (in = f->blocks[bi].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++) {
                BlockId target = in->edges[ei].target;
                IrOperand arg;

                if (!target.v || target.v > f->nblocks || !need[target.v - 1])
                    continue;
                if (loop_contains(loop, (BlockId){bi + 1})) {
                    arg = ir_op_value(f, value);
                } else {
                    if (!need[bi])
                        CGF_ICE("loop_tree: LCSSA value reaches use through "
                                "an undominated outside predecessor");
                    arg = ir_op_value(f, params[bi]);
                }
                append_edge_arg(m, &in->edges[ei], arg);
            }
    }
    for (bi = 0; bi < f->nblocks; bi++)
        if (need[bi])
            replace_block_uses(f, (BlockId){bi + 1}, value, params[bi]);
    return true;
}

static bool establish_lcssa(IrModule *m, IrFunc *f, const LoopTree *tree,
                            Arena *scratch)
{
    bool changed = false;
    u32 depth, max_depth = 0, li;

    for (li = 0; li < tree->nloops; li++)
        if (tree->loops[li].depth > max_depth)
            max_depth = tree->loops[li].depth;
    for (depth = max_depth + 1; depth-- > 0;)
        for (li = 0; li < tree->nloops; li++) {
            const Loop *loop = &tree->loops[li];
            u32 original_nvals = f->nvals;
            u32 vi;

            if (loop->depth != depth)
                continue;
            for (vi = 1; vi <= original_nvals; vi++) {
                BlockId def = f->vals[vi - 1].def_block;

                if (def.v && loop_contains(loop, def))
                    changed |= lcssa_value(m, f, loop, (ValueId){vi}, scratch);
            }
        }
    return changed;
}

bool loop_canonicalize(IrModule *m, IrFunc *f, const LoopTree *lt)
{
    const LoopTree *tree = lt;
    Arena scratch;
    Arena lcssa_scratch;
    bool scratch_live = false;
    bool changed = false;
    u32 iteration = 0;

    if (!tree || tree->func != f || tree->irreducible)
        return false;
    for (;;) {
        if (canonicalize_boundaries(m, f, tree)) {
            IrDomTree *dom;

            changed = true;
            ir_func_renumber(m->arena, f);
            if (scratch_live)
                arena_free_all(&scratch);
            arena_init(&scratch);
            scratch_live = true;
            dom = ir_domtree_build(&scratch, f);
            tree = loop_tree_build(&scratch, f, dom);
            if (++iteration > f->nblocks + 1)
                CGF_ICE("loop_tree: canonicalization did not converge");
            continue;
        }
        break;
    }
    arena_init(&lcssa_scratch);
    if (establish_lcssa(m, f, tree, &lcssa_scratch)) {
        changed = true;
        ir_func_renumber(m->arena, f);
    }
    arena_free_all(&lcssa_scratch);
    if (scratch_live)
        arena_free_all(&scratch);
    return changed;
}

static bool fail_reason(char *why, size_t why_cap, const char *fmt, ...)
{
    va_list ap;

    if (why && why_cap) {
        va_start(ap, fmt);
        vsnprintf(why, why_cap, fmt, ap);
        va_end(ap);
    }
    return false;
}

bool loop_tree_verify_canonical(const LoopTree *lt, const IrFunc *f, char *why,
                                size_t why_cap)
{
    u32 li;

    if (!lt || lt->func != f)
        return fail_reason(why, why_cap,
                           "loop tree belongs to another function");
    if (lt->irreducible)
        return true;
    for (li = 0; li < lt->nloops; li++) {
        const Loop *loop = &lt->loops[li];
        BlockId preheader = detect_preheader(f, loop);
        u32 xi, vi, bi;

        if (!preheader.v)
            return fail_reason(why, why_cap,
                               "loop header %u has no dedicated preheader",
                               loop->header.v);
        for (xi = 0; xi < loop->nexits; xi++)
            if (has_outside_predecessor(f, loop, loop->exits[xi].target))
                return fail_reason(why, why_cap,
                                   "loop exit %u has a non-loop predecessor",
                                   loop->exits[xi].target.v);
        for (vi = 1; vi <= f->nvals; vi++) {
            BlockId def = f->vals[vi - 1].def_block;

            if (!def.v || !loop_contains(loop, def))
                continue;
            for (bi = 0; bi < f->nblocks; bi++)
                if (!loop_contains(loop, (BlockId){bi + 1}) &&
                    block_uses_value(&f->blocks[bi], (ValueId){vi}))
                    return fail_reason(why, why_cap,
                                       "loop value %u is used outside without "
                                       "an exit parameter",
                                       vi);
        }
    }
    if (why && why_cap)
        why[0] = '\0';
    return true;
}
