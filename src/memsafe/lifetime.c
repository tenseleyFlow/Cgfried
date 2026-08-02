#include "memsafe/memsafe.h"

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "opt/opt.h"

typedef struct MsFact {
    MsState state;
    MsTrace trace;
    i32 realloc_old_site;
} MsFact;

typedef struct MsPredicate {
    IrOperand subject;
    i64 constant;
    bool equal;
} MsPredicate;

typedef struct MsPath {
    MsFact *facts;
    MsPredicate predicates[MS_MAX_PREDICATES_PER_PATH];
    u32 npredicates;
    bool correlations_lost;
} MsPath;

typedef struct MsPathSlot {
    MsPath path;
    u32 version;
    bool live;
} MsPathSlot;

typedef struct MsBlockPaths {
    MsPathSlot slots[MS_MAX_STATES_PER_BLOCK];
    u32 nslots;
} MsBlockPaths;

typedef struct MsWorkItem {
    u32 block;
    u32 slot;
    u32 version;
} MsWorkItem;

typedef struct MsWorklist {
    MsWorkItem *items;
    size_t head;
    size_t len;
    size_t cap;
} MsWorklist;

struct MsFunctionResult {
    Arena *arena;
    IrModule *module;
    IrFunc *function;
    AliasCtx *alias;
    const MsAllocFamily **families;
    u32 nsites;
    MsBlockPaths *blocks;
    MsPathSlot exits[MS_MAX_STATES_PER_BLOCK];
    u32 nexits;
    u32 splits;
    bool degraded;
    bool split_budget_exhausted;
};

static void work_push(MsWorklist *work, MsWorkItem item)
{
    if (work->len == work->cap) {
        size_t next = work->cap ? work->cap * 2 : 64;

        work->items = cgf_xrealloc(work->items, next * sizeof(*work->items));
        work->cap = next;
    }
    work->items[work->len++] = item;
}

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static MsPath path_new(MsFunctionResult *result)
{
    MsPath path = {0};
    u32 i;

    path.facts =
        arena_alloc(result->arena,
                    (result->nsites ? result->nsites : 1) * sizeof(*path.facts),
                    _Alignof(MsFact));
    for (i = 0; i < result->nsites; i++) {
        path.facts[i].state = MS_UNALLOCATED;
        ms_trace_init(&path.facts[i].trace, result->arena);
        path.facts[i].realloc_old_site = -1;
    }
    return path;
}

static MsPath path_clone(MsFunctionResult *result, const MsPath *source)
{
    MsPath path = *source;

    path.facts =
        arena_alloc(result->arena,
                    (result->nsites ? result->nsites : 1) * sizeof(*path.facts),
                    _Alignof(MsFact));
    if (result->nsites)
        memcpy(path.facts, source->facts, result->nsites * sizeof(*path.facts));
    return path;
}

static bool predicate_equal(MsPredicate a, MsPredicate b)
{
    return operand_equal(a.subject, b.subject) && a.constant == b.constant &&
           a.equal == b.equal;
}

static bool predicates_equal(const MsPath *a, const MsPath *b)
{
    u32 i;

    if (a->npredicates != b->npredicates)
        return false;
    for (i = 0; i < a->npredicates; i++)
        if (!predicate_equal(a->predicates[i], b->predicates[i]))
            return false;
    return true;
}

static bool path_equal(const MsFunctionResult *result, const MsPath *a,
                       const MsPath *b)
{
    u32 i;

    if (a->correlations_lost != b->correlations_lost || !predicates_equal(a, b))
        return false;
    for (i = 0; i < result->nsites; i++)
        if (a->facts[i].state != b->facts[i].state ||
            a->facts[i].realloc_old_site != b->facts[i].realloc_old_site)
            return false;
    return true;
}

static void trace_clear(MsFact *fact)
{
    Arena *arena = fact->trace.arena;

    ms_trace_init(&fact->trace, arena);
}

static void path_degrade_facts(MsFunctionResult *result, MsPath *path)
{
    u32 i;

    for (i = 0; i < result->nsites; i++) {
        path->facts[i].state = MS_UNKNOWN;
        trace_clear(&path->facts[i]);
        path->facts[i].realloc_old_site = -1;
    }
}

static bool path_add_predicate(MsFunctionResult *result, MsPath *path,
                               MsPredicate predicate)
{
    u32 i;

    for (i = 0; i < path->npredicates; i++) {
        MsPredicate old = path->predicates[i];

        if (!operand_equal(old.subject, predicate.subject))
            continue;
        if (old.constant != predicate.constant) {
            if (old.equal && predicate.equal)
                return false;
            continue;
        }
        return old.equal == predicate.equal;
    }
    if (path->npredicates == MS_MAX_PREDICATES_PER_PATH) {
        memmove(&path->predicates[0], &path->predicates[1],
                (MS_MAX_PREDICATES_PER_PATH - 1) * sizeof(path->predicates[0]));
        path->npredicates--;
        result->degraded = true;
        path->correlations_lost = true;
        path_degrade_facts(result, path);
    }
    path->predicates[path->npredicates++] = predicate;
    return true;
}

static bool path_join_into(MsFunctionResult *result, MsPath *dst,
                           const MsPath *src)
{
    bool keep_predicates = predicates_equal(dst, src);
    bool lose_correlations =
        dst->correlations_lost || src->correlations_lost || !keep_predicates;
    bool changed = !keep_predicates && dst->npredicates != 0;
    u32 i;

    if (!keep_predicates)
        dst->npredicates = 0;
    if (lose_correlations != dst->correlations_lost) {
        dst->correlations_lost = lose_correlations;
        changed = true;
    }
    for (i = 0; i < result->nsites; i++) {
        MsState joined = !lose_correlations ? ms_state_join(dst->facts[i].state,
                                                            src->facts[i].state)
                                            : MS_UNKNOWN;
        i32 realloc_old_site =
            !lose_correlations && dst->facts[i].realloc_old_site ==
                                      src->facts[i].realloc_old_site
                ? dst->facts[i].realloc_old_site
                : -1;

        if (joined != dst->facts[i].state) {
            dst->facts[i].state = joined;
            trace_clear(&dst->facts[i]);
            changed = true;
        }
        if (dst->facts[i].realloc_old_site != realloc_old_site) {
            dst->facts[i].realloc_old_site = realloc_old_site;
            changed = true;
        }
    }
    return changed;
}

static void enqueue_slot(MsWorklist *work, u32 block, u32 slot,
                         MsPathSlot *entry)
{
    entry->version++;
    work_push(work, (MsWorkItem){block, slot, entry->version});
}

static void force_join_block(MsFunctionResult *result, MsWorklist *work,
                             u32 block, MsBlockPaths *set,
                             const MsPath *incoming)
{
    bool changed;
    u32 i;

    if (set->nslots == 0) {
        set->slots[0].path = path_clone(result, incoming);
        set->slots[0].live = true;
        set->nslots = 1;
        enqueue_slot(work, block, 0, &set->slots[0]);
        return;
    }
    changed = path_join_into(result, &set->slots[0].path, incoming);
    for (i = 1; i < set->nslots; i++) {
        if (set->slots[i].live)
            changed |= path_join_into(result, &set->slots[0].path,
                                      &set->slots[i].path);
        set->slots[i].live = false;
        set->slots[i].version++;
    }
    set->nslots = 1;
    if (changed)
        enqueue_slot(work, block, 0, &set->slots[0]);
}

static void join_excess_block_path(MsFunctionResult *result, MsWorklist *work,
                                   u32 block, MsBlockPaths *set,
                                   const MsPath *incoming)
{
    if (path_join_into(result, &set->slots[0].path, incoming))
        enqueue_slot(work, block, 0, &set->slots[0]);
}

static void add_block_path(MsFunctionResult *result, MsWorklist *work,
                           u32 block, const MsPath *path)
{
    MsBlockPaths *set = &result->blocks[block];
    u32 i;

    if (result->split_budget_exhausted) {
        force_join_block(result, work, block, set, path);
        return;
    }
    for (i = 0; i < set->nslots; i++)
        if (set->slots[i].live && path_equal(result, &set->slots[i].path, path))
            return;
    if (set->nslots < MS_MAX_STATES_PER_BLOCK) {
        MsPathSlot *slot = &set->slots[set->nslots];

        slot->path = path_clone(result, path);
        slot->live = true;
        enqueue_slot(work, block, set->nslots, slot);
        set->nslots++;
        return;
    }
    result->degraded = true;
    join_excess_block_path(result, work, block, set, path);
}

static void add_exit_path(MsFunctionResult *result, const MsPath *path)
{
    u32 i;

    for (i = 0; i < result->nexits; i++)
        if (path_equal(result, &result->exits[i].path, path))
            return;
    if (result->nexits < MS_MAX_STATES_PER_BLOCK &&
        !result->split_budget_exhausted) {
        MsPathSlot *slot = &result->exits[result->nexits++];

        slot->path = path_clone(result, path);
        slot->live = true;
        return;
    }
    result->degraded = true;
    if (!result->split_budget_exhausted) {
        (void)path_join_into(result, &result->exits[0].path, path);
        return;
    }
    if (result->nexits == 0) {
        result->exits[0].path = path_clone(result, path);
        result->exits[0].live = true;
        result->nexits = 1;
    } else {
        (void)path_join_into(result, &result->exits[0].path, path);
        for (i = 1; i < result->nexits; i++) {
            (void)path_join_into(result, &result->exits[0].path,
                                 &result->exits[i].path);
            result->exits[i].live = false;
        }
        result->nexits = 1;
    }
}

static const char *call_name(const IrModule *module, const IrInst *call)
{
    if (!call || call->op != IR_CALL || call->subop != FUNCREF_EXTERNAL ||
        call->callee >= module->nsyms)
        return NULL;
    return module->syms[call->callee];
}

static u32 call_first_arg(const IrInst *call)
{
    return call->subop == FUNCREF_INDIRECT ? 1u : 0u;
}

static bool call_arg(const IrInst *call, u32 index, IrOperand *out)
{
    u32 first = call_first_arg(call);

    if (!call || first + index >= call->nops)
        return false;
    *out = call->ops[first + index];
    return true;
}

static bool operand_is_null(IrOperand operand)
{
    return operand.kind == IROP_ICONST && operand.a == 0;
}

static void degrade_candidate_sites(MsFunctionResult *result, MsPath *path,
                                    PtsSet pts)
{
    u32 i;

    for (i = 0; i < result->nsites; i++) {
        const AllocSite *site = alias_alloc_site_at(result->alias, i);

        if (!alias_pts_has_alloc_site(result->alias, pts, site))
            continue;
        path->facts[i].state = MS_UNKNOWN;
        path->facts[i].realloc_old_site = -1;
        trace_clear(&path->facts[i]);
    }
}

static i32 path_site_for_operand(MsFunctionResult *result, MsPath *path,
                                 IrOperand operand)
{
    PtsSet pts = alias_points_to(result->alias, operand);
    i32 found = -1;
    bool ambiguous = pts.has_unknown;
    u32 i;

    for (i = 0; i < result->nsites; i++) {
        const AllocSite *site = alias_alloc_site_at(result->alias, i);

        if (!alias_pts_has_alloc_site(result->alias, pts, site) ||
            path->facts[i].state == MS_UNALLOCATED)
            continue;
        if (found >= 0)
            ambiguous = true;
        else
            found = (i32)i;
    }
    if (ambiguous) {
        degrade_candidate_sites(result, path, pts);
        return -1;
    }
    return found;
}

static void transition_site(MsFunctionResult *result, MsPath *path, u32 site,
                            MsAction action, Span loc, MsEventKind kind,
                            const char *note)
{
    MsFact *fact = &path->facts[site];
    MsTransition step = ms_transition(fact->state, action, false);

    fact->state = step.state;
    ms_trace_push(&fact->trace, loc, kind, "%s", note);
    (void)result;
}

static void transition_members(MsFunctionResult *result, MsPath *path,
                               IrOperand operand, MsAction action, Span loc,
                               MsEventKind kind, const char *note)
{
    PtsSet pts = alias_points_to(result->alias, operand);
    u32 i;

    for (i = 0; i < result->nsites; i++) {
        const AllocSite *site = alias_alloc_site_at(result->alias, i);

        if (alias_pts_has_alloc_site(result->alias, pts, site))
            transition_site(result, path, i, action, loc, kind, note);
    }
}

static void transition_reachable(MsFunctionResult *result, MsPath *path,
                                 IrOperand operand, MsAction action, Span loc,
                                 MsEventKind kind, const char *note)
{
    const AllocSite **sites =
        cgf_xmalloc((result->nsites ? result->nsites : 1) * sizeof(*sites));
    u32 count = alias_reachable_alloc_sites(result->alias, operand, sites,
                                            result->nsites, NULL);
    u32 i;

    for (i = 0; i < count && i < result->nsites; i++) {
        u32 site = alias_alloc_site_id(sites[i]);

        if (site)
            transition_site(result, path, site - 1, action, loc, kind, note);
    }
    free(sites);
}

static const IrInst *value_def(const IrFunc *function, IrOperand operand)
{
    const IrValInfo *info;
    const IrInst *in;

    if (operand.kind != IROP_VALUE || operand.a == 0 ||
        operand.a > function->nvals)
        return NULL;
    info = &function->vals[operand.a - 1];
    if (info->def_kind != VDEF_INST || info->def_block.v == 0 ||
        info->def_block.v > function->nblocks)
        return NULL;
    for (in = function->blocks[info->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == operand.a)
            return in;
    return NULL;
}

static bool operand_constant(const IrFunc *function, IrOperand operand,
                             i64 *value, u32 depth)
{
    const IrInst *def;

    if (operand.kind == IROP_ICONST) {
        *value = (i64)operand.a;
        return true;
    }
    if (depth > function->nvals)
        return false;
    def = value_def(function, operand);
    if (!def || def->nops != 1)
        return false;
    if (def->op == IR_SEXT || def->op == IR_ZEXT || def->op == IR_TRUNC ||
        def->op == IR_BITCAST)
        return operand_constant(function, def->ops[0], value, depth + 1);
    return false;
}

static bool local_address(const IrFunc *function, IrOperand operand, u32 depth)
{
    const IrInst *def;

    if (depth > function->nvals)
        return false;
    def = value_def(function, operand);
    if (!def)
        return false;
    if (def->op == IR_ALLOCA)
        return true;
    if ((def->op == IR_PTRADD || def->op == IR_BITCAST) && def->nops >= 1)
        return local_address(function, def->ops[0], depth + 1);
    return false;
}

static void process_call(MsFunctionResult *result, MsPath *path,
                         const IrInst *call)
{
    const char *name = call_name(result->module, call);
    const MsAllocFamily *family = ms_alloc_family_lookup(name);
    const AllocSite *created = alias_alloc_site(result->alias, call);
    Span loc = ir_inst_span(result->module, call);
    IrOperand operand;
    u32 first, i;

    if (created) {
        u32 site = alias_alloc_site_id(created) - 1;
        MsEventKind kind =
            family && family->frees_on_success ? MS_EV_REALLOC : MS_EV_ALLOC;

        transition_site(result, path, site, MS_ACT_ALLOC, loc, kind,
                        family && family->frees_on_success ? "reallocated here"
                                                           : "allocated here");
        path->facts[site].realloc_old_site = -1;
        if (family && family->frees_on_success &&
            call_arg(call, family->frees_arg, &operand))
            path->facts[site].realloc_old_site =
                path_site_for_operand(result, path, operand);
    }
    if (family && !family->allocates && family->frees_arg != MS_NO_ARG &&
        call_arg(call, family->frees_arg, &operand)) {
        i32 site;

        if (operand_is_null(operand))
            return;
        site = path_site_for_operand(result, path, operand);
        if (site >= 0)
            transition_site(result, path, (u32)site, MS_ACT_FREE, loc,
                            MS_EV_FREE, "freed here");
        return;
    }
    if (family)
        return;
    first = call_first_arg(call);
    for (i = first; i < call->nops; i++)
        if (call->ops[i].type == IRT_PTR)
            transition_reachable(result, path, call->ops[i], MS_ACT_ESCAPE, loc,
                                 MS_EV_CALL, "passed to an unknown call here");
}

static void process_inst(MsFunctionResult *result, MsPath *path,
                         const IrInst *in)
{
    Span loc = ir_inst_span(result->module, in);

    switch (in->op) {
    case IR_CALL:
        process_call(result, path, in);
        break;
    case IR_LOAD:
        if (in->nops >= 1)
            transition_members(result, path, in->ops[0], MS_ACT_DEREF, loc,
                               MS_EV_USE, "read through pointer here");
        break;
    case IR_STORE:
        if (in->nops >= 2) {
            transition_members(result, path, in->ops[1], MS_ACT_DEREF, loc,
                               MS_EV_USE, "wrote through pointer here");
            if (in->ops[0].type == IRT_PTR &&
                !local_address(result->function, in->ops[1], 0))
                transition_reachable(result, path, in->ops[0], MS_ACT_ESCAPE,
                                     loc, MS_EV_ESCAPE,
                                     "stored into escaping memory here");
        }
        break;
    case IR_MEMCPY:
        if (in->nops >= 2) {
            transition_members(result, path, in->ops[0], MS_ACT_DEREF, loc,
                               MS_EV_USE, "wrote through pointer here");
            transition_members(result, path, in->ops[1], MS_ACT_DEREF, loc,
                               MS_EV_USE, "read through pointer here");
        }
        break;
    case IR_MEMSET:
        if (in->nops >= 1)
            transition_members(result, path, in->ops[0], MS_ACT_DEREF, loc,
                               MS_EV_USE, "wrote through pointer here");
        break;
    case IR_RET:
        if (in->nops == 1 && in->ops[0].type == IRT_PTR)
            transition_reachable(result, path, in->ops[0], MS_ACT_ESCAPE, loc,
                                 MS_EV_RETURN, "returned here");
        break;
    default:
        break;
    }
}

static bool decode_predicate(const IrFunc *function, const IrInst *term,
                             u32 edge, MsPredicate *predicate,
                             IrOperand *pointer, bool *pointer_nonnull)
{
    const IrInst *cmp;
    IrOperand subject;
    i64 constant;
    bool equal;

    memset(predicate, 0, sizeof(*predicate));
    memset(pointer, 0, sizeof(*pointer));
    *pointer_nonnull = false;
    if (term->op == IR_SWITCH && term->nops == 1 && edge > 0) {
        predicate->subject = term->ops[0];
        predicate->constant = term->edges[edge].case_val;
        predicate->equal = true;
        return predicate->subject.kind == IROP_VALUE;
    }
    if (term->op != IR_CONDBR || term->nops != 1 || edge > 1)
        return false;
    cmp = value_def(function, term->ops[0]);
    if (!cmp || cmp->op != IR_ICMP || cmp->nops != 2 ||
        (cmp->subop != ICMP_EQ && cmp->subop != ICMP_NE))
        return false;
    if (operand_constant(function, cmp->ops[0], &constant, 0)) {
        subject = cmp->ops[1];
    } else if (operand_constant(function, cmp->ops[1], &constant, 0)) {
        subject = cmp->ops[0];
    } else {
        return false;
    }
    if (subject.kind != IROP_VALUE)
        return false;
    equal = cmp->subop == ICMP_EQ;
    if (edge == 1)
        equal = !equal;
    predicate->subject = subject;
    predicate->constant = constant;
    predicate->equal = equal;
    if (subject.type == IRT_PTR && constant == 0) {
        *pointer = subject;
        *pointer_nonnull = !equal;
    }
    return true;
}

static bool refine_switch_default(MsFunctionResult *result, MsPath *path,
                                  const IrInst *term)
{
    u32 i;

    if (term->op != IR_SWITCH || term->nops != 1 ||
        term->ops[0].kind != IROP_VALUE)
        return true;
    for (i = 1; i < term->nedges; i++) {
        MsPredicate predicate = {
            term->ops[0],
            term->edges[i].case_val,
            false,
        };

        if (!path_add_predicate(result, path, predicate))
            return false;
    }
    return true;
}

static void refine_pointer_branch(MsFunctionResult *result, MsPath *path,
                                  IrOperand pointer, bool nonnull, Span loc)
{
    i32 site = path_site_for_operand(result, path, pointer);
    MsState before, after;

    if (site < 0)
        return;
    before = path->facts[site].state;
    if (!nonnull && before != MS_UNKNOWN)
        after = MS_UNALLOCATED;
    else if (nonnull && before == MS_UNALLOCATED)
        after = MS_UNKNOWN;
    else
        after = before;
    path->facts[site].state = after;
    if (after == MS_UNKNOWN && before != MS_UNKNOWN)
        trace_clear(&path->facts[site]);
    ms_trace_push(&path->facts[site].trace, loc, MS_EV_BRANCH,
                  nonnull ? "pointer is non-null on this branch"
                          : "pointer is null on this branch");
    if (result->families[site] && result->families[site]->frees_on_success &&
        path->facts[site].realloc_old_site >= 0) {
        u32 old = (u32)path->facts[site].realloc_old_site;

        if (nonnull && path->facts[old].state == MS_ALLOCATED)
            transition_site(result, path, old, MS_ACT_FREE, loc, MS_EV_REALLOC,
                            "old pointer freed when realloc succeeded");
    }
}

static u32 feasible_edge_count(const IrFunc *function, const IrInst *term)
{
    u32 count = 0, i;

    for (i = 0; i < term->nedges; i++)
        if (opt_cfg_edge_feasible(function, term, i))
            count++;
    return count;
}

static void propagate_successors(MsFunctionResult *result, MsWorklist *work,
                                 const MsPath *source, const IrInst *term)
{
    u32 feasible = feasible_edge_count(result->function, term);
    Span loc = ir_inst_span(result->module, term);
    u32 i;

    if (feasible > 1) {
        u32 extra = feasible - 1;

        if (extra > MS_MAX_SPLITS_PER_FUNCTION - result->splits) {
            result->degraded = true;
            result->split_budget_exhausted = true;
            result->splits = MS_MAX_SPLITS_PER_FUNCTION;
        } else {
            result->splits += extra;
        }
    }
    for (i = 0; i < term->nedges; i++) {
        MsPath path;
        MsPredicate predicate;
        IrOperand pointer;
        bool pointer_nonnull;
        u32 target;

        if (!opt_cfg_edge_feasible(result->function, term, i))
            continue;
        target = term->edges[i].target.v;
        if (!target || target > result->function->nblocks)
            continue;
        path = path_clone(result, source);
        if (result->split_budget_exhausted) {
            path.correlations_lost = true;
            path_degrade_facts(result, &path);
        }
        if (!result->split_budget_exhausted && i == 0 &&
            !refine_switch_default(result, &path, term))
            continue;
        if (!result->split_budget_exhausted &&
            decode_predicate(result->function, term, i, &predicate, &pointer,
                             &pointer_nonnull)) {
            if (!path_add_predicate(result, &path, predicate))
                continue;
            if (pointer.kind != IROP_NONE)
                refine_pointer_branch(result, &path, pointer, pointer_nonnull,
                                      loc);
        }
        add_block_path(result, work, target - 1, &path);
    }
}

MsFunctionResult *ms_analyze_function(Arena *arena, IrModule *module,
                                      IrFunc *function, bool no_strict_aliasing)
{
    MsFunctionResult *result;
    AliasAllocSeed *seeds = NULL;
    AliasConfig config = {0};
    MsWorklist work = {0};
    MsPath initial;
    u32 nseeds;
    u32 i;

    if (!arena || !module || !function)
        CGF_ICE("ms_analyze_function: arena, module, and function required");
    result = arena_alloc(arena, sizeof(*result), _Alignof(MsFunctionResult));
    memset(result, 0, sizeof(*result));
    result->arena = arena;
    result->module = module;
    result->function = function;
    nseeds = ms_alias_alloc_seeds(arena, module, function, &seeds);
    config.func = function;
    config.no_strict_aliasing = no_strict_aliasing;
    config.alloc_seeds = seeds;
    config.nalloc_seeds = nseeds;
    result->alias = alias_build(module, &config);
    result->nsites = alias_alloc_site_count(result->alias);
    result->families = arena_alloc(arena,
                                   (result->nsites ? result->nsites : 1) *
                                       sizeof(*result->families),
                                   _Alignof(const MsAllocFamily *));
    for (i = 0; i < result->nsites; i++) {
        const IrInst *call =
            alias_alloc_site_call(alias_alloc_site_at(result->alias, i));

        result->families[i] = ms_alloc_family_lookup(call_name(module, call));
    }
    result->blocks = arena_alloc(arena,
                                 (function->nblocks ? function->nblocks : 1) *
                                     sizeof(*result->blocks),
                                 _Alignof(MsBlockPaths));
    memset(result->blocks, 0, function->nblocks * sizeof(*result->blocks));
    if (function->nblocks) {
        initial = path_new(result);
        add_block_path(result, &work, 0, &initial);
    }
    while (work.head < work.len) {
        MsWorkItem item = work.items[work.head++];
        MsBlockPaths *set;
        MsPathSlot *slot;
        MsPath path;
        const IrInst *in;

        if (item.block >= function->nblocks)
            continue;
        set = &result->blocks[item.block];
        if (item.slot >= set->nslots)
            continue;
        slot = &set->slots[item.slot];
        if (!slot->live || slot->version != item.version)
            continue;
        path = path_clone(result, &slot->path);
        for (in = function->blocks[item.block].first; in; in = in->next) {
            process_inst(result, &path, in);
            if (path.correlations_lost)
                path_degrade_facts(result, &path);
        }
        in = function->blocks[item.block].last;
        if (!in || in->op == IR_RET)
            add_exit_path(result, &path);
        else if (in->nedges)
            propagate_successors(result, &work, &path, in);
    }
    free(work.items);
    return result;
}

void ms_result_free(MsFunctionResult *result)
{
    if (result && result->alias) {
        alias_free(result->alias);
        result->alias = NULL;
    }
}

bool ms_result_degraded(const MsFunctionResult *result)
{
    return result && result->degraded;
}

u32 ms_result_split_count(const MsFunctionResult *result)
{
    return result ? result->splits : 0;
}

u32 ms_result_block_state_count(const MsFunctionResult *result, u32 block_index)
{
    return result && block_index < result->function->nblocks
               ? result->blocks[block_index].nslots
               : 0;
}

u32 ms_result_exit_count(const MsFunctionResult *result)
{
    return result ? result->nexits : 0;
}

MsState ms_result_exit_state(const MsFunctionResult *result, u32 exit_index,
                             u32 site_index)
{
    if (!result || exit_index >= result->nexits || site_index >= result->nsites)
        return MS_UNKNOWN;
    return result->exits[exit_index].path.facts[site_index].state;
}

const MsTrace *ms_result_exit_trace(const MsFunctionResult *result,
                                    u32 exit_index, u32 site_index)
{
    if (!result || exit_index >= result->nexits || site_index >= result->nsites)
        return NULL;
    return &result->exits[exit_index].path.facts[site_index].trace;
}

static const char *event_name(MsEventKind kind)
{
    static const char *const names[] = {
        "alloc", "free", "realloc", "escape", "use", "branch", "call", "return",
    };

    return (u32)kind < CGF_ARRAY_LEN(names) ? names[kind] : "unknown";
}

void ms_dump_module(IrModule *module, bool no_strict_aliasing, FILE *out)
{
    Arena analysis;
    u32 fi;

    if (!module || !out)
        return;
    arena_init(&analysis);
    for (fi = 0; fi < module->nfuncs; fi++) {
        MsFunctionResult *result = ms_analyze_function(
            &analysis, module, &module->funcs[fi], no_strict_aliasing);
        u32 bi, si, ei;

        fprintf(out, "memsafe function=%s sites=%u splits=%u degraded=%s\n",
                result->function->name, result->nsites, result->splits,
                result->degraded ? "may" : "no");
        for (bi = 0; bi < result->function->nblocks; bi++)
            fprintf(out, "block=%u name=%s states=%u\n", bi,
                    result->function->blocks[bi].name,
                    result->blocks[bi].nslots);
        for (si = 0; si < result->nsites; si++) {
            const AllocSite *site = alias_alloc_site_at(result->alias, si);
            const IrInst *call = alias_alloc_site_call(site);
            const char *name = call_name(module, call);

            for (ei = 0; ei < result->nexits; ei++) {
                const MsTrace *trace = &result->exits[ei].path.facts[si].trace;
                u32 ti;

                fprintf(out, "site=%u callee=%s exit=%u state=%s\n",
                        alias_alloc_site_id(site), name ? name : "?", ei,
                        ms_state_name(result->exits[ei].path.facts[si].state));
                for (ti = 0; ti < trace->len; ti++) {
                    MsEvent event;
                    u32 line, col;

                    if (!ms_trace_event(trace, ti, &event))
                        continue;
                    line = event.loc.presumed_line ? event.loc.presumed_line
                                                   : event.loc.line;
                    col = event.loc.col;
                    fprintf(out,
                            "trace site=%u exit=%u event=%s line=%u col=%u "
                            "note=%s\n",
                            alias_alloc_site_id(site), ei,
                            event_name(event.kind), line, col, event.note);
                }
            }
        }
        ms_result_free(result);
    }
    arena_free_all(&analysis);
}
