#include "memsafe/memsafe.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "opt/dep.h"
#include "opt/opt.h"
#include "warn/warn.h"

typedef enum MsInitKind {
    MS_INIT_NONE,
    MS_INIT_RANGE,
    MS_INIT_FULL,
    /* Some write may have happened, but its footprint is not provable.
     * Widen toward silence rather than inventing an uninitialized byte. */
    MS_INIT_UNKNOWN
} MsInitKind;

#define MS_MAX_INIT_RANGES 8u
#define MS_MAX_BINDINGS_PER_PATH 8u

typedef struct MsInitRange {
    i64 lo;
    i64 hi;
} MsInitRange;

typedef struct MsFact {
    MsState state;
    MsTrace trace;
    i32 realloc_old_site;
    bool realloc_pending;
    MsAllocSuccess alloc_success;
    u32 alloc_status_value;
    bool extent_known;
    u64 extent;
    MsInitKind init;
    u8 ninit_ranges;
    MsInitRange init_ranges[MS_MAX_INIT_RANGES];
} MsFact;

typedef struct MsPredicate {
    IrOperand subject;
    i64 constant;
    bool equal;
} MsPredicate;

typedef struct MsBinding {
    ValueId param;
    IrOperand incoming;
} MsBinding;

typedef struct MsPath {
    MsFact *facts;
    MsPredicate predicates[MS_MAX_PREDICATES_PER_PATH];
    u32 npredicates;
    MsBinding bindings[MS_MAX_BINDINGS_PER_PATH];
    u32 nbindings;
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
    const MsSummarySet *summaries;
    AliasCtx *alias;
    const MsAllocFamily **families;
    u32 nsites;
    MsBlockPaths *blocks;
    MsPathSlot exits[MS_MAX_STATES_PER_BLOCK];
    u32 nexits;
    u32 splits;
    bool degraded;
    bool split_budget_exhausted;
    MsIssue *issues;
    u32 nissues;
    u32 issue_cap;
};

static void init_reset(MsFact *fact, MsInitKind kind)
{
    fact->init = kind;
    fact->ninit_ranges = 0;
}

static bool init_equal(const MsFact *a, const MsFact *b)
{
    return a->init == b->init && a->ninit_ranges == b->ninit_ranges &&
           (!a->ninit_ranges ||
            memcmp(a->init_ranges, b->init_ranges,
                   a->ninit_ranges * sizeof(a->init_ranges[0])) == 0);
}

static void init_add_range(MsFact *fact, i64 lo, i64 hi)
{
    u32 first, last, i;

    if (hi <= lo || fact->init == MS_INIT_FULL || fact->init == MS_INIT_UNKNOWN)
        return;
    first = 0;
    while (first < fact->ninit_ranges && fact->init_ranges[first].hi < lo)
        first++;
    last = first;
    while (last < fact->ninit_ranges && fact->init_ranges[last].lo <= hi) {
        if (fact->init_ranges[last].lo < lo)
            lo = fact->init_ranges[last].lo;
        if (fact->init_ranges[last].hi > hi)
            hi = fact->init_ranges[last].hi;
        last++;
    }
    if (first == last && fact->ninit_ranges == MS_MAX_INIT_RANGES) {
        init_reset(fact, MS_INIT_UNKNOWN);
        return;
    }
    if (last > first) {
        memmove(&fact->init_ranges[first], &fact->init_ranges[last],
                (fact->ninit_ranges - last) * sizeof(fact->init_ranges[0]));
        fact->ninit_ranges -= last - first;
    }
    if (fact->ninit_ranges == MS_MAX_INIT_RANGES) {
        init_reset(fact, MS_INIT_UNKNOWN);
        return;
    }
    memmove(&fact->init_ranges[first + 1], &fact->init_ranges[first],
            (fact->ninit_ranges - first) * sizeof(fact->init_ranges[0]));
    fact->init_ranges[first] = (MsInitRange){lo, hi};
    fact->ninit_ranges++;
    fact->init = MS_INIT_RANGE;
    for (i = 1; i < fact->ninit_ranges; i++)
        if (fact->init_ranges[i - 1].hi >= fact->init_ranges[i].lo)
            CGF_ICE("memsafe initialization ranges are not normalized");
}

static void init_remove_range(MsFact *fact, i64 lo, i64 hi)
{
    MsInitRange next[MS_MAX_INIT_RANGES];
    u32 n = 0, i;

    if (hi <= lo || fact->init == MS_INIT_NONE || fact->init == MS_INIT_UNKNOWN)
        return;
    if (fact->init == MS_INIT_FULL) {
        if (!fact->extent_known || fact->extent > (u64)INT64_MAX) {
            init_reset(fact, MS_INIT_UNKNOWN);
            return;
        }
        fact->init = MS_INIT_RANGE;
        fact->ninit_ranges = 1;
        fact->init_ranges[0] = (MsInitRange){0, (i64)fact->extent};
    }
    for (i = 0; i < fact->ninit_ranges; i++) {
        MsInitRange old = fact->init_ranges[i];

        if (old.hi <= lo || old.lo >= hi) {
            next[n++] = old;
            continue;
        }
        if (old.lo < lo)
            next[n++] = (MsInitRange){old.lo, lo};
        if (old.hi > hi) {
            if (n == MS_MAX_INIT_RANGES) {
                init_reset(fact, MS_INIT_UNKNOWN);
                return;
            }
            next[n++] = (MsInitRange){hi, old.hi};
        }
    }
    fact->ninit_ranges = (u8)n;
    if (n)
        memcpy(fact->init_ranges, next, n * sizeof(next[0]));
    fact->init = n ? MS_INIT_RANGE : MS_INIT_NONE;
}

static void init_join(MsFact *out, const MsFact *a, const MsFact *b)
{
    u32 i;

    if (a->init == MS_INIT_UNKNOWN || b->init == MS_INIT_UNKNOWN) {
        init_reset(out, MS_INIT_UNKNOWN);
        return;
    }
    if (a->init == MS_INIT_FULL || b->init == MS_INIT_FULL) {
        init_reset(out, MS_INIT_FULL);
        return;
    }
    init_reset(out, MS_INIT_NONE);
    for (i = 0; i < a->ninit_ranges && out->init != MS_INIT_UNKNOWN; i++)
        init_add_range(out, a->init_ranges[i].lo, a->init_ranges[i].hi);
    for (i = 0; i < b->ninit_ranges && out->init != MS_INIT_UNKNOWN; i++)
        init_add_range(out, b->init_ranges[i].lo, b->init_ranges[i].hi);
}

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
    size_t nfacts = result->nsites ? result->nsites : 1;
    u32 i;

    path.facts = arena_alloc(result->arena, nfacts * sizeof(*path.facts),
                             _Alignof(MsFact));
    memset(path.facts, 0, nfacts * sizeof(*path.facts));
    for (i = 0; i < result->nsites; i++) {
        path.facts[i].state = MS_UNALLOCATED;
        ms_trace_init(&path.facts[i].trace, result->arena);
        path.facts[i].realloc_old_site = -1;
        path.facts[i].realloc_pending = false;
        path.facts[i].alloc_success = MS_ALLOC_SUCCESS_DIRECT;
        path.facts[i].alloc_status_value = 0;
        init_reset(&path.facts[i], MS_INIT_NONE);
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

static bool bindings_equal(const MsPath *a, const MsPath *b)
{
    u32 i;

    if (a->nbindings != b->nbindings)
        return false;
    for (i = 0; i < a->nbindings; i++)
        if (a->bindings[i].param.v != b->bindings[i].param.v ||
            !operand_equal(a->bindings[i].incoming, b->bindings[i].incoming))
            return false;
    return true;
}

static IrOperand path_resolve_binding(const MsPath *path, IrOperand operand)
{
    u32 depth;

    for (depth = 0; depth <= path->nbindings; depth++) {
        u32 i;

        if (operand.kind != IROP_VALUE)
            break;
        for (i = 0; i < path->nbindings; i++)
            if (path->bindings[i].param.v == operand.a) {
                operand = path->bindings[i].incoming;
                break;
            }
        if (i == path->nbindings)
            break;
    }
    return operand;
}

static bool path_equal(const MsFunctionResult *result, const MsPath *a,
                       const MsPath *b)
{
    u32 i;

    if (a->correlations_lost != b->correlations_lost ||
        !predicates_equal(a, b) || !bindings_equal(a, b))
        return false;
    for (i = 0; i < result->nsites; i++)
        if (a->facts[i].state != b->facts[i].state ||
            a->facts[i].realloc_old_site != b->facts[i].realloc_old_site ||
            a->facts[i].realloc_pending != b->facts[i].realloc_pending ||
            a->facts[i].alloc_success != b->facts[i].alloc_success ||
            a->facts[i].alloc_status_value != b->facts[i].alloc_status_value ||
            a->facts[i].extent_known != b->facts[i].extent_known ||
            (a->facts[i].extent_known &&
             a->facts[i].extent != b->facts[i].extent) ||
            !init_equal(&a->facts[i], &b->facts[i]))
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

    path->nbindings = 0;
    for (i = 0; i < result->nsites; i++) {
        path->facts[i].state = MS_UNKNOWN;
        trace_clear(&path->facts[i]);
        path->facts[i].realloc_old_site = -1;
        path->facts[i].realloc_pending = false;
        path->facts[i].alloc_success = MS_ALLOC_SUCCESS_DIRECT;
        path->facts[i].alloc_status_value = 0;
        init_reset(&path->facts[i], MS_INIT_UNKNOWN);
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
    bool keep_bindings = bindings_equal(dst, src);
    bool lose_correlations = dst->correlations_lost || src->correlations_lost ||
                             !keep_predicates || !keep_bindings;
    bool changed = (!keep_predicates && dst->npredicates != 0) ||
                   (!keep_bindings && dst->nbindings != 0);
    u32 i;

    if (!keep_predicates)
        dst->npredicates = 0;
    if (!keep_bindings)
        dst->nbindings = 0;
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
        bool realloc_pending =
            !lose_correlations && dst->facts[i].realloc_pending &&
            src->facts[i].realloc_pending &&
            dst->facts[i].realloc_old_site == src->facts[i].realloc_old_site;
        MsAllocSuccess alloc_success =
            !lose_correlations &&
                    dst->facts[i].alloc_success ==
                        src->facts[i].alloc_success &&
                    dst->facts[i].alloc_status_value ==
                        src->facts[i].alloc_status_value
                ? dst->facts[i].alloc_success
                : MS_ALLOC_SUCCESS_DIRECT;
        u32 alloc_status_value = alloc_success != MS_ALLOC_SUCCESS_DIRECT
                                     ? dst->facts[i].alloc_status_value
                                     : 0;
        MsFact joined_init = {0};
        bool extent_known = dst->facts[i].extent_known &&
                            src->facts[i].extent_known &&
                            dst->facts[i].extent == src->facts[i].extent;
        u64 extent = extent_known ? dst->facts[i].extent : 0;

        if (lose_correlations)
            init_reset(&joined_init, MS_INIT_UNKNOWN);
        else
            init_join(&joined_init, &dst->facts[i], &src->facts[i]);

        if (joined != dst->facts[i].state) {
            dst->facts[i].state = joined;
            trace_clear(&dst->facts[i]);
            changed = true;
        }
        if (dst->facts[i].realloc_old_site != realloc_old_site) {
            dst->facts[i].realloc_old_site = realloc_old_site;
            changed = true;
        }
        if (dst->facts[i].realloc_pending != realloc_pending) {
            dst->facts[i].realloc_pending = realloc_pending;
            changed = true;
        }
        if (dst->facts[i].alloc_success != alloc_success ||
            dst->facts[i].alloc_status_value != alloc_status_value) {
            dst->facts[i].alloc_success = alloc_success;
            dst->facts[i].alloc_status_value = alloc_status_value;
            changed = true;
        }
        if (dst->facts[i].extent_known != extent_known ||
            dst->facts[i].extent != extent) {
            dst->facts[i].extent_known = extent_known;
            dst->facts[i].extent = extent;
            changed = true;
        }
        if (!init_equal(&dst->facts[i], &joined_init)) {
            dst->facts[i].init = joined_init.init;
            dst->facts[i].ninit_ranges = joined_init.ninit_ranges;
            memcpy(dst->facts[i].init_ranges, joined_init.init_ranges,
                   joined_init.ninit_ranges *
                       sizeof(joined_init.init_ranges[0]));
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

static int issue_compare(const MsIssue *a, const MsIssue *b)
{
    u32 a_priority = a->kind == MS_ISSUE_LEAK ? MS_ISSUE_COUNT : a->kind;
    u32 b_priority = b->kind == MS_ISSUE_LEAK ? MS_ISSUE_COUNT : b->kind;

    if (a->loc.seq != b->loc.seq)
        return a->loc.seq < b->loc.seq ? -1 : 1;
    if (a->loc.file_id != b->loc.file_id)
        return a->loc.file_id < b->loc.file_id ? -1 : 1;
    if (a->loc.line != b->loc.line)
        return a->loc.line < b->loc.line ? -1 : 1;
    if (a->loc.col != b->loc.col)
        return a->loc.col < b->loc.col ? -1 : 1;
    if (a_priority != b_priority)
        return a_priority < b_priority ? -1 : 1;
    if (a->site_id != b->site_id)
        return a->site_id < b->site_id ? -1 : 1;
    if (a->strict != b->strict)
        return a->strict ? 1 : -1;
    return 0;
}

static void add_issue(MsFunctionResult *result, const MsPath *path,
                      MsIssueKind kind, Span loc, i32 site_index, bool strict,
                      const MsTrace *trace)
{
    MsIssue issue;
    u32 at;

    if (path && path->correlations_lost)
        return;
    memset(&issue, 0, sizeof(issue));
    issue.kind = kind;
    issue.loc = loc;
    issue.site_id = site_index >= 0 ? alias_alloc_site_id(alias_alloc_site_at(
                                          result->alias, (u32)site_index))
                                    : 0;
    issue.strict = strict;
    if (site_index >= 0 && (u32)site_index < result->nsites &&
        result->families && result->families[site_index])
        issue.file_resource = result->families[site_index]->is_file_resource;
    if (trace)
        issue.trace = *trace;
    else
        ms_trace_init(&issue.trace, result->arena);
    for (at = 0; at < result->nissues; at++) {
        int cmp = issue_compare(&issue, &result->issues[at]);

        if (cmp == 0)
            return;
        if (cmp < 0)
            break;
    }
    if (result->nissues == result->issue_cap) {
        u32 cap = result->issue_cap ? result->issue_cap * 2 : 16;

        result->issues =
            cgf_xrealloc(result->issues, cap * sizeof(*result->issues));
        result->issue_cap = cap;
    }
    memmove(&result->issues[at + 1], &result->issues[at],
            (result->nissues - at) * sizeof(*result->issues));
    result->issues[at] = issue;
    result->nissues++;
}

static MsOutcome transition_site(MsFunctionResult *result, MsPath *path,
                                 u32 site, MsAction action, Span loc,
                                 MsEventKind kind, const char *note, bool exact)
{
    MsFact *fact = &path->facts[site];
    MsTransition step = ms_transition(fact->state, action, false);

    if (exact && step.outcome == MS_OUTCOME_DOUBLE_FREE)
        add_issue(result, path, MS_ISSUE_DOUBLE_FREE, loc, (i32)site, false,
                  &fact->trace);
    else if (exact && step.outcome == MS_OUTCOME_UAF)
        add_issue(result, path, MS_ISSUE_USE_AFTER_FREE, loc, (i32)site, false,
                  &fact->trace);
    else if (exact && step.outcome == MS_OUTCOME_UAF_ESCAPE)
        add_issue(result, path, MS_ISSUE_USE_AFTER_FREE, loc, (i32)site, true,
                  &fact->trace);
    fact->state = step.state;
    ms_trace_push(&fact->trace, loc, kind, "%s", note);
    return step.outcome;
}

static void transition_members(MsFunctionResult *result, MsPath *path,
                               IrOperand operand, MsAction action, Span loc,
                               MsEventKind kind, const char *note)
{
    PtsSet pts = alias_points_to(result->alias, operand);
    const AllocSite *unique = alias_pts_unique_alloc_site(result->alias, pts);
    u32 i;

    for (i = 0; i < result->nsites; i++) {
        const AllocSite *site = alias_alloc_site_at(result->alias, i);

        if (alias_pts_has_alloc_site(result->alias, pts, site))
            (void)transition_site(result, path, i, action, loc, kind, note,
                                  unique == site);
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
            (void)transition_site(result, path, site - 1, action, loc, kind,
                                  note, false);
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
    IrInst folded;
    IrOperand ops[3], out;
    OptConfig cfg;
    u32 i;

    if (operand.kind == IROP_ICONST) {
        *value = (i64)operand.a;
        return true;
    }
    if (depth > function->nvals)
        return false;
    def = value_def(function, operand);
    if (!def || def->nops > CGF_ARRAY_LEN(ops))
        return false;
    if (def->op == IR_BITCAST && def->nops == 1)
        return operand_constant(function, def->ops[0], value, depth + 1);
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
    for (i = 0; i < def->nops; i++) {
        i64 constant;

        if (!operand_constant(function, def->ops[i], &constant, depth + 1))
            return false;
        ops[i] = ir_op_iconst((IrType)def->ops[i].type, constant);
    }
    folded.ops = ops;
    opt_config_init(&cfg, OPT_O0);
    if (!opt_fold_inst(&folded, &out, &cfg) || out.kind != IROP_ICONST)
        return false;
    *value = (i64)out.a;
    return true;
}

static bool operand_is_null(const IrFunc *function, IrOperand operand)
{
    i64 value;

    return operand.type == IRT_PTR &&
           operand_constant(function, operand, &value, 0) && value == 0;
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

static bool family_extent(const MsFunctionResult *result,
                          const MsAllocFamily *family, const IrInst *call,
                          u64 *extent)
{
    IrOperand operand;
    i64 first, second = 1;

    if (!family || family->size_arg == MS_NO_ARG ||
        !call_arg(call, family->size_arg, &operand) ||
        !operand_constant(result->function, operand, &first, 0) || first < 0)
        return false;
    if (family->size_arg2 != MS_NO_ARG &&
        (!call_arg(call, family->size_arg2, &operand) ||
         !operand_constant(result->function, operand, &second, 0) ||
         second < 0))
        return false;
    if ((u64)second && (u64)first > UINT64_MAX / (u64)second)
        return false;
    *extent = (u64)first * (u64)second;
    return true;
}

static bool access_range(MsFunctionResult *result, IrOperand pointer, u64 size,
                         i64 *lo, i64 *hi)
{
    i64 start_lo, start_hi;

    if (size > (u64)INT64_MAX ||
        !alias_offset_range(result->alias, pointer, &start_lo, &start_hi) ||
        start_lo != start_hi || start_lo > INT64_MAX - (i64)size)
        return false;
    *lo = start_lo;
    *hi = start_lo + (i64)size;
    return true;
}

static bool affine_access_range(MsFunctionResult *result, IrOperand pointer,
                                BlockId access_block, u64 size, i64 *lo,
                                i64 *hi)
{
    i64 start_lo, start_hi;

    if (size > (u64)INT64_MAX ||
        !dep_affine_ptr_range_at(result->function, pointer, access_block,
                                 &start_lo, &start_hi) ||
        start_hi > INT64_MAX - (i64)size)
        return false;
    *lo = start_lo;
    *hi = start_hi + (i64)size;
    return true;
}

static void mark_initialized(MsFunctionResult *result, MsPath *path,
                             IrOperand pointer, bool size_known, u64 size,
                             Span loc)
{
    PtsSet pts = alias_points_to(result->alias, pointer);
    const AllocSite *unique = alias_pts_unique_alloc_site(result->alias, pts);
    i64 lo, hi;
    u32 i;

    if (!unique || !size_known ||
        !access_range(result, pointer, size, &lo, &hi)) {
        for (i = 0; i < result->nsites; i++) {
            const AllocSite *site = alias_alloc_site_at(result->alias, i);

            if (alias_pts_has_alloc_site(result->alias, pts, site) &&
                path->facts[i].state == MS_ALLOCATED)
                init_reset(&path->facts[i], MS_INIT_UNKNOWN);
        }
        return;
    }
    i = alias_alloc_site_id(unique) - 1;
    if (path->facts[i].state != MS_ALLOCATED || size == 0)
        return;
    init_add_range(&path->facts[i], lo, hi);
    if (path->facts[i].extent_known && lo <= 0 && hi >= 0 &&
        (u64)hi >= path->facts[i].extent)
        init_reset(&path->facts[i], MS_INIT_FULL);
    ms_trace_push(&path->facts[i].trace, loc, MS_EV_USE,
                  "memory initialized here");
}

static bool access_must_oob(const MsFact *fact, i64 lo, i64 hi)
{
    if (!fact->extent_known || hi <= lo)
        return false;
    return lo < 0 || (u64)hi > fact->extent;
}

static bool access_must_uninit(const MsFact *fact, i64 lo, i64 hi)
{
    u32 i;

    if (hi <= lo || fact->init == MS_INIT_FULL || fact->init == MS_INIT_UNKNOWN)
        return false;
    if (fact->init == MS_INIT_NONE)
        return true;
    for (i = 0; i < fact->ninit_ranges; i++)
        if (lo >= fact->init_ranges[i].lo && hi <= fact->init_ranges[i].hi)
            return false;
    return true;
}

static bool process_access(MsFunctionResult *result, MsPath *path,
                           IrOperand pointer, bool size_known, u64 size,
                           bool write, bool check_uninit, BlockId access_block,
                           Span loc, const char *note)
{
    PtsSet pts = alias_points_to(result->alias, pointer);
    const AllocSite *unique = alias_pts_unique_alloc_site(result->alias, pts);
    bool oob = false;

    if (unique && !path->correlations_lost) {
        u32 site = alias_alloc_site_id(unique) - 1;
        MsFact *fact = &path->facts[site];
        i64 lo, hi;

        if (fact->state == MS_ALLOCATED && size_known && size != 0 &&
            access_range(result, pointer, size, &lo, &hi)) {
            oob = access_must_oob(fact, lo, hi);
            if (oob)
                add_issue(result, path, MS_ISSUE_OUT_OF_BOUNDS, loc, (i32)site,
                          false, &fact->trace);
            else if (!write && check_uninit && access_must_uninit(fact, lo, hi))
                add_issue(result, path, MS_ISSUE_UNINIT_READ, loc, (i32)site,
                          false, &fact->trace);
        } else if (fact->state == MS_ALLOCATED && size_known && size != 0 &&
                   fact->extent_known &&
                   affine_access_range(result, pointer, access_block, size, &lo,
                                       &hi) &&
                   (lo < 0 || (lo >= 0 && (u64)hi > fact->extent))) {
            oob = true;
            add_issue(result, path, MS_ISSUE_OUT_OF_BOUNDS, loc, (i32)site,
                      false, &fact->trace);
        }
    }
    transition_members(result, path, pointer, MS_ACT_DEREF, loc, MS_EV_USE,
                       note);
    if (write && !oob)
        mark_initialized(result, path, pointer, size_known, size, loc);
    return oob;
}

static void process_aggregate_uninit(MsFunctionResult *result, MsPath *path,
                                     IrOperand pointer,
                                     const IrMemLayout *layout, Span loc)
{
    PtsSet pts;
    const AllocSite *unique;
    MsFact *fact;
    i64 base_lo, base_hi;
    u32 site, i;

    if (!layout || layout->suppress_uninit || path->correlations_lost ||
        !alias_offset_range(result->alias, pointer, &base_lo, &base_hi) ||
        base_lo != base_hi)
        return;
    pts = alias_points_to(result->alias, pointer);
    unique = alias_pts_unique_alloc_site(result->alias, pts);
    if (!unique)
        return;
    site = alias_alloc_site_id(unique) - 1;
    fact = &path->facts[site];
    if (fact->state != MS_ALLOCATED)
        return;
    for (i = 0; i < layout->nranges; i++) {
        i64 lo, hi;

        if (layout->ranges[i].hi > (u64)INT64_MAX ||
            base_lo > INT64_MAX - (i64)layout->ranges[i].hi)
            return;
        lo = base_lo + (i64)layout->ranges[i].lo;
        hi = base_lo + (i64)layout->ranges[i].hi;
        if (access_must_uninit(fact, lo, hi)) {
            add_issue(result, path, MS_ISSUE_UNINIT_READ, loc, (i32)site, false,
                      &fact->trace);
            return;
        }
    }
}

static void mark_aggregate_copy(MsFunctionResult *result, MsPath *path,
                                IrOperand dst, IrOperand src,
                                const IrMemLayout *layout, Span loc)
{
    PtsSet dst_pts = alias_points_to(result->alias, dst);
    const AllocSite *dst_site =
        alias_pts_unique_alloc_site(result->alias, dst_pts);
    PtsSet src_pts;
    const AllocSite *src_site;
    MsFact *dst_fact;
    MsFact *src_fact = NULL;
    i64 dst_lo, dst_hi, src_lo = 0, src_hi = 0;
    u32 dst_index, i;

    if (!layout || !dst_site ||
        !alias_offset_range(result->alias, dst, &dst_lo, &dst_hi) ||
        dst_lo != dst_hi)
        return;
    dst_index = alias_alloc_site_id(dst_site) - 1;
    dst_fact = &path->facts[dst_index];
    if (dst_fact->state != MS_ALLOCATED)
        return;
    if (layout->suppress_uninit) {
        init_reset(dst_fact, MS_INIT_UNKNOWN);
        return;
    }

    src_pts = alias_points_to(result->alias, src);
    src_site = alias_pts_unique_alloc_site(result->alias, src_pts);
    if (src_site) {
        src_fact = &path->facts[alias_alloc_site_id(src_site) - 1];
        if (src_fact->state != MS_ALLOCATED ||
            !alias_offset_range(result->alias, src, &src_lo, &src_hi) ||
            src_lo != src_hi || src_fact->init == MS_INIT_UNKNOWN) {
            init_reset(dst_fact, MS_INIT_UNKNOWN);
            return;
        }
    }
    for (i = 0; i < layout->nranges; i++) {
        i64 dlo, dhi;
        bool initialized = true;

        if (layout->ranges[i].hi > (u64)INT64_MAX ||
            dst_lo > INT64_MAX - (i64)layout->ranges[i].hi) {
            init_reset(dst_fact, MS_INIT_UNKNOWN);
            return;
        }
        if (src_fact) {
            i64 slo, shi;

            if (src_lo > INT64_MAX - (i64)layout->ranges[i].hi) {
                init_reset(dst_fact, MS_INIT_UNKNOWN);
                return;
            }
            slo = src_lo + (i64)layout->ranges[i].lo;
            shi = src_lo + (i64)layout->ranges[i].hi;
            initialized = !access_must_uninit(src_fact, slo, shi);
        }
        dlo = dst_lo + (i64)layout->ranges[i].lo;
        dhi = dst_lo + (i64)layout->ranges[i].hi;
        init_remove_range(dst_fact, dlo, dhi);
        if (initialized)
            init_add_range(dst_fact, dlo, dhi);
    }
    ms_trace_push(&dst_fact->trace, loc, MS_EV_USE,
                  "aggregate members initialized here");
}

static void process_pointer_value_use(MsFunctionResult *result, MsPath *path,
                                      IrOperand pointer, Span loc)
{
    PtsSet pts;
    const AllocSite *unique;
    u32 site;

    if (pointer.type != IRT_PTR || path->correlations_lost)
        return;
    pts = alias_points_to(result->alias, pointer);
    unique = alias_pts_unique_alloc_site(result->alias, pts);
    if (!unique)
        return;
    site = alias_alloc_site_id(unique) - 1;
    if (path->facts[site].state == MS_FREED)
        add_issue(result, path, MS_ISSUE_USE_AFTER_FREE, loc, (i32)site, false,
                  &path->facts[site].trace);
}

static bool call_size(const MsFunctionResult *result, const IrInst *call,
                      u32 first_arg, u32 second_arg, u64 *size)
{
    IrOperand operand;
    i64 a, b = 1;

    if (!call_arg(call, first_arg, &operand) ||
        !operand_constant(result->function, operand, &a, 0) || a < 0)
        return false;
    if (second_arg != MS_NO_ARG &&
        (!call_arg(call, second_arg, &operand) ||
         !operand_constant(result->function, operand, &b, 0) || b < 0))
        return false;
    if ((u64)b && (u64)a > UINT64_MAX / (u64)b)
        return false;
    *size = (u64)a * (u64)b;
    return true;
}

static void apply_summary_write_range(MsFunctionResult *result, MsPath *path,
                                      IrOperand pointer,
                                      const MsParamSummary *effect, Span loc)
{
    i64 base_lo, base_hi, lo, hi;
    i32 site;

    process_pointer_value_use(result, path, pointer, loc);
    site = path_site_for_operand(result, path, pointer);
    if (site < 0 || !effect->write_range_known ||
        !alias_offset_range(result->alias, pointer, &base_lo, &base_hi) ||
        base_lo != base_hi ||
        (effect->write_lo > 0 && base_lo > INT64_MAX - effect->write_lo) ||
        (effect->write_lo < 0 && base_lo < INT64_MIN - effect->write_lo) ||
        (effect->write_hi > 0 && base_hi > INT64_MAX - effect->write_hi) ||
        (effect->write_hi < 0 && base_hi < INT64_MIN - effect->write_hi))
        return;
    lo = base_lo + effect->write_lo;
    hi = base_hi + effect->write_hi;
    init_add_range(&path->facts[site], lo, hi);
    ms_trace_push(&path->facts[site].trace, loc, MS_EV_USE,
                  "callee initialized this byte range here");
}

static bool process_known_memory_call(MsFunctionResult *result, MsPath *path,
                                      const IrInst *call, const char *name,
                                      BlockId block, Span loc)
{
    IrOperand dst, src;
    u64 size = 0;
    bool known;

    if (!name)
        return false;
    if (strcmp(name, "memcpy") == 0 || strcmp(name, "memmove") == 0) {
        if (!call_arg(call, 0, &dst) || !call_arg(call, 1, &src))
            return false;
        known = call_size(result, call, 2, MS_NO_ARG, &size);
        process_access(result, path, dst, known, size, true, true, block, loc,
                       "wrote through pointer here");
        process_access(result, path, src, known, size, false, true, block, loc,
                       "read through pointer here");
        return true;
    }
    if (strcmp(name, "memset") == 0) {
        if (!call_arg(call, 0, &dst))
            return false;
        known = call_size(result, call, 2, MS_NO_ARG, &size);
        process_access(result, path, dst, known, size, true, true, block, loc,
                       "wrote through pointer here");
        return true;
    }
    if (strcmp(name, "fread") == 0) {
        if (!call_arg(call, 0, &dst))
            return false;
        known = call_size(result, call, 1, 2, &size);
        process_access(result, path, dst, known, size, true, true, block, loc,
                       "library call writes through pointer here");
        return true;
    }
    if (strcmp(name, "snprintf") == 0) {
        if (!call_arg(call, 0, &dst))
            return false;
        known = call_size(result, call, 1, MS_NO_ARG, &size);
        process_access(result, path, dst, known, size, true, true, block, loc,
                       "library call writes through pointer here");
        if (call_arg(call, 2, &src))
            process_access(result, path, src, false, 0, false, true, block, loc,
                           "library call reads through pointer here");
        return true;
    }
    return false;
}

static void process_call(MsFunctionResult *result, MsPath *path,
                         const IrInst *call, BlockId block)
{
    const char *name = call_name(result->module, call);
    const MsAllocFamily *family = ms_alloc_family_lookup(name);
    const MsLibSummary *lib = name ? ms_lib_summary_lookup(name) : NULL;
    const AllocSite *created = alias_alloc_site(result->alias, call);
    Span loc = ir_inst_span(result->module, call);
    IrOperand operand;
    u64 extent = 0;
    bool extent_known = family_extent(result, family, call, &extent);
    u32 first, i;

    if (family && family->frees_on_success &&
        call_arg(call, family->frees_arg, &operand))
        process_pointer_value_use(result, path, operand, loc);
    if (created) {
        u32 site = alias_alloc_site_id(created) - 1;
        MsFact *fact = &path->facts[site];

        fact->extent_known = extent_known;
        fact->extent = extent_known ? extent : 0;
        path->facts[site].realloc_old_site = -1;
        path->facts[site].realloc_pending = false;
        path->facts[site].alloc_success = MS_ALLOC_SUCCESS_DIRECT;
        path->facts[site].alloc_status_value = 0;
        if (family && family->frees_on_success) {
            i32 old = -1;

            fact->state = MS_UNKNOWN; /* pending success/failure correlation */
            fact->realloc_pending = true;
            init_reset(fact, MS_INIT_UNKNOWN);
            ms_trace_push(&fact->trace, loc, MS_EV_REALLOC,
                          "reallocated here; success or failure is pending");
            if (call_arg(call, family->frees_arg, &operand))
                old = path_site_for_operand(result, path, operand);
            if (extent_known && extent == 0) {
                MsTrace proof = fact->trace;

                ms_trace_push(&proof, loc, MS_EV_REALLOC,
                              "zero allocation size passed here");
                add_issue(result, path, MS_ISSUE_REALLOC_ZERO, loc, (i32)site,
                          false, &proof);
                if (old >= 0) {
                    path->facts[old].state = MS_UNKNOWN;
                    init_reset(&path->facts[old], MS_INIT_UNKNOWN);
                    trace_clear(&path->facts[old]);
                }
                fact->realloc_pending = false;
            } else if (old >= 0 && path->facts[old].state == MS_ALLOCATED) {
                if (family->is_file_resource) {
                    (void)transition_site(
                        result, path, (u32)old, MS_ACT_FREE, loc, MS_EV_REALLOC,
                        "old stream closed while replacement was attempted",
                        false);
                } else {
                    fact->realloc_old_site = old;
                    ms_trace_push(&path->facts[old].trace, loc, MS_EV_REALLOC,
                                  "reallocated here; on success the old "
                                  "pointer is freed");
                }
            }
        } else if (family && family->alloc_out_arg != MS_NO_ARG &&
                   family->success != MS_ALLOC_SUCCESS_DIRECT &&
                   call->result.v) {
            fact->state = MS_UNKNOWN;
            init_reset(fact, MS_INIT_UNKNOWN);
            fact->alloc_success = family->success;
            fact->alloc_status_value = call->result.v;
            ms_trace_push(&fact->trace, loc, MS_EV_ALLOC,
                          "allocation result is pending its status check");
        } else {
            (void)transition_site(
                result, path, site, MS_ACT_ALLOC, loc, MS_EV_ALLOC,
                family && family->is_file_resource ? "resource opened here"
                                                   : "allocated here",
                false);
            init_reset(fact, family && family->fully_written ? MS_INIT_FULL
                                                             : MS_INIT_NONE);
        }
        if (family && family->alloc_out_arg != MS_NO_ARG &&
            call_arg(call, family->alloc_out_arg, &operand) &&
            !local_address(result->function, operand, 0))
            (void)transition_site(result, path, site, MS_ACT_ESCAPE, loc,
                                  MS_EV_ESCAPE,
                                  "ownership published through an escaping "
                                  "output parameter here",
                                  false);
    }
    if (family && !family->allocates && family->frees_arg != MS_NO_ARG &&
        call_arg(call, family->frees_arg, &operand)) {
        PtsSet pts;
        const AllocSite *unique;
        i64 lo, hi;
        i32 site;

        if (operand_is_null(result->function, operand))
            return;
        pts = alias_points_to(result->alias, operand);
        unique = alias_pts_unique_alloc_site(result->alias, pts);
        if (unique) {
            u32 unique_site = alias_alloc_site_id(unique) - 1;
            const MsAllocFamily *created_by = result->families[unique_site];

            if (created_by &&
                created_by->is_file_resource != family->is_file_resource) {
                MsTrace proof = path->facts[unique_site].trace;

                ms_trace_push(&proof, loc, MS_EV_FREE,
                              family->is_file_resource
                                  ? "stream close applied to a non-stream "
                                    "allocation"
                                  : "memory deallocator applied to an open "
                                    "stream");
                add_issue(result, path, MS_ISSUE_FREE_NONHEAP, loc,
                          (i32)unique_site, false, &proof);
                return;
            }
        }
        if (alias_pts_must_be_nonheap(result->alias, pts)) {
            MsTrace proof;

            ms_trace_init(&proof, result->arena);
            ms_trace_push(&proof, loc, MS_EV_FREE,
                          "pointer is proven to designate non-heap storage");
            add_issue(result, path, MS_ISSUE_FREE_NONHEAP, loc, -1, false,
                      &proof);
            return;
        }
        if (unique && alias_offset_range(result->alias, operand, &lo, &hi) &&
            (lo > 0 || hi < 0)) {
            u32 unique_site = alias_alloc_site_id(unique) - 1;
            MsTrace proof = path->facts[unique_site].trace;

            ms_trace_push(&proof, loc, MS_EV_FREE,
                          "pointer has a proven nonzero allocation offset");
            add_issue(result, path, MS_ISSUE_FREE_NONHEAP, loc,
                      (i32)unique_site, false, &proof);
            path->facts[unique_site].state = MS_UNKNOWN;
            init_reset(&path->facts[unique_site], MS_INIT_UNKNOWN);
            trace_clear(&path->facts[unique_site]);
            return;
        }
        site = path_site_for_operand(result, path, operand);
        if (site >= 0)
            (void)transition_site(result, path, (u32)site, MS_ACT_FREE, loc,
                                  MS_EV_FREE, "freed here", true);
        return;
    }
    if (family && !lib)
        return;
    if (process_known_memory_call(result, path, call, name, block, loc))
        return;
    {
        const MsSummary *summary = ms_summary_for_call(result->summaries, call);
        bool handled = summary != NULL || lib != NULL;
        u64 lib_extent = 0;
        bool lib_extent_known =
            lib && lib->write_size_arg >= 0 &&
            call_size(result, call, (u32)lib->write_size_arg,
                      lib->write_size_arg2 >= 0 ? (u32)lib->write_size_arg2
                                                : MS_NO_ARG,
                      &lib_extent);

        if (handled) {
            first = call_first_arg(call);
            for (i = first; i < call->nops; i++) {
                u32 arg = i - first;
                bool deref = false, write = false, escape = false;
                bool may_free = false, must_free = false;
                IrOperand actual = call->ops[i];

                if (summary && arg < summary->nparams) {
                    const MsParamSummary *e = &summary->params[arg];
                    deref = e->dereferenced;
                    write = e->written;
                    escape = e->escapes;
                    may_free = e->may_free;
                    must_free = e->must_free;
                    if (summary->partial || summary->top) {
                        deref = true;
                        write = true;
                        escape = !e->annot_no_escape && !e->annot_borrow;
                        if (!e->annot_borrow && !e->annot_takes_ownership)
                            may_free = true;
                    }
                } else if (summary && (summary->top || summary->partial)) {
                    deref = write = escape = actual.type == IRT_PTR;
                } else if (lib && arg < 64) {
                    u64 bit = 1ull << arg;
                    deref = (lib->deref_mask & bit) != 0;
                    write = (lib->write_mask & bit) != 0;
                    escape = (lib->escape_mask & bit) != 0;
                    may_free = (lib->free_mask & bit) != 0;
                    if (arg >= ms_lib_summary_variadic_from(lib))
                        deref = write = escape = true;
                }
                if (actual.type != IRT_PTR)
                    continue;
                /* The family transition already validated and consumed this
                 * argument before the generic libc effects are applied. */
                if (family && family->frees_on_success &&
                    arg == family->frees_arg)
                    continue;
                if ((summary && (summary->top || summary->partial)) && deref) {
                    PtsSet pts = alias_points_to(result->alias, actual);
                    const AllocSite *unique =
                        alias_pts_unique_alloc_site(result->alias, pts);
                    if (unique) {
                        u32 site = alias_alloc_site_id(unique) - 1;
                        if (path->facts[site].state == MS_FREED)
                            add_issue(result, path, MS_ISSUE_USE_AFTER_FREE,
                                      loc, (i32)site, true,
                                      &path->facts[site].trace);
                    }
                } else if (write && summary && arg < summary->nparams &&
                           summary->params[arg].write_range_known) {
                    apply_summary_write_range(result, path, actual,
                                              &summary->params[arg], loc);
                } else if (write && lib && lib_extent_known) {
                    process_access(result, path, actual, true, lib_extent, true,
                                   true, block, loc,
                                   "library call writes through pointer here");
                } else if (deref || write) {
                    process_access(result, path, actual, false, 0, write, true,
                                   block, loc,
                                   write ? "callee writes through pointer here"
                                         : "callee reads through pointer here");
                }
                if (must_free) {
                    i32 site = path_site_for_operand(result, path, actual);
                    if (site >= 0)
                        (void)transition_site(result, path, (u32)site,
                                              MS_ACT_FREE, loc, MS_EV_FREE,
                                              "ownership taken here", true);
                } else if (may_free) {
                    PtsSet pts = alias_points_to(result->alias, actual);
                    degrade_candidate_sites(result, path, pts);
                }
                if (escape)
                    transition_reachable(result, path, actual, MS_ACT_ESCAPE,
                                         loc, MS_EV_CALL,
                                         "passed to an escaping callee here");
            }
            return;
        }
    }
    first = call_first_arg(call);
    for (i = first; i < call->nops; i++) {
        if (call->ops[i].type == IRT_PTR) {
            PtsSet pts = alias_points_to(result->alias, call->ops[i]);
            const AllocSite *unique =
                alias_pts_unique_alloc_site(result->alias, pts);

            if (unique) {
                u32 site = alias_alloc_site_id(unique) - 1;

                if (path->facts[site].state == MS_FREED)
                    add_issue(result, path, MS_ISSUE_USE_AFTER_FREE, loc,
                              (i32)site, true, &path->facts[site].trace);
            }
            transition_reachable(result, path, call->ops[i], MS_ACT_ESCAPE, loc,
                                 MS_EV_CALL, "passed to an unknown call here");
        }
    }
}

static void process_inst(MsFunctionResult *result, MsPath *path,
                         const IrInst *in, BlockId block)
{
    Span loc = ir_inst_span(result->module, in);

    switch (in->op) {
    case IR_CALL:
        process_call(result, path, in, block);
        break;
    case IR_LOAD:
        if (in->nops >= 1)
            process_access(result, path, in->ops[0], true,
                           ir_type_size((IrType)in->type), false, true, block,
                           loc, "read through pointer here");
        break;
    case IR_STORE:
        if (in->nops >= 2) {
            if (in->ops[0].type == IRT_PTR)
                process_pointer_value_use(result, path, in->ops[0], loc);
            process_access(result, path, in->ops[1], true,
                           ir_type_size((IrType)in->ops[0].type), true, true,
                           block, loc, "wrote through pointer here");
            if (in->ops[0].type == IRT_PTR &&
                !local_address(result->function, in->ops[1], 0))
                transition_reachable(result, path, in->ops[0], MS_ACT_ESCAPE,
                                     loc, MS_EV_ESCAPE,
                                     "stored into escaping memory here");
        }
        break;
    case IR_MEMCPY:
        if (in->nops >= 2) {
            i64 value = 0;
            const IrMemLayout *layout;
            bool source_oob;
            bool known =
                in->nops >= 3 &&
                operand_constant(result->function, in->ops[2], &value, 0) &&
                value >= 0;

            layout = known ? ir_mem_layout_find(result->module, in, (u64)value)
                           : NULL;

            /* The source is observed before the destination changes.  This
             * is load-before-store even for `*p = *p`, and preserves missing
             * member state when the destination is another heap object. */
            source_oob = process_access(result, path, in->ops[1], known,
                                        (u64)value, false, false, block, loc,
                                        "read through pointer here");
            if (!source_oob && layout)
                process_aggregate_uninit(result, path, in->ops[1], layout, loc);
            process_access(result, path, in->ops[0], known, (u64)value,
                           layout == NULL, false, block, loc,
                           "wrote through pointer here");
            if (layout)
                mark_aggregate_copy(result, path, in->ops[0], in->ops[1],
                                    layout, loc);
        }
        break;
    case IR_MEMSET:
        if (in->nops >= 1) {
            i64 value = 0;
            bool known =
                in->nops >= 3 &&
                operand_constant(result->function, in->ops[2], &value, 0) &&
                value >= 0;

            process_access(result, path, in->ops[0], known, (u64)value, true,
                           true, block, loc, "wrote through pointer here");
        }
        break;
    case IR_RET:
        if (in->nops == 1 && in->ops[0].type == IRT_PTR) {
            const IrInst *def = value_def(result->function, in->ops[0]);

            process_pointer_value_use(result, path, in->ops[0], loc);
            transition_reachable(result, path, in->ops[0], MS_ACT_ESCAPE, loc,
                                 MS_EV_RETURN, "returned here");
            if (def && def->op == IR_CALL) {
                const char *name = call_name(result->module, def);
                IrOperand returned;

                if (name &&
                    (strcmp(name, "memcpy") == 0 ||
                     strcmp(name, "memmove") == 0 ||
                     strcmp(name, "memset") == 0) &&
                    call_arg(def, 0, &returned))
                    transition_reachable(result, path, returned, MS_ACT_ESCAPE,
                                         loc, MS_EV_RETURN, "returned here");
            }
        }
        break;
    case IR_ICMP:
        if (in->nops == 2 && !operand_is_null(result->function, in->ops[0]) &&
            !operand_is_null(result->function, in->ops[1])) {
            process_pointer_value_use(result, path, in->ops[0], loc);
            process_pointer_value_use(result, path, in->ops[1], loc);
        }
        break;
    case IR_PTRADD:
    case IR_BITCAST:
        if (in->nops >= 1)
            process_pointer_value_use(result, path, in->ops[0], loc);
        break;
    default:
        break;
    }
}

static bool decode_predicate(const IrFunc *function, const MsPath *path,
                             const IrInst *term, u32 edge,
                             MsPredicate *predicate, IrOperand *pointer,
                             bool *pointer_nonnull)
{
    const IrInst *cmp;
    IrOperand lhs, rhs, subject;
    i64 constant;
    bool equal;

    memset(predicate, 0, sizeof(*predicate));
    memset(pointer, 0, sizeof(*pointer));
    *pointer_nonnull = false;
    if (term->op == IR_SWITCH && term->nops == 1 && edge > 0) {
        predicate->subject = path_resolve_binding(path, term->ops[0]);
        predicate->constant = term->edges[edge].case_val;
        predicate->equal = true;
        return predicate->subject.kind == IROP_VALUE;
    }
    if (term->op != IR_CONDBR || term->nops != 1 || edge > 1)
        return false;
    subject = path_resolve_binding(path, term->ops[0]);
    cmp = value_def(function, subject);
    if (!cmp || cmp->op != IR_ICMP || cmp->nops != 2 ||
        (cmp->subop != ICMP_EQ && cmp->subop != ICMP_NE))
        return false;
    lhs = path_resolve_binding(path, cmp->ops[0]);
    rhs = path_resolve_binding(path, cmp->ops[1]);
    if (operand_constant(function, lhs, &constant, 0)) {
        subject = rhs;
    } else if (operand_constant(function, rhs, &constant, 0)) {
        subject = lhs;
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
            path_resolve_binding(path, term->ops[0]),
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
    if (result->families[site] && result->families[site]->frees_on_success &&
        path->facts[site].realloc_pending) {
        i32 old = path->facts[site].realloc_old_site;

        path->facts[site].state = nonnull ? MS_ALLOCATED : MS_UNALLOCATED;
        init_reset(&path->facts[site], MS_INIT_UNKNOWN);
        ms_trace_push(
            &path->facts[site].trace, loc, MS_EV_BRANCH,
            result->families[site]->is_file_resource
                ? (nonnull ? "this branch is taken only when reopening the "
                             "stream succeeded"
                           : "this branch is taken only when reopening the "
                             "stream failed")
                : (nonnull ? "this branch is taken only when realloc succeeded"
                           : "this branch is taken only when realloc failed"));
        if (nonnull && old >= 0)
            (void)transition_site(
                result, path, (u32)old, MS_ACT_FREE, loc, MS_EV_REALLOC,
                "old pointer freed when realloc succeeded", false);
        path->facts[site].realloc_old_site = -1;
        path->facts[site].realloc_pending = false;
        return;
    }
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
}

static IrIcmp reverse_icmp(IrIcmp pred)
{
    switch (pred) {
    case ICMP_SLT:
        return ICMP_SGT;
    case ICMP_SLE:
        return ICMP_SGE;
    case ICMP_SGT:
        return ICMP_SLT;
    case ICMP_SGE:
        return ICMP_SLE;
    case ICMP_ULT:
        return ICMP_UGT;
    case ICMP_ULE:
        return ICMP_UGE;
    case ICMP_UGT:
        return ICMP_ULT;
    case ICMP_UGE:
        return ICMP_ULE;
    default:
        return pred;
    }
}

static bool status_edge_success(const MsFunctionResult *result,
                                const MsPath *path, const IrInst *term,
                                u32 edge, u32 status_value,
                                MsAllocSuccess convention, bool *success)
{
    const IrInst *cmp;
    IrOperand lhs, rhs, subject;
    IrIcmp pred;
    i64 constant;
    bool truth;

    if (!success || term->op != IR_CONDBR || term->nops != 1 || edge > 1 ||
        !(cmp = value_def(result->function,
                          path_resolve_binding(path, term->ops[0]))) ||
        cmp->op != IR_ICMP || cmp->nops != 2)
        return false;
    pred = (IrIcmp)cmp->subop;
    lhs = path_resolve_binding(path, cmp->ops[0]);
    rhs = path_resolve_binding(path, cmp->ops[1]);
    if (lhs.kind == IROP_VALUE && lhs.a == status_value &&
        operand_constant(result->function, rhs, &constant, 0)) {
        subject = lhs;
    } else if (rhs.kind == IROP_VALUE && rhs.a == status_value &&
               operand_constant(result->function, lhs, &constant, 0)) {
        subject = rhs;
        pred = reverse_icmp(pred);
    } else {
        return false;
    }
    (void)subject;
    if (constant != 0)
        return false;
    truth = edge == 0;
    if (convention == MS_ALLOC_SUCCESS_STATUS_ZERO) {
        if (pred == ICMP_EQ)
            *success = truth;
        else if (pred == ICMP_NE)
            *success = !truth;
        else
            return false;
    } else if (convention == MS_ALLOC_SUCCESS_STATUS_NONNEG) {
        if (pred == ICMP_SGE)
            *success = truth;
        else if (pred == ICMP_SLT)
            *success = !truth;
        else
            return false;
    } else {
        return false;
    }
    return true;
}

static void refine_alloc_status_branch(MsFunctionResult *result, MsPath *path,
                                       const IrInst *term, u32 edge, Span loc)
{
    u32 i;

    for (i = 0; i < result->nsites; i++) {
        MsFact *fact = &path->facts[i];
        const MsAllocFamily *family = result->families[i];
        bool success;

        if (fact->alloc_success == MS_ALLOC_SUCCESS_DIRECT ||
            !status_edge_success(result, path, term, edge,
                                 fact->alloc_status_value, fact->alloc_success,
                                 &success))
            continue;
        fact->state = success ? MS_ALLOCATED : MS_UNALLOCATED;
        init_reset(fact, success && family && family->fully_written
                             ? MS_INIT_FULL
                             : MS_INIT_NONE);
        ms_trace_push(&fact->trace, loc, MS_EV_BRANCH,
                      success ? "allocator status proves success on this path"
                              : "allocator status proves failure on this path");
        fact->alloc_success = MS_ALLOC_SUCCESS_DIRECT;
        fact->alloc_status_value = 0;
    }
}

static bool path_edge_feasible(const IrFunc *function, const MsPath *path,
                               const IrInst *term, u32 edge)
{
    IrOperand condition;
    i64 constant;

    if (!opt_cfg_edge_feasible(function, term, edge))
        return false;
    if (term->op != IR_CONDBR || term->nops != 1 || edge > 1)
        return true;
    condition = path_resolve_binding(path, term->ops[0]);
    if (!operand_constant(function, condition, &constant, 0))
        return true;
    return (constant != 0) == (edge == 0);
}

static bool path_bind_target(MsFunctionResult *result, MsPath *path,
                             const IrEdge *edge, const IrBlock *target)
{
    MsBinding next[MS_MAX_BINDINGS_PER_PATH];
    u32 i;

    if (path->correlations_lost) {
        path->nbindings = 0;
        return true;
    }
    if (target->nparams != edge->nargs ||
        target->nparams > MS_MAX_BINDINGS_PER_PATH) {
        result->degraded = true;
        path->correlations_lost = true;
        path_degrade_facts(result, path);
        return true;
    }
    for (i = 0; i < target->nparams; i++) {
        next[i].param = target->params[i];
        next[i].incoming = path_resolve_binding(path, edge->args[i]);
    }
    path->nbindings = target->nparams;
    if (target->nparams)
        memcpy(path->bindings, next, target->nparams * sizeof(next[0]));
    return true;
}

static u32 feasible_edge_count(const IrFunc *function, const MsPath *path,
                               const IrInst *term)
{
    u32 count = 0, i;

    for (i = 0; i < term->nedges; i++)
        if (path_edge_feasible(function, path, term, i))
            count++;
    return count;
}

static void propagate_successors(MsFunctionResult *result, MsWorklist *work,
                                 const MsPath *source, const IrInst *term)
{
    u32 feasible = feasible_edge_count(result->function, source, term);
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

        if (!path_edge_feasible(result->function, source, term, i))
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
        if (!result->split_budget_exhausted)
            refine_alloc_status_branch(result, &path, term, i, loc);
        if (!result->split_budget_exhausted &&
            decode_predicate(result->function, &path, term, i, &predicate,
                             &pointer, &pointer_nonnull)) {
            if (!path_add_predicate(result, &path, predicate))
                continue;
            if (pointer.kind != IROP_NONE)
                refine_pointer_branch(result, &path, pointer, pointer_nonnull,
                                      loc);
        }
        if (!path_bind_target(result, &path, &term->edges[i],
                              &result->function->blocks[target - 1]))
            continue;
        add_block_path(result, work, target - 1, &path);
    }
}

static bool realloc_relation_pending(const MsFunctionResult *result,
                                     const MsPath *path, u32 old_site)
{
    u32 i;

    for (i = 0; i < result->nsites; i++)
        if (path->facts[i].realloc_old_site == (i32)old_site)
            return true;
    return false;
}

static void record_exit_leaks(MsFunctionResult *result, const MsPath *path,
                              Span loc)
{
    u32 i;

    if (path->correlations_lost)
        return;
    for (i = 0; i < result->nsites; i++) {
        const AllocSite *site;
        const IrInst *call;
        Span primary;
        MsTrace proof;

        if (path->facts[i].state != MS_ALLOCATED ||
            realloc_relation_pending(result, path, i))
            continue;
        site = alias_alloc_site_at(result->alias, i);
        call = alias_alloc_site_call(site);
        primary = loc.file_id ? loc : ir_inst_span(result->module, call);
        proof = path->facts[i].trace;
        ms_trace_push(&proof, primary, MS_EV_RETURN,
                      "function returns on this path without releasing it");
        add_issue(result, path, MS_ISSUE_LEAK, primary, (i32)i, false, &proof);
    }
}

static void context_alias_seeds(Arena *arena, IrModule *module,
                                IrFunc *function, const MsSummarySet *summaries,
                                AliasAllocSeed **alloc_out, u32 *nalloc_out,
                                AliasReturnSeed **return_out, u32 *nreturn_out)
{
    AliasAllocSeed *allocs;
    AliasReturnSeed *returns;
    u32 na = 0, nr = 0, bi, cap = 0, return_cap = 0;

    for (bi = 0; bi < function->nblocks; bi++) {
        const IrInst *in;
        for (in = function->blocks[bi].first; in; in = in->next) {
            const MsSummary *s = ms_summary_for_call(summaries, in);
            const char *callee_name = call_name(module, in);
            const MsLibSummary *lib = ms_lib_summary_lookup(callee_name);
            u32 p;

            cap++;
            if (s) {
                for (p = 0; p < s->nparams; p++)
                    if (!s->annot_returns_owned &&
                        s->params[p].returned_alias &&
                        (!s->top || s->params[p].annot_returns_borrowed))
                        return_cap++;
            } else if (lib && lib->return_alias >= 0) {
                return_cap++;
            }
        }
    }
    allocs = arena_alloc(arena, (cap ? cap : 1) * sizeof(*allocs),
                         _Alignof(AliasAllocSeed));
    returns =
        arena_alloc(arena, (return_cap ? return_cap : 1) * sizeof(*returns),
                    _Alignof(AliasReturnSeed));
    for (bi = 0; bi < function->nblocks; bi++) {
        const IrInst *in;
        for (in = function->blocks[bi].first; in; in = in->next) {
            const MsSummary *s = ms_summary_for_call(summaries, in);
            const char *callee_name = call_name(module, in);
            const MsLibSummary *lib = ms_lib_summary_lookup(callee_name);
            if (ms_alloc_seed_for_call(module, in, &allocs[na]))
                na++;
            else if ((s && s->returns_ownership &&
                      (!s->top || s->annot_returns_owned)) ||
                     (lib && lib->returns_ownership))
                allocs[na++] = (AliasAllocSeed){in, true, ALIAS_NO_OUT_PARAM};
            if (s) {
                u32 p;
                for (p = 0; p < s->nparams; p++)
                    if (!s->annot_returns_owned &&
                        s->params[p].returned_alias &&
                        (!s->top || s->params[p].annot_returns_borrowed))
                        returns[nr++] = (AliasReturnSeed){in, p};
            } else if (lib && lib->return_alias >= 0)
                returns[nr++] = (AliasReturnSeed){in, (u32)lib->return_alias};
        }
    }
    *alloc_out = allocs;
    *nalloc_out = na;
    *return_out = returns;
    *nreturn_out = nr;
    for (bi = 0; bi < nr; bi++)
        if (!returns[bi].call || returns[bi].call->op != IR_CALL)
            CGF_ICE("memsafe: corrupt return-alias seed in @%s",
                    function->name);
}

static MsFunctionResult *
ms_analyze_function_with_summaries(Arena *arena, IrModule *module,
                                   IrFunc *function, bool no_strict_aliasing,
                                   const MsSummarySet *summaries)
{
    MsFunctionResult *result;
    AliasAllocSeed *seeds = NULL;
    AliasConfig config = {0};
    AliasReturnSeed *return_seeds = NULL;
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
    u32 nreturn_seeds = 0;

    if (summaries)
        context_alias_seeds(arena, module, function, summaries, &seeds, &nseeds,
                            &return_seeds, &nreturn_seeds);
    else
        nseeds = ms_alias_alloc_seeds(arena, module, function, &seeds);
    result->summaries = summaries;
    config.func = function;
    config.no_strict_aliasing = no_strict_aliasing;
    config.alloc_seeds = seeds;
    config.nalloc_seeds = nseeds;
    config.return_seeds = return_seeds;
    config.nreturn_seeds = nreturn_seeds;
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
        bool noreturn = false;

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
            process_inst(result, &path, in, (BlockId){item.block + 1});
            if (in->op == IR_CALL && (in->flags & IRF_NORETURN)) {
                noreturn = true;
                break;
            }
            if (path.correlations_lost)
                path_degrade_facts(result, &path);
        }
        if (noreturn)
            continue;
        in = function->blocks[item.block].last;
        if (!in || in->op == IR_RET) {
            record_exit_leaks(result, &path,
                              in ? ir_inst_span(module, in) : (Span){0});
            add_exit_path(result, &path);
        } else if (in->nedges) {
            propagate_successors(result, &work, &path, in);
        }
    }
    free(work.items);
    return result;
}

MsFunctionResult *ms_analyze_function(Arena *arena, IrModule *module,
                                      IrFunc *function, bool no_strict_aliasing)
{
    return ms_analyze_function_with_summaries(arena, module, function,
                                              no_strict_aliasing, NULL);
}

void ms_result_free(MsFunctionResult *result)
{
    if (result && result->alias) {
        alias_free(result->alias);
        result->alias = NULL;
    }
    if (result) {
        free(result->issues);
        result->issues = NULL;
        result->nissues = 0;
        result->issue_cap = 0;
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

u32 ms_result_issue_count(const MsFunctionResult *result)
{
    return result ? result->nissues : 0;
}

const MsIssue *ms_result_issue_at(const MsFunctionResult *result, u32 index)
{
    return result && index < result->nissues ? &result->issues[index] : NULL;
}

static WarnId issue_warn_id(const MsIssue *issue)
{
    static const WarnId ids[MS_ISSUE_COUNT] = {
        WARN_MEM_USE_AFTER_FREE, WARN_MEM_DOUBLE_FREE, WARN_MEM_LEAK,
        WARN_MEM_OUT_OF_BOUNDS,  WARN_MEM_UNINIT_READ, WARN_MEM_FREE_NONHEAP,
        WARN_MEM_REALLOC_ZERO,
    };

    if (issue->strict && issue->kind == MS_ISSUE_USE_AFTER_FREE)
        return WARN_MEM_USE_AFTER_FREE_UNKNOWN;
    return (u32)issue->kind < MS_ISSUE_COUNT ? ids[issue->kind] : WARN_NONE;
}

static const char *issue_message(const MsIssue *issue)
{
    if (issue->strict && issue->kind == MS_ISSUE_USE_AFTER_FREE)
        return "passing freed memory to an unknown function may use it";
    switch (issue->kind) {
    case MS_ISSUE_USE_AFTER_FREE:
        return "use of memory after it was freed";
    case MS_ISSUE_DOUBLE_FREE:
        return issue->file_resource ? "resource is closed more than once"
                                    : "memory is freed more than once";
    case MS_ISSUE_LEAK:
        return issue->file_resource
                   ? "opened resource is not closed before this return"
                   : "allocated memory is not released before this return";
    case MS_ISSUE_OUT_OF_BOUNDS:
        return "memory access is outside the allocated object";
    case MS_ISSUE_UNINIT_READ:
        return "read of uninitialized heap memory";
    case MS_ISSUE_FREE_NONHEAP:
        return "free called on a pointer not returned by an allocator";
    case MS_ISSUE_REALLOC_ZERO:
        return "realloc with size 0 is implementation-defined; use free and "
               "assign NULL explicitly";
    case MS_ISSUE_COUNT:
        break;
    }
    return "memory-safety issue";
}

static void emit_issue(WarnCtx *warnings, const MsIssue *issue)
{
    WarnId id = issue_warn_id(issue);
    u32 i;

    if (id == WARN_NONE || !warn_enabled(warnings, id, issue->loc))
        return;
    warn_at(warnings, id, issue->loc, "%s", issue_message(issue));
    for (i = 0; i < issue->trace.len; i++) {
        MsEvent event;

        if (ms_trace_event(&issue->trace, i, &event))
            diag_emit(warn_diag(warnings), DIAG_NOTE, event.loc, "%s",
                      event.note);
    }
}

void ms_warn_module(WarnCtx *warnings, IrModule *module,
                    bool no_strict_aliasing)
{
    Arena analysis;
    OptConfig cfg;
    u32 fi;

    if (!warnings || !module)
        return;
    /* L1 is an SSA/path analysis even at -O0.  Promotion is an analysis
     * normalization step here, not a user-selected optimization, and the
     * driver gave us a throwaway module separate from emission. */
    opt_config_init(&cfg, OPT_O0);
    cfg.no_strict_aliasing = no_strict_aliasing;
    (void)opt_mem2reg(module, &cfg);
    arena_init(&analysis);
    {
        MsSummarySet *summaries =
            ms_summary_build(&analysis, module, no_strict_aliasing, warnings);
        for (fi = 0; fi < module->nfuncs; fi++) {
            MsFunctionResult *result = ms_analyze_function_with_summaries(
                &analysis, module, &module->funcs[fi], no_strict_aliasing,
                summaries);
            u32 i;

            for (i = 0; i < result->nissues; i++)
                emit_issue(warnings, &result->issues[i]);
            ms_result_free(result);
        }
    }
    arena_free_all(&analysis);
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
    {
        MsSummarySet *summaries =
            ms_summary_build(&analysis, module, no_strict_aliasing, NULL);
        ms_summary_dump(summaries, out);
        for (fi = 0; fi < module->nfuncs; fi++) {
            MsFunctionResult *result = ms_analyze_function_with_summaries(
                &analysis, module, &module->funcs[fi], no_strict_aliasing,
                summaries);
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
                    const MsTrace *trace =
                        &result->exits[ei].path.facts[si].trace;
                    u32 ti;

                    fprintf(
                        out, "site=%u callee=%s exit=%u state=%s\n",
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
    }
    arena_free_all(&analysis);
}
