#include "opt/opt.h"

#include <stdio.h>
#include <string.h>

#include "util/arena.h"

typedef struct {
    u32 block;
    IrInst *call;
    IrInst *prev;
} CallSite;

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

static u32 inst_count(const IrFunc *f)
{
    u32 i, n = 0;

    for (i = 0; i < f->nblocks; i++)
        n += f->blocks[i].ninsts;
    return n;
}

static bool contains_op(const IrFunc *f, IrOp op)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == op)
                return true;
    }
    return false;
}

static bool arg_feeds_control(const IrFunc *f, u32 arg)
{
    u32 bi;
    ValueId param = f->param_vals[arg];

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in = f->blocks[bi].last;

        if (in && (in->op == IR_CONDBR || in->op == IR_SWITCH) && in->nops &&
            in->ops[0].kind == IROP_VALUE && in->ops[0].a == param.v)
            return true;
    }
    return false;
}

static u32 direct_call_count(const IrModule *m, u32 target)
{
    u32 fi, bi, n = 0;

    for (fi = 0; fi < m->nfuncs; fi++)
        for (bi = 0; bi < m->funcs[fi].nblocks; bi++) {
            const IrInst *in;

            for (in = m->funcs[fi].blocks[bi].first; in; in = in->next)
                if (in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
                    in->callee == target)
                    n++;
        }
    return n;
}

static bool block_in_loop(Arena *scratch, const IrFunc *f, u32 block)
{
    bool *seen;
    u32 *work;
    u32 nwork = 0;
    const IrInst *term;
    u32 ei;

    if (block >= f->nblocks)
        return false;
    seen = arena_alloc(scratch, f->nblocks * sizeof(*seen), _Alignof(bool));
    work = arena_alloc(scratch, f->nblocks * sizeof(*work), _Alignof(u32));
    memset(seen, 0, f->nblocks * sizeof(*seen));
    term = f->blocks[block].last;
    if (!term)
        return false;
    /* The call block belongs to a cyclic CFG SCC iff one of its successors
     * can reach it.  This covers natural, self, and irreducible loops; the
     * last class has no dominance-classified retreating edge but repeats an
     * inlined alloca just as surely. */
    for (ei = 0; ei < term->nedges; ei++) {
        u32 target = term->edges[ei].target.v;

        if (target == block + 1)
            return true;
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

            if (target == block + 1)
                return true;
            if (target && target <= f->nblocks && !seen[target - 1]) {
                seen[target - 1] = true;
                work[nwork++] = target - 1;
            }
        }
    }
    return false;
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
    /* Operand.b on a value is a use-site ABI annotation.  Parameter
     * substitution must not leak the outer call's annotation into ordinary
     * callee uses, while a nested byval/sret call keeps its own annotation. */
    if (out.kind != IROP_FCONST)
        out.b = old.b;
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

            for (i = 0; i < in->nops; i++)
                if (in->ops[i].kind == IROP_VALUE && in->ops[i].a == old.v) {
                    u64 annot = in->ops[i].b;

                    in->ops[i] = replacement;
                    if (replacement.kind != IROP_FCONST)
                        in->ops[i].b = annot;
                }
            for (i = 0; i < in->nedges; i++)
                for (j = 0; j < in->edges[i].nargs; j++)
                    if (in->edges[i].args[j].kind == IROP_VALUE &&
                        in->edges[i].args[j].a == old.v) {
                        u64 annot = in->edges[i].args[j].b;

                        in->edges[i].args[j] = replacement;
                        if (replacement.kind != IROP_FCONST)
                            in->edges[i].args[j].b = annot;
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

static u32 count_returns(const IrFunc *f)
{
    u32 bi, n = 0;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            n += in->op == IR_RET;
    }
    return n;
}

static bool inline_site(IrModule *m, IrFunc *caller, const IrFunc *callee,
                        const CallSite *site, u32 *next_serial)
{
    IrOperand *map;
    BlockId *blocks;
    IrInst *suffix = site->call->next;
    u32 suffix_count = 0;
    u32 returns = count_returns(callee);
    u32 serial =
        choose_serial(caller, callee->nblocks, returns > 1, next_serial);
    BlockId join = BLOCK_INVALID;
    ValueId join_value = VALUE_INVALID;
    IrOperand single_result = {0};
    IrBlock *call_block;
    u32 bi, i;

    for (IrInst *in = suffix; in; in = in->next)
        suffix_count++;
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
    ir_func_remove_unreachable(caller);
    ir_func_renumber(m->arena, caller);
    return true;
}

static bool eligible(IrModule *m, u32 caller_index, const CallSite *site,
                     const Callgraph *graph, const OptConfig *cfg,
                     Arena *scratch)
{
    const IrFunc *caller = &m->funcs[caller_index];
    const IrInst *call = site->call;
    const IrFunc *callee;
    OptConfig fc = *cfg;
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
    /* The current DWARF backend has no abstract-origin/inlined-subroutine
     * records.  Splicing a debug build would silently erase source-level
     * breakpoint and backtrace frames promised by Sprint 29. */
    if (cfg->debug_info) {
        OPT_BAIL(&fc, "inline", "inl_debug_info");
        return false;
    }
    if (callee->variadic && contains_op(callee, IR_VA_START)) {
        OPT_BAIL(&fc, "inline", "inl_va_start");
        return false;
    }
    /* A no-prototype call may omit body parameters or pass differently
     * promoted types.  Mapping the callee's concrete parameter SSA values
     * onto such operands would either index past the call or change C's
     * old-style ABI semantics. */
    if (callee->unprototyped) {
        OPT_BAIL(&fc, "inline", "inl_unprototyped_signature");
        return false;
    }
    if (caller->calls_setjmp || callee->calls_setjmp) {
        OPT_BAIL(&fc, "inline", "inl_setjmp");
        return false;
    }
    /* A no-return splice would make the caller suffix unreachable.  Leave it
     * intact until the pinned-operation audit can distinguish intentionally
     * dead suffixes from transformations that lose observable operations. */
    if (count_returns(callee) == 0) {
        OPT_BAIL(&fc, "inline", "inl_noreturn");
        return false;
    }
    /* A recursive callee cannot be spliced even into a caller outside its
     * SCC: the cloned recursive edge would otherwise become a fresh eligible
     * site on the next scan/fixpoint iteration and expand without bound. */
    if (recursive_node(graph, call->callee) ||
        ipo_callgraph_scc_of(graph, caller_index) ==
            ipo_callgraph_scc_of(graph, call->callee)) {
        OPT_BAIL(&fc, "inline", "inl_recursion");
        return false;
    }
    if (contains_op(callee, IR_ALLOCA) &&
        block_in_loop(scratch, caller, site->block)) {
        OPT_BAIL(&fc, "inline", "inl_alloca_in_loop");
        return false;
    }
    threshold = cfg->inline_threshold;
    if (callee->linkage == IRLINK_INTERNAL &&
        direct_call_count(m, call->callee) == 1 &&
        !ipo_callgraph_address_taken(graph, call->callee))
        threshold *= 4;
    for (i = 0; i < callee->nparams && i < call->nops; i++)
        if (call->ops[i].kind != IROP_VALUE &&
            call->ops[i].kind != IROP_UNDEF && arg_feeds_control(callee, i))
            threshold += cfg->level == OPT_OS ? 5 : 10;
    if (inst_count(callee) > threshold) {
        OPT_BAIL(&fc, "inline", "inl_cost");
        return false;
    }
    return true;
}

bool opt_inline(IrModule *m, const OptConfig *cfg)
{
    Callgraph *graph;
    bool changed = false;
    u32 oi, nsccs;

    graph = ipo_callgraph_build(m);
    nsccs = ipo_callgraph_scc_count(graph);
    for (oi = 0; oi < nsccs; oi++) {
        u32 scc = ipo_callgraph_bottom_up_scc(graph, oi);
        u32 mi;

        for (mi = 0; mi < ipo_callgraph_scc_size(graph, scc); mi++) {
            u32 fi = ipo_callgraph_scc_member(graph, scc, mi);
            u32 serial = 0;

            for (;;) {
                CallSite site;
                bool found_eligible = false;
                u32 bi;

                /* Scan every call in document order, logging every named bail,
                 * and commit the first eligible site before restarting because
                 * block compaction invalidates saved block coordinates. */
                for (bi = 0; bi < m->funcs[fi].nblocks && !found_eligible;
                     bi++) {
                    IrInst *prev = NULL;
                    IrInst *in;

                    for (in = m->funcs[fi].blocks[bi].first; in;
                         prev = in, in = in->next) {
                        Arena legal_scratch;

                        if (in->op != IR_CALL)
                            continue;
                        site.block = bi;
                        site.call = in;
                        site.prev = prev;
                        arena_init(&legal_scratch);
                        found_eligible =
                            eligible(m, fi, &site, graph, cfg, &legal_scratch);
                        arena_free_all(&legal_scratch);
                        if (found_eligible)
                            break;
                    }
                }
                if (!found_eligible)
                    break;
                changed |=
                    inline_site(m, &m->funcs[fi], &m->funcs[site.call->callee],
                                &site, &serial);
            }
        }
    }
    ipo_callgraph_free(graph);
    return changed;
}

const Pass OPT_PASS_INLINE = {"inline", opt_inline, PASS_PINNED_INLINE_CLONES};
