#include "opt/opt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util/arena.h"

typedef struct {
    u32 block;
    IrInst *call;
    IrInst *prev;
} CallSite;

typedef enum {
    INL_LOOP_UNKNOWN,
    INL_LOOP_NO,
    INL_LOOP_YES,
} InlLoopState;

typedef struct {
    u32 insts;
    u32 returns;
    bool has_alloca;
    bool has_dynamic_alloca;
    bool has_va_start;
    bool recursive;
    bool force_recursive;
    u8 *control_params;
} InlFuncFacts;

typedef struct {
    Arena arena;
    u32 *direct_calls;
    InlFuncFacts *funcs;
} InlScanCache;

typedef struct {
    Arena arena;
    u8 *blocks;
} InlLoopCache;

enum {
    /* Bound compile work as well as code growth over the complete optimization
     * pipeline.  This is deliberately an instruction budget, not a site count:
     * many tiny leaf calls remain cheap, while giant dispatchers cannot consume
     * unbounded optimization time. */
    INL_MIN_GROWTH_BUDGET = 128,
    INL_MAX_CALLER_INSTS = 512,
};

static void *inl_grow(Arena *a, const void *old, u32 oldn, u32 newn,
                      size_t size, size_t align)
{
    void *p = arena_alloc(a, (size_t)newn * size, align);

    if (oldn)
        memcpy(p, old, (size_t)oldn * size);
    return p;
}

static IrOperand *copy_ops(IrModule *m, const IrOperand *ops, u32 n)
{
    IrOperand *out;

    if (!n)
        return NULL;
    out = arena_alloc(m->arena, (size_t)n * sizeof(*out), _Alignof(IrOperand));
    memcpy(out, ops, (size_t)n * sizeof(*out));
    return out;
}

static IrEdge *copy_edges(IrModule *m, const IrEdge *edges, u32 n)
{
    IrEdge *out;
    u32 i;

    if (!n)
        return NULL;
    out = arena_alloc(m->arena, (size_t)n * sizeof(*out), _Alignof(IrEdge));
    memcpy(out, edges, (size_t)n * sizeof(*out));
    for (i = 0; i < n; i++)
        out[i].args = copy_ops(m, edges[i].args, edges[i].nargs);
    return out;
}

static ValueId add_value(IrModule *m, IrFunc *f, IrType type, BlockId block,
                         u32 pos)
{
    IrValInfo *vi;
    ValueId id;

    if (f->nvals == f->cap_vals) {
        u32 cap = f->cap_vals ? f->cap_vals * 2 : 16;

        f->vals = inl_grow(m->arena, f->vals, f->nvals, cap, sizeof(*f->vals),
                           _Alignof(IrValInfo));
        f->cap_vals = cap;
    }
    vi = &f->vals[f->nvals];
    memset(vi, 0, sizeof(*vi));
    vi->type = (u8)type;
    vi->def_kind = VDEF_INST;
    vi->def_block = block;
    vi->def_pos = pos;
    id.v = ++f->nvals;
    return id;
}

static IrInst *new_inst(IrModule *m)
{
    IrInst *in = arena_alloc(m->arena, sizeof(*in), _Alignof(IrInst));

    memset(in, 0, sizeof(*in));
    return in;
}

static void append_inst(IrBlock *block, IrInst *in)
{
    if (block->last)
        block->last->next = in;
    else
        block->first = in;
    block->last = in;
    block->ninsts++;
}

static IrInst *make_br(IrModule *m, BlockId target, const IrOperand *args,
                       u32 nargs, u32 loc)
{
    IrInst *in = new_inst(m);

    in->op = IR_BR;
    in->type = IRT_VOID;
    in->loc = loc;
    in->nedges = 1;
    in->edges = arena_alloc(m->arena, sizeof(*in->edges), _Alignof(IrEdge));
    memset(in->edges, 0, sizeof(*in->edges));
    in->edges[0].target = target;
    in->edges[0].args = copy_ops(m, args, nargs);
    in->edges[0].nargs = nargs;
    return in;
}

static bool recursive_node(const Callgraph *g, u32 node)
{
    u32 scc = ipo_callgraph_scc_of(g, node);
    u32 i;

    if (scc == UINT32_MAX)
        return false;
    if (ipo_callgraph_scc_size(g, scc) > 1)
        return true;
    for (i = 0; i < ipo_callgraph_edge_count(g, node); i++)
        if (ipo_callgraph_edge(g, node, i) == node)
            return true;
    return false;
}

/* Ordinary inlining refuses any recursive SCC. Mandatory inlining needs the
 * narrower question: will repeatedly expanding calls whose TARGET is marked
 * always_inline ever return to this function? A forced helper may call an
 * ordinary outer function which calls the helper back; expanding the helper
 * once merely turns that edge into an ordinary recursive call, exactly as
 * GCC does. Only a cycle made entirely of forced-target edges is unbounded. */
static bool force_recursive_node(const IrModule *m, const Callgraph *g,
                                 u32 node)
{
    bool *seen;
    u32 *work;
    u32 nwork = 0;
    bool recursive = false;

    seen = cgf_xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(*seen));
    work = cgf_xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(*work));
    memset(seen, 0, m->nfuncs * sizeof(*seen));
    seen[node] = true;
    work[nwork++] = node;
    while (nwork && !recursive) {
        u32 from = work[--nwork];
        u32 ei;

        for (ei = 0; ei < ipo_callgraph_edge_count(g, from); ei++) {
            u32 to = ipo_callgraph_edge(g, from, ei);

            if (to >= m->nfuncs || !m->funcs[to].always_inline)
                continue;
            if (to == node) {
                recursive = true;
                break;
            }
            if (!seen[to]) {
                seen[to] = true;
                work[nwork++] = to;
            }
        }
    }
    free(seen);
    free(work);
    return recursive;
}

static void scan_func_facts(InlScanCache *cache, const IrModule *m,
                            const Callgraph *graph, u32 fi,
                            bool count_direct_calls)
{
    const IrFunc *f = &m->funcs[fi];
    InlFuncFacts *facts = &cache->funcs[fi];
    u32 bi;

    facts->insts = 0;
    facts->returns = 0;
    facts->has_alloca = false;
    facts->has_dynamic_alloca = false;
    facts->has_va_start = false;
    /* The callgraph describes the module before any splicing.  Preserve its
     * cycle facts when rescanning a mutated caller; forced expansion only
     * removes a forced edge or exposes forced edges already present in the
     * original graph. */
    if (count_direct_calls) {
        facts->recursive = recursive_node(graph, fi);
        facts->force_recursive =
            f->always_inline && force_recursive_node(m, graph, fi);
    }
    memset(facts->control_params, 0,
           f->nparams * sizeof(*facts->control_params));
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 pi;

            facts->insts++;
            facts->returns += in->op == IR_RET;
            facts->has_alloca |= in->op == IR_ALLOCA;
            facts->has_dynamic_alloca |=
                in->op == IR_ALLOCA &&
                (in->nops == 0 || in->ops[0].kind != IROP_ICONST);
            facts->has_va_start |= in->op == IR_VA_START;
            if (in == f->blocks[bi].last &&
                (in->op == IR_CONDBR || in->op == IR_SWITCH) && in->nops &&
                in->ops[0].kind == IROP_VALUE)
                for (pi = 0; pi < f->nparams; pi++)
                    if (f->param_vals[pi].v == in->ops[0].a)
                        facts->control_params[pi] = true;
            if (count_direct_calls && in->op == IR_CALL &&
                in->subop == FUNCREF_INTERNAL && in->callee < m->nfuncs)
                cache->direct_calls[in->callee]++;
        }
    }
}

static void scan_cache_build(InlScanCache *cache, const IrModule *m,
                             const Callgraph *graph)
{
    u32 fi;

    arena_init(&cache->arena);
    cache->direct_calls =
        arena_alloc(&cache->arena,
                    (m->nfuncs ? m->nfuncs : 1) * sizeof(*cache->direct_calls),
                    _Alignof(u32));
    cache->funcs = arena_alloc(
        &cache->arena, (m->nfuncs ? m->nfuncs : 1) * sizeof(*cache->funcs),
        _Alignof(InlFuncFacts));
    memset(cache->direct_calls, 0, m->nfuncs * sizeof(*cache->direct_calls));
    memset(cache->funcs, 0, m->nfuncs * sizeof(*cache->funcs));
    for (fi = 0; fi < m->nfuncs; fi++) {
        const IrFunc *f = &m->funcs[fi];
        InlFuncFacts *facts = &cache->funcs[fi];

        facts->control_params = arena_alloc(
            &cache->arena, (f->nparams ? f->nparams : 1) * sizeof(u8),
            _Alignof(u8));
        scan_func_facts(cache, m, graph, fi, true);
    }
}

static void add_cloned_direct_calls(InlScanCache *cache, const IrModule *m,
                                    const IrFunc *callee)
{
    u32 bi;

    for (bi = 0; bi < callee->nblocks; bi++) {
        const IrInst *in;

        for (in = callee->blocks[bi].first; in; in = in->next) {
            if (in->op != IR_CALL || in->subop != FUNCREF_INTERNAL ||
                in->callee >= m->nfuncs)
                continue;
            cache->direct_calls[in->callee]++;
        }
    }
}

static u32 growth_budget(u32 original_insts)
{
    u64 budget = original_insts;
    u32 room;

    if (original_insts >= INL_MAX_CALLER_INSTS)
        return 0;
    room = INL_MAX_CALLER_INSTS - original_insts;

    if (budget < INL_MIN_GROWTH_BUDGET)
        budget = INL_MIN_GROWTH_BUDGET;
    if (budget > room)
        budget = room;
    return (u32)budget;
}

static bool block_in_loop(InlLoopCache *cache, const IrFunc *f, u32 block)
{
    bool *seen;
    u32 *work;
    u32 nwork = 0;
    const IrInst *term;
    u32 ei;
    bool in_loop = false;

    if (block >= f->nblocks)
        return false;
    if (cache->blocks[block] != INL_LOOP_UNKNOWN)
        return cache->blocks[block] == INL_LOOP_YES;
    seen =
        arena_alloc(&cache->arena, f->nblocks * sizeof(*seen), _Alignof(bool));
    work =
        arena_alloc(&cache->arena, f->nblocks * sizeof(*work), _Alignof(u32));
    memset(seen, 0, f->nblocks * sizeof(*seen));
    term = f->blocks[block].last;
    if (!term)
        goto done;
    /* The call block belongs to a cyclic CFG SCC iff one of its successors
     * can reach it.  This covers natural, self, and irreducible loops; the
     * last class has no dominance-classified retreating edge but repeats an
     * inlined alloca just as surely. */
    for (ei = 0; ei < term->nedges; ei++) {
        u32 target = term->edges[ei].target.v;

        if (target == block + 1) {
            in_loop = true;
            goto done;
        }
        if (target && target <= f->nblocks && !seen[target - 1]) {
            seen[target - 1] = true;
            work[nwork++] = target - 1;
        }
    }
    while (nwork) {
        u32 node = work[--nwork];
        const IrInst *next = f->blocks[node].last;

        if (!next)
            continue;
        for (ei = 0; ei < next->nedges; ei++) {
            u32 target = next->edges[ei].target.v;

            if (target == block + 1) {
                in_loop = true;
                goto done;
            }
            if (target && target <= f->nblocks && !seen[target - 1]) {
                seen[target - 1] = true;
                work[nwork++] = target - 1;
            }
        }
    }
done:
    cache->blocks[block] = in_loop ? INL_LOOP_YES : INL_LOOP_NO;
    return in_loop;
}

static bool call_matches_unprototyped_body(const IrInst *call,
                                           const IrFunc *callee)
{
    u32 i;

    if (call->nops != callee->nparams)
        return false;
    for (i = 0; i < call->nops; i++)
        if (call->ops[i].type != callee->param_types[i])
            return false;
    return true;
}

static bool block_name_exists(const IrFunc *f, const char *name)
{
    u32 i;

    for (i = 0; i < f->nblocks; i++)
        if (f->blocks[i].name && strcmp(f->blocks[i].name, name) == 0)
            return true;
    return false;
}

static const char *site_name(IrModule *m, u32 serial, const char *suffix)
{
    char tmp[96];
    size_t n;
    char *out;

    snprintf(tmp, sizeof(tmp), "inl.%u.%s", serial, suffix);
    n = strlen(tmp);
    out = arena_alloc(m->arena, n + 1, 1);
    memcpy(out, tmp, n + 1);
    return out;
}

static u32 choose_serial(const IrFunc *caller, u32 nblocks, bool need_cont,
                         u32 *next)
{
    char tmp[96];
    u32 serial = *next;

    for (;;) {
        u32 i;
        bool collision = false;

        for (i = 0; i < nblocks && !collision; i++) {
            snprintf(tmp, sizeof(tmp), "inl.%u.b%u", serial, i);
            collision = block_name_exists(caller, tmp);
        }
        if (need_cont && !collision) {
            snprintf(tmp, sizeof(tmp), "inl.%u.join", serial);
            collision = block_name_exists(caller, tmp);
        }
        if (!collision)
            break;
        serial++;
    }
    *next = serial + 1;
    return serial;
}

static IrOperand mapped_operand(const IrOperand *map, IrOperand old)
{
    IrOperand out;

    if (old.kind != IROP_VALUE)
        return old;
    out = map[old.a];
    /* Operand.b and argflags on a value are use-site ABI annotations.
     * Parameter substitution must not leak the outer call's annotation into
     * ordinary callee uses, while a nested byval/sret call keeps its own --
     * which is precisely ir_arg_carry_provenance's contract, including the
     * fconst guard this site had and the argflags carry it did not. Losing
     * argflags here dropped IROPF_ANON from a variadic call inside an
     * inlined body: verifier check 9. */
    ir_arg_carry_provenance(&out, &old);
    return out;
}

static void map_inst_operands(IrInst *in, const IrOperand *map)
{
    u32 i, j;

    for (i = 0; i < in->nops; i++)
        in->ops[i] = mapped_operand(map, in->ops[i]);
    for (i = 0; i < in->nedges; i++)
        for (j = 0; j < in->edges[i].nargs; j++)
            in->edges[i].args[j] = mapped_operand(map, in->edges[i].args[j]);
}

static void replace_value(IrFunc *f, ValueId old, IrOperand replacement)
{
    u32 bi;

    if (!old.v)
        return;
    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 i, j;

            /* ir_arg_carry_provenance, not a local copy of it: this was the
             * sixth site hand-rolling "re-attach a call argument's
             * annotation across a replacement", and like three of the
             * earlier five it carried the ABI word `b` and silently dropped
             * `argflags`. Substituting an inlined parameter into a variadic
             * call then lost IROPF_ANON on exactly the arguments the
             * inliner made constant -- verifier check 9. */
            for (i = 0; i < in->nops; i++)
                if (in->ops[i].kind == IROP_VALUE && in->ops[i].a == old.v) {
                    IrOperand old_op = in->ops[i];

                    in->ops[i] = replacement;
                    ir_arg_carry_provenance(&in->ops[i], &old_op);
                }
            for (i = 0; i < in->nedges; i++)
                for (j = 0; j < in->edges[i].nargs; j++)
                    if (in->edges[i].args[j].kind == IROP_VALUE &&
                        in->edges[i].args[j].a == old.v) {
                        IrOperand old_op = in->edges[i].args[j];

                        in->edges[i].args[j] = replacement;
                        ir_arg_carry_provenance(&in->edges[i].args[j], &old_op);
                    }
        }
    }
}

static void append_local_slots(IrModule *m, IrFunc *caller,
                               const IrFunc *callee, const IrOperand *map)
{
    u32 i;

    for (i = 0; i < callee->nlocal_slots; i++) {
        IrOperand addr = map[callee->local_slots[i].addr.v];
        IrLocalSlot *slot;

        if (addr.kind != IROP_VALUE)
            continue;
        if (caller->nlocal_slots == caller->cap_local_slots) {
            u32 cap = caller->cap_local_slots ? caller->cap_local_slots * 2 : 8;

            caller->local_slots = inl_grow(
                m->arena, caller->local_slots, caller->nlocal_slots, cap,
                sizeof(*caller->local_slots), _Alignof(IrLocalSlot));
            caller->cap_local_slots = cap;
        }
        slot = &caller->local_slots[caller->nlocal_slots++];
        *slot = callee->local_slots[i];
        slot->addr.v = (u32)addr.a;
    }
}

static bool inline_site(IrModule *m, IrFunc *caller, const IrFunc *callee,
                        const CallSite *site, u32 returns, u32 *next_serial)
{
    IrOperand *map;
    BlockId *blocks;
    IrInst *suffix = site->call->next;
    u32 suffix_count = 0;
    u32 serial =
        choose_serial(caller, callee->nblocks, returns > 1, next_serial);
    BlockId join = BLOCK_INVALID;
    ValueId join_value = VALUE_INVALID;
    IrOperand single_result = {0};
    IrBlock *call_block;
    IrInlinePinnedPlan pinned_plan;
    IrInst **pinned_clones = NULL;
    u32 bi, i, pinned_at = 0;

    for (IrInst *in = suffix; in; in = in->next)
        suffix_count++;
    ir_capture_inline_pinned_plan(m, caller, site->call, callee, &pinned_plan);
    if (pinned_plan.nops)
        pinned_clones = arena_alloc(
            m->arena, (size_t)pinned_plan.nops * sizeof(*pinned_clones),
            _Alignof(IrInst *));
    map = arena_alloc(m->arena, (callee->nvals + 1) * sizeof(*map),
                      _Alignof(IrOperand));
    memset(map, 0, (callee->nvals + 1) * sizeof(*map));
    blocks = arena_alloc(
        m->arena, (callee->nblocks ? callee->nblocks : 1) * sizeof(*blocks),
        _Alignof(BlockId));
    for (i = 0; i < callee->nparams; i++) {
        IrOperand arg = site->call->ops[i];

        if (arg.kind != IROP_FCONST)
            arg.b = 0;
        map[callee->param_vals[i].v] = arg;
    }
    for (bi = 0; bi < callee->nblocks; bi++) {
        char suffix_name[32];

        snprintf(suffix_name, sizeof(suffix_name), "b%u", bi);
        blocks[bi] = ir_block_new(m, caller, site_name(m, serial, suffix_name));
    }
    if (returns > 1) {
        join = ir_block_new(m, caller, site_name(m, serial, "join"));
        if (site->call->type != IRT_VOID)
            join_value =
                ir_block_param(m, caller, join, (IrType)site->call->type);
    }
    for (bi = 0; bi < callee->nblocks; bi++) {
        const IrBlock *src = &callee->blocks[bi];
        u32 pos = 0;

        for (i = 0; i < src->nparams; i++) {
            IrType type = ir_value_type(callee, src->params[i]);
            ValueId value = ir_block_param(m, caller, blocks[bi], type);

            map[src->params[i].v] = ir_op_value(caller, value);
        }
        for (const IrInst *in = src->first; in; in = in->next, pos++)
            if (in->result.v) {
                ValueId value =
                    add_value(m, caller, (IrType)in->type, blocks[bi], pos);

                map[in->result.v] = ir_op_value(caller, value);
            }
    }
    for (bi = 0; bi < callee->nblocks; bi++) {
        const IrBlock *src = &callee->blocks[bi];
        IrBlock *dst = ir_block(caller, blocks[bi]);
        const IrInst *in;

        for (in = src->first; in; in = in->next) {
            IrInst *clone;

            if (in->op == IR_RET) {
                if (returns == 1) {
                    if (in->nops)
                        single_result = mapped_operand(map, in->ops[0]);
                    continue;
                }
                if (in->nops) {
                    IrOperand result = mapped_operand(map, in->ops[0]);

                    append_inst(dst, make_br(m, join, &result, 1, in->loc));
                } else {
                    append_inst(dst, make_br(m, join, NULL, 0, in->loc));
                }
                continue;
            }
            clone = new_inst(m);
            *clone = *in;
            clone->next = NULL;
            clone->ops = copy_ops(m, in->ops, in->nops);
            clone->edges = copy_edges(m, in->edges, in->nedges);
            if (in->result.v)
                clone->result.v = (u32)map[in->result.v].a;
            map_inst_operands(clone, map);
            for (i = 0; i < clone->nedges; i++)
                clone->edges[i].target = blocks[clone->edges[i].target.v - 1];
            append_inst(dst, clone);
            if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) {
                if (pinned_at >= pinned_plan.nops ||
                    pinned_plan.sources[pinned_at] != in)
                    CGF_ICE(
                        "inline: pinned clone order disagrees with capture");
                pinned_clones[pinned_at++] = clone;
            }
        }
    }
    append_local_slots(m, caller, callee, map);

    /* The old call block becomes the unique gateway to the cloned entry. */
    call_block = &caller->blocks[site->block];
    if (site->prev) {
        site->prev->next = NULL;
        call_block->last = site->prev;
    } else {
        call_block->first = NULL;
        call_block->last = NULL;
    }
    call_block->ninsts -= suffix_count + 1;
    append_inst(call_block, make_br(m, blocks[0], NULL, 0, site->call->loc));

    if (returns > 1) {
        IrBlock *cont = ir_block(caller, join);

        cont->first = suffix;
        cont->last = suffix;
        cont->ninsts = suffix_count;
        while (cont->last && cont->last->next)
            cont->last = cont->last->next;
        if (site->call->result.v)
            replace_value(caller, site->call->result,
                          ir_op_value(caller, join_value));
    } else if (returns == 1) {
        /* A unique return needs no synthetic phi/join: its cloned block is
         * the continuation, and the returned SSA value dominates the suffix. */
        IrBlock *ret_block = NULL;

        for (bi = 0; bi < callee->nblocks; bi++)
            if (callee->blocks[bi].last &&
                callee->blocks[bi].last->op == IR_RET) {
                ret_block = ir_block(caller, blocks[bi]);
                break;
            }
        if (!ret_block)
            CGF_ICE("inline: return count disagrees with cloned CFG");
        if (ret_block->last)
            ret_block->last->next = suffix;
        else
            ret_block->first = suffix;
        if (suffix) {
            ret_block->last = suffix;
            while (ret_block->last->next)
                ret_block->last = ret_block->last->next;
        }
        ret_block->ninsts += suffix_count;
        if (site->call->result.v)
            replace_value(caller, site->call->result, single_result);
    }
    if (pinned_at != pinned_plan.nops)
        CGF_ICE("inline: pinned clone count disagrees with source");
    ir_record_inline_pinned_group(m, caller, callee, &pinned_plan,
                                  pinned_clones, pinned_plan.nops);
    return true;
}

static bool eligible(IrModule *m, u32 caller_index, const CallSite *site,
                     const Callgraph *graph, const OptConfig *cfg,
                     InlScanCache *cache, InlLoopCache *loops, u32 growth_left)
{
    const IrFunc *caller = &m->funcs[caller_index];
    const IrInst *call = site->call;
    const IrFunc *callee;
    OptConfig fc = *cfg;
    bool forced;
    u64 threshold;
    u32 i;

    fc.current_func = caller->name;
    if (call->subop == FUNCREF_INDIRECT) {
        OPT_BAIL(&fc, "inline", "inl_indirect");
        return false;
    }
    if (call->subop != FUNCREF_INTERNAL || call->callee >= m->nfuncs)
        return false;
    callee = &m->funcs[call->callee];
    forced = callee->always_inline;
    /* An ownership annotation is a call-boundary contract and may be
     * deliberately stronger than the current body.  Splicing that body
     * would erase the promised effect before memsafe applies summaries. */
    if (callee->cgf_attrs) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "ownership call-boundary contract cannot be preserved",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_memsafe_contract");
        return false;
    }
    /* The current DWARF backend has no abstract-origin/inlined-subroutine
     * records.  Splicing a debug build would silently erase source-level
     * breakpoint and backtrace frames promised by Sprint 29. */
    if (cfg->debug_info && !forced) {
        OPT_BAIL(&fc, "inline", "inl_debug_info");
        return false;
    }
    if (callee->variadic && cache->funcs[call->callee].has_va_start) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "variadic argument state cannot be spliced safely",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_va_start");
        return false;
    }
    /* A no-prototype call may omit body parameters or pass differently
     * promoted types.  Mapping the callee's concrete parameter SSA values
     * onto such operands would either index past the call or change C's
     * old-style ABI semantics. */
    if ((callee->unprototyped || (call->flags & IRF_CALL_UNPROTOTYPED) != 0) &&
        (!forced || !call_matches_unprototyped_body(call, callee))) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "call has an unprototyped signature",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_unprototyped_signature");
        return false;
    }
    if (caller->calls_setjmp || callee->calls_setjmp) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "setjmp frame cannot be spliced safely",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_setjmp");
        return false;
    }
    /* A no-return splice would make the caller suffix unreachable.  Leave it
     * intact until the pinned-operation audit can distinguish intentionally
     * dead suffixes from transformations that lose observable operations. */
    if (cache->funcs[call->callee].returns == 0) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "function has no returning path",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_noreturn");
        return false;
    }
    /* x87 f80 values obey the backend's memory law: lowering represents
     * them through stack slots and never as block parameters.  A multi-return
     * splice needs a join parameter for its result, so inlining it would
     * manufacture the one IR shape x86 isel cannot represent.  Single-return
     * f80 callees need no join and remain eligible.  musl's floatscan campaign
     * reached this through hexfloat at -O3. */
    if (callee->ret == IRT_F80 && cache->funcs[call->callee].returns > 1) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "multiple long-double return paths are not supported",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_f80_multiret");
        return false;
    }
    /* A recursive callee cannot be spliced even into a caller outside its
     * SCC: the cloned recursive edge would otherwise become a fresh eligible
     * site on the next scan/fixpoint iteration and expand without bound. */
    if ((forced && cache->funcs[call->callee].force_recursive) ||
        (!forced && (cache->funcs[call->callee].recursive ||
                     ipo_callgraph_scc_of(graph, caller_index) ==
                         ipo_callgraph_scc_of(graph, call->callee)))) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "recursive call",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_recursion");
        return false;
    }
    if ((forced ? cache->funcs[call->callee].has_dynamic_alloca
                : cache->funcs[call->callee].has_alloca) &&
        block_in_loop(loops, caller, site->block)) {
        if (forced)
            diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, call),
                      "inlining failed in call to 'always_inline' '%s': "
                      "alloca in a repeated caller region cannot preserve "
                      "callee lifetime",
                      callee->name);
        else
            OPT_BAIL(&fc, "inline", "inl_alloca_in_loop");
        return false;
    }
    if (forced)
        return true;
    threshold = cfg->inline_threshold;
    if (callee->linkage == IRLINK_INTERNAL &&
        cache->direct_calls[call->callee] == 1 &&
        !ipo_callgraph_address_taken(graph, call->callee))
        threshold *= 4;
    for (i = 0; i < callee->nparams && i < call->nops; i++)
        if (call->ops[i].kind != IROP_VALUE &&
            call->ops[i].kind != IROP_UNDEF &&
            cache->funcs[call->callee].control_params[i])
            threshold += cfg->level == OPT_OS ? 5 : 10;
    if (cache->funcs[call->callee].insts > threshold) {
        OPT_BAIL(&fc, "inline", "inl_cost");
        return false;
    }
    if (cache->funcs[call->callee].insts > growth_left) {
        OPT_BAIL(&fc, "inline", "inl_growth_budget");
        return false;
    }
    return true;
}

static bool run_inline(IrModule *m, const OptConfig *cfg, bool forced_only)
{
    Callgraph *graph;
    InlScanCache cache;
    bool changed = false;
    u32 oi, nsccs;

    graph = ipo_callgraph_build(m);
    scan_cache_build(&cache, m, graph);
    nsccs = ipo_callgraph_scc_count(graph);
    for (oi = 0; oi < nsccs; oi++) {
        u32 scc = ipo_callgraph_bottom_up_scc(graph, oi);
        u32 mi;

        for (mi = 0; mi < ipo_callgraph_scc_size(graph, scc); mi++) {
            u32 fi = ipo_callgraph_scc_member(graph, scc, mi);
            u32 serial = 0;
            bool func_changed = false;

            if (!forced_only && !m->funcs[fi].opt_inline_growth_initialized) {
                m->funcs[fi].opt_inline_growth_left =
                    growth_budget(cache.funcs[fi].insts);
                m->funcs[fi].opt_inline_growth_initialized = true;
            }

            for (;;) {
                CallSite site;
                InlLoopCache loops;
                bool found_eligible = false;
                u32 bi;

                arena_init(&loops.arena);
                loops.blocks = arena_alloc(
                    &loops.arena,
                    (m->funcs[fi].nblocks ? m->funcs[fi].nblocks : 1) *
                        sizeof(*loops.blocks),
                    _Alignof(u8));
                memset(loops.blocks, INL_LOOP_UNKNOWN,
                       m->funcs[fi].nblocks * sizeof(*loops.blocks));
                /* Scan every call in document order, logging every named bail,
                 * and commit the first eligible site before restarting because
                 * block compaction invalidates saved block coordinates. */
                for (bi = 0; bi < m->funcs[fi].nblocks && !found_eligible;
                     bi++) {
                    IrInst *prev = NULL;
                    IrInst *in;

                    for (in = m->funcs[fi].blocks[bi].first; in;
                         prev = in, in = in->next) {
                        if (in->op != IR_CALL)
                            continue;
                        if (forced_only &&
                            (in->subop != FUNCREF_INTERNAL ||
                             in->callee >= m->nfuncs ||
                             !m->funcs[in->callee].always_inline))
                            continue;
                        site.block = bi;
                        site.call = in;
                        site.prev = prev;
                        found_eligible =
                            eligible(m, fi, &site, graph, cfg, &cache, &loops,
                                     m->funcs[fi].opt_inline_growth_left);
                        if (found_eligible)
                            break;
                    }
                }
                if (!found_eligible) {
                    arena_free_all(&loops.arena);
                    break;
                }
                {
                    u32 callee = site.call->callee;
                    u32 growth = cache.funcs[callee].insts;
                    bool forced = m->funcs[callee].always_inline;

                    if (!cache.direct_calls[callee])
                        CGF_ICE("inline: direct-call cache underflow");
                    cache.direct_calls[callee]--;
                    add_cloned_direct_calls(&cache, m, &m->funcs[callee]);
                    if (!forced)
                        m->funcs[fi].opt_inline_growth_left -= growth;
                }
                func_changed |= inline_site(
                    m, &m->funcs[fi], &m->funcs[site.call->callee], &site,
                    cache.funcs[site.call->callee].returns, &serial);
                arena_free_all(&loops.arena);
            }
            if (func_changed) {
                /* Value numbering is not consulted by inliner eligibility.
                 * Canonicalize once after the caller reaches its fixed point,
                 * instead of performing three whole-function walks per site. */
                ir_func_remove_unreachable(&m->funcs[fi]);
                ir_func_renumber(m->arena, &m->funcs[fi]);
                scan_func_facts(&cache, m, graph, fi, false);
                changed = true;
            }
        }
    }
    arena_free_all(&cache.arena);
    ipo_callgraph_free(graph);
    return changed;
}

bool opt_force_inline(IrModule *m, const OptConfig *cfg)
{
    return run_inline(m, cfg, true);
}

bool opt_inline(IrModule *m, const OptConfig *cfg)
{
    return run_inline(m, cfg, false);
}

/* Remove always-inline bodies that C's inline rules say are not external
 * definitions. They were admitted to emission IR only so always_inline had
 * something concrete to splice. Every internal call must be gone first;
 * symbol operands (address taking, indirect calls, relocations) deliberately
 * keep naming the external fallback and therefore need no function-index
 * remap. */
bool opt_strip_inline_only(IrModule *m, const OptConfig *cfg)
{
    bool *keep;
    u32 *map;
    u32 i, out = 0;
    bool changed = false;

    (void)cfg;
    if (diag_had_error(m->dc))
        return false;
    for (i = 0; i < m->nfuncs; i++) {
        u32 bi;

        for (bi = 0; bi < m->funcs[i].nblocks; bi++) {
            IrInst *in;

            for (in = m->funcs[i].blocks[bi].first; in; in = in->next)
                if (in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
                    in->callee < m->nfuncs &&
                    m->funcs[in->callee].inline_only &&
                    m->funcs[in->callee].always_inline) {
                    diag_emit(m->dc, DIAG_ERROR, ir_inst_span(m, in),
                              "inlining failed in call to 'always_inline' "
                              "'%s': function was not inlined",
                              m->funcs[in->callee].name);
                    return false;
                }
        }
    }
    keep = cgf_xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(*keep));
    map = cgf_xmalloc((m->nfuncs ? m->nfuncs : 1) * sizeof(*map));
    for (i = 0; i < m->nfuncs; i++) {
        keep[i] = !(m->funcs[i].inline_only && m->funcs[i].always_inline);
        changed |= !keep[i];
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
                        CGF_ICE("inline: stripped function still has a caller");
                    in->callee = map[in->callee];
                }
        }
    }
    free(keep);
    free(map);
    return true;
}

const Pass OPT_PASS_INLINE = {"inline", opt_inline, PASS_PINNED_INLINE_CLONES};
const Pass OPT_PASS_FORCE_INLINE = {"force-inline", opt_force_inline,
                                    PASS_PINNED_INLINE_CLONES};
const Pass OPT_PASS_STRIP_INLINE_ONLY = {
    "strip-inline-only", opt_strip_inline_only, PASS_PINNED_DELETE_FUNCS};
