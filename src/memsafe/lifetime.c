#include "memsafe/memsafe.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "opt/dep.h"
#include "opt/opt.h"
#include "parse/ast.h"
#include "pp/pp.h"
#include "sema/sema.h"
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
#define MS_MAX_POINTER_ORIGINS 8u

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
    bool nonnull_proven;
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

typedef struct MsPointerOrigin {
    IrOperand subject;
    IrOperand origin;
} MsPointerOrigin;

typedef enum MsProofKind {
    MS_PROOF_NONE,
    MS_PROOF_INDEPENDENT,
    MS_PROOF_PATH,
} MsProofKind;

typedef struct MsPath {
    MsFact *facts;
    MsPredicate predicates[MS_MAX_PREDICATES_PER_PATH];
    u32 npredicates;
    MsBinding bindings[MS_MAX_BINDINGS_PER_PATH];
    u32 nbindings;
    MsPointerOrigin pointer_origins[MS_MAX_POINTER_ORIGINS];
    u32 npointer_origins;
    bool pointer_origins_lost;
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

typedef enum MsRuntimeAccessKind {
    MS_RUNTIME_READ,
    MS_RUNTIME_WRITE,
} MsRuntimeAccessKind;

typedef struct MsRuntimeAccess {
    const IrInst *inst;
    IrOperand pointer;
    IrOperand size;
    u32 site_id;
    u8 slot;
    u8 kind;
    bool discharged;
} MsRuntimeAccess;

typedef struct MsRuntimeAlloc {
    const IrInst *call;
    u32 site_id;
} MsRuntimeAlloc;

struct MsFunctionResult {
    Arena *arena;
    IrModule *module;
    IrFunc *function;
    IrDomTree *dom;
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
    MsRuntimeAccess *accesses;
    u32 naccesses;
    u32 access_cap;
    MsRuntimeAlloc *allocs;
    u32 nallocs;
};

static const IrInst *value_def(const IrFunc *function, IrOperand operand);

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

static IrOperand path_resolve_pointer_origin(const MsPath *path,
                                             IrOperand operand)
{
    u32 depth;

    operand = path_resolve_binding(path, operand);
    for (depth = 0; depth <= path->npointer_origins; depth++) {
        u32 i;

        for (i = 0; i < path->npointer_origins; i++)
            if (operand_equal(path->pointer_origins[i].subject, operand)) {
                operand = path->pointer_origins[i].origin;
                break;
            }
        if (i == path->npointer_origins)
            break;
    }
    return operand;
}

static void path_bind_pointer_origin(MsPath *path, IrOperand subject,
                                     IrOperand origin)
{
    u32 i;

    if (subject.type != IRT_PTR || origin.type != IRT_PTR)
        return;
    origin = path_resolve_pointer_origin(path, origin);
    if (operand_equal(subject, origin))
        return;
    for (i = 0; i < path->npointer_origins; i++)
        if (operand_equal(path->pointer_origins[i].subject, subject)) {
            path->pointer_origins[i].origin = origin;
            return;
        }
    if (path->npointer_origins == MS_MAX_POINTER_ORIGINS) {
        /* Origin entries are MUST facts, including explicit unknown
           tombstones.  Evicting one would make absence fall back to the
           flow-insensitive alias graph and could revive a killed identity. */
        path->npointer_origins = 0;
        path->pointer_origins_lost = true;
        return;
    }
    path->pointer_origins[path->npointer_origins++] =
        (MsPointerOrigin){subject, origin};
}

static void path_forget_pointer_origin(MsPath *path, IrOperand subject)
{
    u32 i;

    for (i = 0; i < path->npointer_origins; i++) {
        if (!operand_equal(path->pointer_origins[i].subject, subject))
            continue;
        memmove(&path->pointer_origins[i], &path->pointer_origins[i + 1],
                (path->npointer_origins - i - 1) *
                    sizeof(path->pointer_origins[0]));
        path->npointer_origins--;
        return;
    }
}

static bool operand_defined_in_loop(const MsFunctionResult *result,
                                    IrOperand operand, BlockId header)
{
    BlockId defined;

    if (!result->dom || operand.kind != IROP_VALUE || operand.a == 0 ||
        operand.a > result->function->nvals)
        return false;
    defined = result->function->vals[operand.a - 1].def_block;
    return defined.v && ir_dominates(result->dom, header, defined);
}

static void path_forget_loop_correlations(const MsFunctionResult *result,
                                          MsPath *path, BlockId header)
{
    u32 read, write = 0;

    for (read = 0; read < path->npredicates; read++)
        if (!operand_defined_in_loop(result, path->predicates[read].subject,
                                     header))
            path->predicates[write++] = path->predicates[read];
    path->npredicates = write;

    write = 0;
    for (read = 0; read < path->nbindings; read++) {
        IrOperand param =
            ir_op_value(result->function, path->bindings[read].param);

        if (!operand_defined_in_loop(result, param, header))
            path->bindings[write++] = path->bindings[read];
    }
    path->nbindings = write;

    write = 0;
    for (read = 0; read < path->npointer_origins; read++)
        if (!operand_defined_in_loop(
                result, path->pointer_origins[read].subject, header))
            path->pointer_origins[write++] = path->pointer_origins[read];
    path->npointer_origins = write;
}

static IrOperand unknown_pointer_origin(void)
{
    IrOperand unknown = {0};

    unknown.type = IRT_PTR;
    return unknown;
}

static bool pointer_origins_equal(const MsPath *a, const MsPath *b)
{
    u32 i;

    if (a->npointer_origins != b->npointer_origins)
        return false;
    for (i = 0; i < a->npointer_origins; i++)
        if (!operand_equal(a->pointer_origins[i].subject,
                           b->pointer_origins[i].subject) ||
            !operand_equal(a->pointer_origins[i].origin,
                           b->pointer_origins[i].origin))
            return false;
    return true;
}

static bool path_equal(const MsFunctionResult *result, const MsPath *a,
                       const MsPath *b)
{
    u32 i;

    if (a->correlations_lost != b->correlations_lost ||
        a->pointer_origins_lost != b->pointer_origins_lost ||
        !predicates_equal(a, b) || !bindings_equal(a, b) ||
        !pointer_origins_equal(a, b))
        return false;
    for (i = 0; i < result->nsites; i++)
        if (a->facts[i].state != b->facts[i].state ||
            a->facts[i].realloc_old_site != b->facts[i].realloc_old_site ||
            a->facts[i].realloc_pending != b->facts[i].realloc_pending ||
            a->facts[i].alloc_success != b->facts[i].alloc_success ||
            a->facts[i].alloc_status_value != b->facts[i].alloc_status_value ||
            a->facts[i].nonnull_proven != b->facts[i].nonnull_proven ||
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
    path->npointer_origins = 0;
    path->pointer_origins_lost = true;
    for (i = 0; i < result->nsites; i++) {
        path->facts[i].state = MS_UNKNOWN;
        trace_clear(&path->facts[i]);
        path->facts[i].realloc_old_site = -1;
        path->facts[i].realloc_pending = false;
        path->facts[i].alloc_success = MS_ALLOC_SUCCESS_DIRECT;
        path->facts[i].alloc_status_value = 0;
        path->facts[i].nonnull_proven = false;
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
    bool keep_pointer_origins = pointer_origins_equal(dst, src);
    bool lose_pointer_origins = dst->pointer_origins_lost ||
                                src->pointer_origins_lost ||
                                !keep_pointer_origins;
    bool lose_correlations = dst->correlations_lost || src->correlations_lost ||
                             !keep_predicates || !keep_bindings;
    bool changed = (!keep_predicates && dst->npredicates != 0) ||
                   (!keep_bindings && dst->nbindings != 0);
    u32 i;

    if (!keep_predicates)
        dst->npredicates = 0;
    if (!keep_bindings)
        dst->nbindings = 0;
    if (!keep_pointer_origins) {
        dst->npointer_origins = 0;
        changed = true;
    }
    if (lose_pointer_origins != dst->pointer_origins_lost) {
        dst->pointer_origins_lost = lose_pointer_origins;
        changed = true;
    }
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
        bool nonnull_proven = !lose_correlations &&
                              dst->facts[i].nonnull_proven &&
                              src->facts[i].nonnull_proven;
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
        if (dst->facts[i].nonnull_proven != nonnull_proven) {
            dst->facts[i].nonnull_proven = nonnull_proven;
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

static bool is_runtime_index_check(const IrModule *module, const IrInst *in)
{
    return in && in->op == IR_CALL && in->subop == FUNCREF_EXTERNAL &&
           in->callee < module->nsyms && in->nops == 6 &&
           strcmp(module->syms[in->callee], "cgf_safe_check_index") == 0 &&
           in->ops[0].type == IRT_PTR && in->ops[1].type == IRT_I64 &&
           in->ops[2].type == IRT_I64 && in->ops[3].type == IRT_I32 &&
           in->ops[4].type == IRT_PTR && in->ops[5].type == IRT_I32;
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
        path->facts[i].nonnull_proven = false;
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

static bool isolated_local_pointer_slot(const IrFunc *function, IrOperand slot)
{
    const IrInst *def;
    u32 bi;

    if (slot.kind != IROP_VALUE || !(def = value_def(function, slot)) ||
        def->op != IR_ALLOCA)
        return false;
    for (bi = 0; bi < function->nblocks; bi++) {
        const IrInst *in;

        for (in = function->blocks[bi].first; in; in = in->next) {
            u32 i;

            for (i = 0; i < in->nops; i++) {
                if (!operand_equal(in->ops[i], slot))
                    continue;
                if ((in->op == IR_LOAD && i == 0) ||
                    (in->op == IR_STORE && i == 1))
                    continue;
                return false;
            }
        }
    }
    return true;
}

static void path_track_pointer_store(const MsFunctionResult *result,
                                     MsPath *path, const IrInst *store)
{
    if (store->nops != 2 ||
        !isolated_local_pointer_slot(result->function, store->ops[1]))
        return;

    /* A slot origin is a path-local MUST fact.  Every store kills the old
       fact, including a non-pointer overwrite; retaining it would let the
       MS-M-02 equality proof prune a feasible leak path. */
    path_forget_pointer_origin(path, store->ops[1]);
    if (store->ops[0].type == IRT_PTR) {
        path_bind_pointer_origin(path, store->ops[1], store->ops[0]);
    } else {
        /* Keep an explicit unknown origin.  Mere absence would fall back to
           flow-insensitive alias data for a later pointer-typed reload. */
        path_bind_pointer_origin(path, store->ops[1], unknown_pointer_origin());
    }
}

static void path_track_pointer_load(MsPath *path, const IrInst *load)
{
    IrOperand subject = {0};
    u32 i;

    if (!load->result.v || load->type != IRT_PTR || load->nops != 1)
        return;
    for (i = 0; i < path->npointer_origins; i++)
        if (operand_equal(path->pointer_origins[i].subject, load->ops[0]))
            break;
    if (i == path->npointer_origins)
        return;
    subject.kind = IROP_VALUE;
    subject.type = IRT_PTR;
    subject.a = load->result.v;
    path_bind_pointer_origin(path, subject, load->ops[0]);
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

static bool operand_is_standard_stream(const MsFunctionResult *result,
                                       const MsPath *path, IrOperand operand)
{
    const IrInst *def;
    const char *name;

    operand = path_resolve_pointer_origin(path, operand);
    if (operand.kind != IROP_VALUE ||
        !(def = value_def(result->function, operand)) || def->op != IR_LOAD ||
        def->nops != 1 || def->ops[0].kind != IROP_SYMBOL ||
        def->ops[0].sym >= result->module->nsyms)
        return false;
    name = result->module->syms[def->ops[0].sym];
    return name && (!strcmp(name, "stdin") || !strcmp(name, "stdout") ||
                    !strcmp(name, "stderr"));
}

static bool operand_has_nonheap_identity(const MsFunctionResult *result,
                                         const MsPath *path, IrOperand operand)
{
    PtsSet pts = alias_points_to(result->alias,
                                 path_resolve_pointer_origin(path, operand));

    return alias_pts_must_be_nonheap(result->alias, pts) ||
           operand_is_standard_stream(result, path, operand);
}

static bool operand_has_live_allocation(const MsFunctionResult *result,
                                        const MsPath *path, IrOperand operand)
{
    const AllocSite *site = alias_pts_unique_alloc_site(
        result->alias,
        alias_points_to(result->alias,
                        path_resolve_pointer_origin(path, operand)));
    u32 id;

    if (!site || !(id = alias_alloc_site_id(site)))
        return false;
    return path->facts[id - 1].state == MS_ALLOCATED &&
           path->facts[id - 1].nonnull_proven;
}

static IrOperand null_base_subject(const IrFunc *function, const MsPath *path,
                                   IrOperand operand, u32 depth)
{
    const IrInst *def;

    operand = path_resolve_binding(path, operand);
    if (depth > function->nvals || operand.kind != IROP_VALUE)
        return operand;
    def = value_def(function, operand);
    if (def && def->nops >= 1 && def->op == IR_BITCAST &&
        def->ops[0].type == IRT_PTR)
        return null_base_subject(function, path, def->ops[0], depth + 1);
    if (def && def->nops >= 1 && def->op == IR_PTRADD)
        return null_base_subject(function, path, def->ops[0], depth + 1);
    return operand;
}

static IrOperand null_exact_subject(const IrFunc *function, const MsPath *path,
                                    IrOperand operand, u32 depth)
{
    const IrInst *def;

    operand = path_resolve_binding(path, operand);
    if (depth > function->nvals || operand.kind != IROP_VALUE)
        return operand;
    def = value_def(function, operand);
    if (def && def->nops >= 1 && def->op == IR_BITCAST &&
        def->ops[0].type == IRT_PTR)
        return null_exact_subject(function, path, def->ops[0], depth + 1);
    if (def && def->nops >= 2 && def->op == IR_PTRADD) {
        i64 offset;

        if (operand_constant(function, def->ops[1], &offset, 0) && offset == 0)
            return null_exact_subject(function, path, def->ops[0], depth + 1);
    }
    return operand;
}

static bool null_chain_contains_subject(const IrFunc *function,
                                        const MsPath *path, IrOperand operand,
                                        IrOperand subject, u32 depth)
{
    const IrInst *def;
    IrOperand exact;

    operand = path_resolve_binding(path, operand);
    if (depth > function->nvals)
        return false;
    exact = null_exact_subject(function, path, operand, depth);
    if (exact.kind == IROP_VALUE && operand_equal(exact, subject))
        return true;
    if (operand.kind != IROP_VALUE)
        return false;
    def = value_def(function, operand);
    if (!def || def->nops < 1 ||
        (def->op != IR_BITCAST && def->op != IR_PTRADD))
        return false;
    /* MS-C-04: walk only from the pointer being used toward its bases.  This
     * lets a null predicate on any intermediate prove later derivatives
     * invalid without unsoundly inferring a base null from a null derivative.
     */
    return null_chain_contains_subject(function, path, def->ops[0], subject,
                                       depth + 1);
}

static MsProofKind path_operand_has_null_base(const MsFunctionResult *result,
                                              const MsPath *path,
                                              IrOperand operand)
{
    IrOperand resolved = path_resolve_binding(path, operand);
    IrOperand exact = null_exact_subject(result->function, path, resolved, 0);
    IrOperand base = null_base_subject(result->function, path, resolved, 0);
    u32 i;

    if (operand_is_null(result->function, resolved) ||
        operand_is_null(result->function, exact) ||
        operand_is_null(result->function, base))
        return MS_PROOF_INDEPENDENT;
    if (path->correlations_lost)
        return MS_PROOF_NONE;
    for (i = 0; i < path->npredicates; i++) {
        const MsPredicate *predicate = &path->predicates[i];
        IrOperand known =
            null_exact_subject(result->function, path, predicate->subject, 0);

        if (predicate->equal && predicate->constant == 0 &&
            null_chain_contains_subject(result->function, path, resolved, known,
                                        0))
            return MS_PROOF_PATH;
    }
    return MS_PROOF_NONE;
}

static MsProofKind path_operand_is_zero(const MsFunctionResult *result,
                                        const MsPath *path, IrOperand operand)
{
    IrOperand resolved = path_resolve_binding(path, operand);
    i64 value;
    u32 i;

    if (operand_constant(result->function, resolved, &value, 0))
        return value == 0 ? MS_PROOF_INDEPENDENT : MS_PROOF_NONE;
    if (path->correlations_lost || resolved.kind != IROP_VALUE)
        return MS_PROOF_NONE;
    for (i = 0; i < path->npredicates; i++) {
        const MsPredicate *predicate = &path->predicates[i];

        if (predicate->equal && predicate->constant == 0 &&
            operand_equal(resolved, predicate->subject))
            return MS_PROOF_PATH;
    }
    return MS_PROOF_NONE;
}

static bool path_operand_constant(const MsFunctionResult *result,
                                  const MsPath *path, IrOperand operand,
                                  i64 *value)
{
    IrOperand resolved = path_resolve_binding(path, operand);
    u32 i;

    if (operand_constant(result->function, resolved, value, 0))
        return true;
    if (path->correlations_lost || resolved.kind != IROP_VALUE)
        return false;
    for (i = 0; i < path->npredicates; i++) {
        const MsPredicate *predicate = &path->predicates[i];

        if (predicate->equal && operand_equal(resolved, predicate->subject)) {
            *value = predicate->constant;
            return true;
        }
    }
    return false;
}

static bool path_operand_size(const MsFunctionResult *result,
                              const MsPath *path, IrOperand operand, u64 *size)
{
    i64 value;

    if (operand_constant(result->function, operand, &value, 0) && value >= 0) {
        *size = (u64)value;
        return true;
    }
    if (path_operand_is_zero(result, path, operand) != MS_PROOF_NONE) {
        *size = 0;
        return true;
    }
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

static bool family_extent(const MsFunctionResult *result, const MsPath *path,
                          const MsAllocFamily *family, const IrInst *call,
                          u64 *extent)
{
    IrOperand first_operand, second_operand;
    i64 first, second = 1;

    if (!family || family->size_arg == MS_NO_ARG ||
        !call_arg(call, family->size_arg, &first_operand))
        return false;
    if (family->size_arg2 != MS_NO_ARG) {
        if (!call_arg(call, family->size_arg2, &second_operand))
            return false;
        /* MS-C-04: either zero factor proves a zero multiplicative extent;
         * requiring the other factor to be constant would lose that fact. */
        if (path_operand_is_zero(result, path, first_operand) !=
                MS_PROOF_NONE ||
            path_operand_is_zero(result, path, second_operand) !=
                MS_PROOF_NONE) {
            *extent = 0;
            return true;
        }
        if (!operand_constant(result->function, second_operand, &second, 0) ||
            second < 0)
            return false;
    } else if (path_operand_is_zero(result, path, first_operand) !=
               MS_PROOF_NONE) {
        *extent = 0;
        return true;
    }
    if (!operand_constant(result->function, first_operand, &first, 0) ||
        first < 0)
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

static bool runtime_access_proven(MsFunctionResult *result, const MsPath *path,
                                  PtsSet pts, const AllocSite *unique,
                                  IrOperand pointer, bool size_known, u64 size)
{
    const MsFact *fact;
    i64 lo, hi;
    u32 site;

    /* A zero-byte library operation observes no storage.  Stack and global
     * objects are deliberately outside Sprint 44's heap runtime; proving the
     * pointer is non-heap discharges the otherwise useless magic probe. */
    if (size_known && size == 0)
        return true;
    if (alias_pts_must_be_nonheap(result->alias, pts))
        return true;
    if (!unique || path->correlations_lost || !size_known)
        return false;
    site = alias_alloc_site_id(unique) - 1;
    fact = &path->facts[site];
    if (fact->state != MS_ALLOCATED || !fact->nonnull_proven ||
        !fact->extent_known || !access_range(result, pointer, size, &lo, &hi))
        return false;
    return lo >= 0 && hi >= lo && (u64)hi <= fact->extent;
}

static void record_runtime_access(MsFunctionResult *result, const MsPath *path,
                                  const IrInst *inst, u8 slot,
                                  IrOperand pointer, IrOperand size_operand,
                                  bool size_known, u64 size, bool write,
                                  PtsSet pts, const AllocSite *unique)
{
    MsRuntimeAccess *access = NULL;
    bool proven = runtime_access_proven(result, path, pts, unique, pointer,
                                        size_known, size);
    u32 i;

    for (i = 0; i < result->naccesses; i++)
        if (result->accesses[i].inst == inst &&
            result->accesses[i].slot == slot) {
            access = &result->accesses[i];
            break;
        }
    if (!access) {
        if (result->naccesses == result->access_cap) {
            u32 nc = result->access_cap ? result->access_cap * 2 : 16;
            MsRuntimeAccess *grown = arena_alloc(
                result->arena, nc * sizeof(*grown), _Alignof(MsRuntimeAccess));

            if (result->naccesses)
                memcpy(grown, result->accesses,
                       result->naccesses * sizeof(*grown));
            result->accesses = grown;
            result->access_cap = nc;
        }
        access = &result->accesses[result->naccesses++];
        memset(access, 0, sizeof(*access));
        access->inst = inst;
        access->slot = slot;
        access->pointer = pointer;
        access->size = size_operand;
        access->kind = write ? MS_RUNTIME_WRITE : MS_RUNTIME_READ;
        /* Access-site identity is independent of pointer provenance. The
         * allocation header reports its own allocation-site ID. */
        access->site_id = result->naccesses;
        access->discharged = proven;
        return;
    }
    access->discharged = access->discharged && proven;
}

static bool process_access(MsFunctionResult *result, MsPath *path,
                           const IrInst *inst, u8 slot, IrOperand pointer,
                           IrOperand size_operand, bool size_known, u64 size,
                           bool write, bool check_uninit, BlockId access_block,
                           Span loc, const char *note)
{
    PtsSet pts = alias_points_to(result->alias, pointer);
    const AllocSite *unique = alias_pts_unique_alloc_site(result->alias, pts);
    MsProofKind null_proof = path_operand_has_null_base(result, path, pointer);
    bool oob = false;

    /* MS-C-04: a proven-null access is a compile-time fact, not runtime-check
     * residue. Keep zero-byte library operations exempt because they do not
     * access storage, even when their pointer value is null. An independent
     * literal proof survives discarded path correlations. */
    if ((!size_known || size != 0) && null_proof != MS_PROOF_NONE)
        add_issue(result, null_proof == MS_PROOF_INDEPENDENT ? NULL : path,
                  MS_ISSUE_NULL_DEREFERENCE, loc, -1, false, NULL);
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
    if (inst)
        record_runtime_access(result, path, inst, slot, pointer, size_operand,
                              size_known, size, write, pts, unique);
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
                      const MsPath *path, u32 first_arg, u32 second_arg,
                      u64 *size)
{
    IrOperand first_operand, second_operand;
    i64 a, b = 1;

    if (!call_arg(call, first_arg, &first_operand))
        return false;
    if (second_arg != MS_NO_ARG) {
        if (!call_arg(call, second_arg, &second_operand))
            return false;
        /* MS-C-04: a current-path zero in either factor makes the operation
         * byte-free even when the other factor is unknown. */
        if (path_operand_is_zero(result, path, first_operand) !=
                MS_PROOF_NONE ||
            path_operand_is_zero(result, path, second_operand) !=
                MS_PROOF_NONE) {
            *size = 0;
            return true;
        }
        if (!operand_constant(result->function, second_operand, &b, 0) || b < 0)
            return false;
    } else if (path_operand_is_zero(result, path, first_operand) !=
               MS_PROOF_NONE) {
        *size = 0;
        return true;
    }
    if (!operand_constant(result->function, first_operand, &a, 0) || a < 0)
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
    IrOperand size_operand = ir_op_iconst(IRT_I64, 1);
    u64 size = 0;
    bool known;

    if (!name)
        return false;
    if (strcmp(name, "memcpy") == 0 || strcmp(name, "memmove") == 0) {
        if (!call_arg(call, 0, &dst) || !call_arg(call, 1, &src))
            return false;
        known = call_size(result, call, path, 2, MS_NO_ARG, &size);
        (void)call_arg(call, 2, &size_operand);
        process_access(result, path, NULL, 0, dst, size_operand, known, size,
                       true, true, block, loc, "wrote through pointer here");
        process_access(result, path, NULL, 1, src, size_operand, known, size,
                       false, true, block, loc, "read through pointer here");
        return true;
    }
    if (strcmp(name, "memset") == 0) {
        if (!call_arg(call, 0, &dst))
            return false;
        known = call_size(result, call, path, 2, MS_NO_ARG, &size);
        (void)call_arg(call, 2, &size_operand);
        process_access(result, path, NULL, 0, dst, size_operand, known, size,
                       true, true, block, loc, "wrote through pointer here");
        return true;
    }
    if (strcmp(name, "fread") == 0) {
        if (!call_arg(call, 0, &dst) || !call_arg(call, 3, &src))
            return false;
        known = call_size(result, call, path, 1, 2, &size);
        if (!call_arg(call, 1, &size_operand))
            size_operand = ir_op_iconst(IRT_I64, 1);
        process_access(result, path, NULL, 0, dst, size_operand, known, size,
                       true, true, block, loc,
                       "library call writes through pointer here");
        /* MS-C-04: a zero transfer extent exempts only the destination bytes.
         * fread still dereferences its independent FILE control object. */
        process_access(result, path, NULL, 3, src, ir_op_iconst(IRT_I64, 1),
                       false, 0, false, true, block, loc,
                       "library call reads through pointer here");
        return true;
    }
    if (strcmp(name, "snprintf") == 0) {
        if (!call_arg(call, 0, &dst))
            return false;
        known = call_size(result, call, path, 1, MS_NO_ARG, &size);
        (void)call_arg(call, 1, &size_operand);
        process_access(result, path, NULL, 0, dst, size_operand, known, size,
                       true, true, block, loc,
                       "library call writes through pointer here");
        if (call_arg(call, 2, &src))
            process_access(result, path, NULL, 1, src, ir_op_iconst(IRT_I64, 1),
                           false, 0, false, true, block, loc,
                           "library call reads through pointer here");
        return true;
    }
    return false;
}

static bool lib_extent_bounds_arg(const char *name, const MsLibSummary *lib,
                                  u32 arg)
{
    u64 bit = arg < 64 ? 1ull << arg : 0;

    if ((lib->write_mask & bit) != 0)
        return true;
    /* MS-C-04: the table's extent fields historically described writes, but
     * these two reviewed contracts bound a read-only source by the same byte
     * count.  Do not exempt unrelated dereferences such as snprintf's format
     * string or fread's FILE object when the destination extent is zero. */
    return (strcmp(name, "strncpy") == 0 && arg == 1) ||
           (strcmp(name, "bcopy") == 0 && arg == 0);
}

static void process_call(MsFunctionResult *result, MsPath *path,
                         const IrInst *call, BlockId block)
{
    const char *name = call_name(result->module, call);
    const MsAllocFamily *family = ms_alloc_family_for_call(name, call);
    const MsLibSummary *lib = ms_lib_summary_for_call(name, call);
    const AllocSite *created = alias_alloc_site(result->alias, call);
    Span loc = ir_inst_span(result->module, call);
    IrOperand operand;
    u64 extent = 0;
    bool extent_known = family_extent(result, path, family, call, &extent);
    u32 first, i;

    /* MS-C-05 lowering-only guard: it observes metadata but neither
       dereferences nor captures its pointer operands. */
    if (is_runtime_index_check(result->module, call))
        return;

    if (call->subop == FUNCREF_INDIRECT && call->nops >= 1) {
        MsProofKind proof =
            path_operand_has_null_base(result, path, call->ops[0]);

        /* MS-C-04: correlation degradation cannot erase a literal null
         * callee, but it must suppress conclusions that needed lost facts. */
        if (proof != MS_PROOF_NONE)
            add_issue(result, proof == MS_PROOF_INDEPENDENT ? NULL : path,
                      MS_ISSUE_NULL_DEREFERENCE, loc, -1, false, NULL);
    }

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
        path->facts[site].nonnull_proven = false;
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
        /* MS-M-03: freopen returns the same stream object it was given.  A
         * replacement of a standard stream therefore remains published by
         * stdin/stdout/stderr instead of becoming fresh local ownership. */
        if (family && family->is_file_resource && family->frees_on_success &&
            call_arg(call, family->frees_arg, &operand) &&
            operand_is_standard_stream(result, path, operand)) {
            fact->state = MS_ESCAPED;
            fact->realloc_pending = false;
            ms_trace_push(&fact->trace, loc, MS_EV_ESCAPE,
                          "reopened standard stream remains globally owned");
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
            call_size(result, call, path, (u32)lib->write_size_arg,
                      lib->write_size_arg2 >= 0 ? (u32)lib->write_size_arg2
                                                : MS_NO_ARG,
                      &lib_extent);

        if (handled) {
            first = call_first_arg(call);
            for (i = first; i < call->nops; i++) {
                u32 arg = i - first;
                bool deref = false, write = false, escape = false;
                bool may_free = false, must_free = false;
                bool requires_nonnull = false;
                IrOperand actual = call->ops[i];

                if (summary && arg < summary->nparams) {
                    const MsParamSummary *e = &summary->params[arg];
                    deref = e->dereferenced;
                    write = e->written;
                    escape = e->escapes;
                    may_free = e->may_free;
                    must_free = e->must_free;
                    requires_nonnull = e->requires_nonnull;
                    if (summary->partial || summary->top) {
                        deref = true;
                        write = true;
                        requires_nonnull = true;
                        escape = !e->annot_no_escape && !e->annot_borrow;
                        if (!e->annot_borrow && !e->annot_takes_ownership)
                            may_free = true;
                    }
                } else if (summary && (summary->top || summary->partial)) {
                    deref = write = escape = actual.type == IRT_PTR;
                    requires_nonnull = actual.type == IRT_PTR;
                } else if (lib && arg < 64) {
                    u64 bit = 1ull << arg;
                    deref = (lib->deref_mask & bit) != 0;
                    write = (lib->write_mask & bit) != 0;
                    escape = (lib->escape_mask & bit) != 0;
                    may_free = (lib->free_mask & bit) != 0;
                    if (arg >= ms_lib_summary_variadic_from(lib))
                        deref = write = escape = true;
                    requires_nonnull = deref || write;
                }
                if (actual.type != IRT_PTR)
                    continue;
                /* The family transition already validated and consumed this
                 * argument before the generic libc effects are applied. */
                if (family && family->frees_on_success &&
                    arg == family->frees_arg)
                    continue;
                /* A may-dereference summary can still be explicitly
                 * null-safe: when the actual is proven zero, every access in
                 * the callee is dominated by its non-null guard and this path
                 * performs no memory operation. */
                if ((deref || write) && !requires_nonnull &&
                    path_operand_is_zero(result, path, actual) != MS_PROOF_NONE)
                    deref = write = false;
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
                } else if ((deref || write) && lib && lib_extent_known &&
                           lib_extent_bounds_arg(name, lib, arg)) {
                    process_access(
                        result, path, NULL, 0, actual,
                        ir_op_iconst(IRT_I64, (i64)lib_extent), true,
                        lib_extent, write, true, block, loc,
                        write ? "library call writes through pointer here"
                              : "library call reads through pointer here");
                } else if (deref || write) {
                    process_access(result, path, NULL, 0, actual,
                                   ir_op_iconst(IRT_I64, 1), false, 0, write,
                                   true, block, loc,
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
        if (in->nops >= 1) {
            path_track_pointer_load(path, in);
            process_access(
                result, path, in, 0, in->ops[0],
                ir_op_iconst(IRT_I64, (i64)ir_type_size((IrType)in->type)),
                true, ir_type_size((IrType)in->type), false, true, block, loc,
                "read through pointer here");
        }
        break;
    case IR_STORE:
        if (in->nops >= 2) {
            if (in->ops[0].type == IRT_PTR)
                process_pointer_value_use(result, path, in->ops[0], loc);
            process_access(result, path, in, 0, in->ops[1],
                           ir_op_iconst(IRT_I64, (i64)ir_type_size(
                                                     (IrType)in->ops[0].type)),
                           true, ir_type_size((IrType)in->ops[0].type), true,
                           true, block, loc, "wrote through pointer here");
            if (in->ops[0].type == IRT_PTR &&
                !local_address(result->function, in->ops[1], 0))
                transition_reachable(result, path, in->ops[0], MS_ACT_ESCAPE,
                                     loc, MS_EV_ESCAPE,
                                     "stored into escaping memory here");
            path_track_pointer_store(result, path, in);
        }
        break;
    case IR_ASM: {
        /* A template is OPAQUE, so an asm is an unknown call for ownership:
         * every pointer it is handed may be stored anywhere, and an output
         * operand is an ADDRESS the template writes through (see ir.h), so
         * both directions escape. Without this row
         * `p = malloc(16); asm("" :: "r"(p));` is diagnosed a LEAK, which is
         * a false positive on the shape every libc's arch/ directory uses.
         *
         * alias.c's mark_escapes needs the SAME row and is not a substitute
         * for it -- that one keeps the points-to service honest, this one is
         * what the lifetime lattice reads. Fixing only alias.c leaves the
         * warning exactly where it was; measured. */
        u32 oi;

        for (oi = 0; oi < in->nops; oi++)
            if (in->ops[oi].type == IRT_PTR)
                transition_reachable(result, path, in->ops[oi], MS_ACT_ESCAPE,
                                     loc, MS_EV_ESCAPE,
                                     "passed to inline asm here");
        break;
    }
    case IR_VA_START:
        if (in->nops >= 1)
            /* SysV va_start fills the pointer fields after lowering has
             * stored the two offsets. Treat the backend expansion as one
             * 24-byte write so terminal residue protects every field. */
            process_access(result, path, in, 0, in->ops[0],
                           ir_op_iconst(IRT_I64, 24), true, 24, true, true,
                           block, loc, "initialized va_list here");
        break;
    case IR_MEMCPY:
        if (in->nops >= 2) {
            u64 value = 0;
            const IrMemLayout *layout;
            bool source_oob;
            /* MS-C-04: direct IR memory operations consume the same current-
             * path zero proof as summarized library calls. */
            bool known = in->nops >= 3 &&
                         path_operand_size(result, path, in->ops[2], &value);

            layout =
                known ? ir_mem_layout_find(result->module, in, value) : NULL;

            /* The source is observed before the destination changes.  This
             * is load-before-store even for `*p = *p`, and preserves missing
             * member state when the destination is another heap object. */
            source_oob = process_access(
                result, path, in, 0, in->ops[1],
                in->nops >= 3 ? in->ops[2] : ir_op_iconst(IRT_I64, 0), known,
                value, false, false, block, loc, "read through pointer here");
            if (!source_oob && layout)
                process_aggregate_uninit(result, path, in->ops[1], layout, loc);
            process_access(result, path, in, 1, in->ops[0],
                           in->nops >= 3 ? in->ops[2]
                                         : ir_op_iconst(IRT_I64, 0),
                           known, value, layout == NULL, false, block, loc,
                           "wrote through pointer here");
            if (layout)
                mark_aggregate_copy(result, path, in->ops[0], in->ops[1],
                                    layout, loc);
        }
        break;
    case IR_MEMSET:
        if (in->nops >= 1) {
            u64 value = 0;
            bool known = in->nops >= 3 &&
                         path_operand_size(result, path, in->ops[2], &value);

            process_access(
                result, path, in, 0, in->ops[0],
                in->nops >= 3 ? in->ops[2] : ir_op_iconst(IRT_I64, 0), known,
                value, true, true, block, loc, "wrote through pointer here");
        }
        break;
    case IR_ATOMICRMW:
        if (in->nops >= 1)
            process_access(
                result, path, in, 0, in->ops[0],
                ir_op_iconst(IRT_I64, (i64)ir_type_size((IrType)in->type)),
                true, ir_type_size((IrType)in->type), true, true, block, loc,
                "accessed through atomic pointer here");
        break;
    case IR_CMPXCHG:
        if (in->nops >= 1)
            process_access(
                result, path, in, 0, in->ops[0],
                ir_op_iconst(IRT_I64, (i64)ir_type_size((IrType)in->type)),
                true, ir_type_size((IrType)in->type), true, true, block, loc,
                "accessed through atomic pointer here");
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
        path->facts[site].nonnull_proven = nonnull;
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
    path->facts[site].nonnull_proven = nonnull && after == MS_ALLOCATED;
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
        fact->nonnull_proven = success;
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

static bool path_edge_feasible(const MsFunctionResult *result,
                               const MsPath *path, const IrInst *term, u32 edge)
{
    const IrFunc *function = result->function;
    IrOperand condition;
    i64 constant;

    if (!opt_cfg_edge_feasible(function, term, edge))
        return false;
    if (term->op != IR_CONDBR || term->nops != 1 || edge > 1)
        return true;
    condition = path_resolve_binding(path, term->ops[0]);
    if (operand_constant(function, condition, &constant, 0))
        return (constant != 0) == (edge == 0);
    {
        const IrInst *cmp = value_def(function, condition);
        IrOperand folded_value;
        IrOperand folded_ops[2];
        IrInst folded;
        OptConfig cfg;
        i64 values[2];
        bool known[2];
        u32 i;

        /* Resolve both block-parameter bindings and exact path predicates,
         * then let the shared integer folder evaluate every ICMP flavor.  A
         * branch such as `count != 0` can make a later `0 < count` edge
         * impossible even though the raw SSA operands are not constants. */
        if (cmp && cmp->op == IR_ICMP && cmp->nops == 2) {
            folded = *cmp;
            folded.ops = folded_ops;
            for (i = 0; i < 2; i++) {
                known[i] = path_operand_constant(result, path, cmp->ops[i],
                                                 &values[i]);
                if (known[i])
                    folded_ops[i] =
                        ir_op_iconst((IrType)cmp->ops[i].type, (u64)values[i]);
            }
            /* Unsigned zero is the least representable value regardless of
             * width.  These one-sided comparisons therefore have an exact
             * result even when the other operand remains path-variable. */
            if ((!known[0] && known[1] && values[1] == 0 &&
                 (cmp->subop == ICMP_ULT || cmp->subop == ICMP_UGE)) ||
                (known[0] && values[0] == 0 && !known[1] &&
                 (cmp->subop == ICMP_UGT || cmp->subop == ICMP_ULE))) {
                bool truth = cmp->subop == ICMP_UGE || cmp->subop == ICMP_ULE;

                return truth == (edge == 0);
            }
            /* Pointer null constants are represented as ICONST operands but
             * deliberately are not integer-foldable.  Equality still has an
             * exact result once both bit patterns are known. */
            if (known[0] && known[1] &&
                (cmp->subop == ICMP_EQ || cmp->subop == ICMP_NE) &&
                cmp->ops[0].type == IRT_PTR) {
                bool truth = cmp->subop == ICMP_EQ ? values[0] == values[1]
                                                   : values[0] != values[1];

                return truth == (edge == 0);
            }
            opt_config_init(&cfg, OPT_O0);
            if (known[0] && known[1] &&
                opt_fold_inst(&folded, &folded_value, &cfg) &&
                folded_value.kind == IROP_ICONST)
                return (folded_value.a != 0) == (edge == 0);
        }

        /* MS-M-02: a non-null live allocation cannot equal a standard stream
         * or another proven non-heap identity.  Pruning that equality edge
         * prevents an infeasible path from bypassing the matching close. */
        if (!path->pointer_origins_lost && cmp && cmp->op == IR_ICMP &&
            cmp->nops == 2 &&
            (cmp->subop == ICMP_EQ || cmp->subop == ICMP_NE) &&
            cmp->ops[0].type == IRT_PTR && cmp->ops[1].type == IRT_PTR) {
            IrOperand lhs = path_resolve_pointer_origin(path, cmp->ops[0]);
            IrOperand rhs = path_resolve_pointer_origin(path, cmp->ops[1]);
            bool lhs_live = operand_has_live_allocation(result, path, lhs);
            bool rhs_live = operand_has_live_allocation(result, path, rhs);
            bool equality_edge = (cmp->subop == ICMP_EQ) == (edge == 0);

            if (equality_edge &&
                ((lhs_live &&
                  operand_has_nonheap_identity(result, path, rhs)) ||
                 (rhs_live && operand_has_nonheap_identity(result, path, lhs))))
                return false;
        }
    }
    return true;
}

static bool path_bind_target(MsFunctionResult *result, MsPath *path,
                             const IrEdge *edge, const IrBlock *target,
                             bool backedge)
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
    if (backedge) {
        /* A loop parameter receives a new dynamic value even though its IR id
         * is reused.  Binding it to an operand defined in the loop body lets
         * next-iteration predicates walk backward through the previous
         * iteration's static definition.  Treat loop-carried values as
         * unknown at the header, while preserving allocation-state facts. */
        return true;
    }
    /* A block parameter remains the current SSA value throughout ordinary
     * successor blocks.  Retain its substitution until another parameterized
     * edge replaces it. */
    if (target->nparams == 0)
        return true;
    for (i = 0; i < target->nparams; i++) {
        IrOperand subject = ir_op_value(result->function, target->params[i]);

        next[i].param = target->params[i];
        next[i].incoming = path_resolve_binding(path, edge->args[i]);
        if (subject.type == IRT_PTR)
            path_bind_pointer_origin(path, subject, edge->args[i]);
    }
    path->nbindings = target->nparams;
    if (target->nparams)
        memcpy(path->bindings, next, target->nparams * sizeof(next[0]));
    return true;
}

static u32 feasible_edge_count(const MsFunctionResult *result,
                               const MsPath *path, const IrInst *term)
{
    u32 count = 0, i;

    for (i = 0; i < term->nedges; i++)
        if (path_edge_feasible(result, path, term, i))
            count++;
    return count;
}

static void propagate_successors(MsFunctionResult *result, MsWorklist *work,
                                 const MsPath *source, const IrInst *term,
                                 u32 source_block)
{
    u32 feasible = feasible_edge_count(result, source, term);
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
        bool backedge;
        u32 target;

        if (!path_edge_feasible(result, source, term, i))
            continue;
        target = term->edges[i].target.v;
        if (!target || target > result->function->nblocks)
            continue;
        path = path_clone(result, source);
        backedge = result->dom && ir_dominates(result->dom, (BlockId){target},
                                               (BlockId){source_block + 1});
        /* Instruction and block-parameter ids defined inside the dominated
         * loop region denote a new dynamic value on every iteration.  Drop
         * only their correlations: bindings and predicates for values defined
         * before the loop remain invariant and keep infeasible edges pruned. */
        if (backedge)
            path_forget_loop_correlations(result, &path, (BlockId){target});
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
        /* A conditional backedge may have just added a predicate for its
         * current dynamic iteration.  It must not describe the reused SSA ids
         * when the loop header executes again. */
        if (backedge)
            path_forget_loop_correlations(result, &path, (BlockId){target});
        if (!path_bind_target(result, &path, &term->edges[i],
                              &result->function->blocks[target - 1], backedge))
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
            const MsLibSummary *lib = ms_lib_summary_for_call(callee_name, in);
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
            const MsLibSummary *lib = ms_lib_summary_for_call(callee_name, in);
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
    result->dom = ir_domtree_build(arena, function);
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
    result->allocs = arena_alloc(
        arena, (result->nsites ? result->nsites : 1) * sizeof(*result->allocs),
        _Alignof(MsRuntimeAlloc));
    for (i = 0; i < result->nsites; i++) {
        const AllocSite *site = alias_alloc_site_at(result->alias, i);
        const IrInst *call = alias_alloc_site_call(site);
        const char *name = call_name(module, call);

        result->families[i] = ms_alloc_family_for_call(name, call);
        if (name &&
            (!strcmp(name, "malloc") || !strcmp(name, "calloc") ||
             !strcmp(name, "realloc") || !strcmp(name, "reallocarray") ||
             !strcmp(name, "strdup") || !strcmp(name, "strndup") ||
             !strcmp(name, "aligned_alloc") ||
             !strcmp(name, "posix_memalign"))) {
            result->allocs[result->nallocs++] =
                (MsRuntimeAlloc){call, alias_alloc_site_id(site)};
        }
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
            propagate_successors(result, &work, &path, in, item.block);
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
        WARN_MEM_USE_AFTER_FREE, WARN_MEM_DOUBLE_FREE,  WARN_MEM_LEAK,
        WARN_MEM_OUT_OF_BOUNDS,  WARN_MEM_UNINIT_READ,  WARN_MEM_FREE_NONHEAP,
        WARN_MEM_REALLOC_ZERO,   WARN_NULL_DEREFERENCE,
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
    case MS_ISSUE_NULL_DEREFERENCE:
        return "dereference of a pointer proven to be null";
    case MS_ISSUE_COUNT:
        break;
    }
    return "memory-safety issue";
}

static bool source_span_offset(const DiagCtx *dc, Span span, size_t *offset,
                               size_t *line_start)
{
    const char *source;
    size_t len, pos = 0;
    u32 line = 1, col = 1;

    source = diag_file_source(dc, span.file_id, &len);
    if (!source || !span.line || !span.col)
        return false;
    while (pos < len && line < span.line)
        if (source[pos++] == '\n')
            line++;
    if (line != span.line)
        return false;
    *line_start = pos;
    while (pos < len && source[pos] != '\n' && col < span.col) {
        pos++;
        col++;
    }
    if (col != span.col)
        return false;
    *offset = pos;
    return true;
}

static bool same_span_start(Span a, Span b)
{
    return a.file_id == b.file_id && a.line == b.line && a.col == b.col;
}

static const AstNode *strip_expr(const AstNode *e);

static bool direct_return_body(const AstNode *st, Span target)
{
    if (!st)
        return false;
    if (st->kind == AST_STMT_RETURN)
        return same_span_start(st->span, target);
    return st->kind == AST_STMT_COMPOUND && st->nitems == 1 && st->items[0] &&
           st->items[0]->kind == AST_STMT_RETURN &&
           same_span_start(st->items[0]->span, target);
}

static bool decl_chain_contains(const AstNode *decl, const AstNode *wanted)
{
    u32 i;

    if (!decl)
        return false;
    if (decl == wanted)
        return true;
    for (i = 0; i < decl->nitems; i++)
        if (decl_chain_contains(decl->items[i], wanted))
            return true;
    return false;
}

static bool node_changes_binding(const AstNode *n, const struct Symbol *binding)
{
    const AstNode *lhs;
    u32 i;

    if (!n)
        return false;
    if (n->kind == AST_EXPR_BINARY && n->op >= PUNCT_ASSIGN &&
        n->op <= PUNCT_PIPE_ASSIGN) {
        lhs = strip_expr(n->lhs);
        if (lhs && lhs->kind == AST_EXPR_IDENT && lhs->sym == binding)
            return true;
    }
    if (n->kind == AST_EXPR_UNARY &&
        (n->op == PUNCT_PLUSPLUS || n->op == PUNCT_MINUSMINUS ||
         n->op == PUNCT_AMP)) {
        lhs = strip_expr(n->lhs);
        if (lhs && lhs->kind == AST_EXPR_IDENT && lhs->sym == binding)
            return true;
    }
    if (node_changes_binding(n->body, binding) ||
        node_changes_binding(n->lhs, binding) ||
        node_changes_binding(n->rhs, binding) ||
        node_changes_binding(n->mid, binding) ||
        node_changes_binding(n->init, binding))
        return true;
    for (i = 0; i < n->nitems; i++)
        if (node_changes_binding(n->items[i], binding))
            return true;
    for (i = 0; i < n->nargs; i++)
        if (node_changes_binding(n->args[i], binding))
            return true;
    return false;
}

static bool compound_has_safe_early_return(const AstNode *compound,
                                           const AstNode *decl, Span target)
{
    u32 i, j;

    if (!compound || compound->kind != AST_STMT_COMPOUND)
        return false;
    for (i = 0; i < compound->nitems; i++) {
        const AstNode *item = compound->items[i];

        if (item && item->kind == AST_STMT_DECL &&
            decl_chain_contains(item->lhs, decl)) {
            for (j = i + 1; j < compound->nitems; j++) {
                const AstNode *later = compound->items[j];

                if (later && later->kind == AST_STMT_IF &&
                    direct_return_body(later->body, target))
                    return !node_changes_binding(later->lhs, decl->sym);
                if (node_changes_binding(later, decl->sym))
                    return false;
                /* Do not carry source-level binding claims through another
                 * control-flow construct before the target guard. */
                if (later && later->kind != AST_STMT_EXPR &&
                    later->kind != AST_STMT_DECL &&
                    later->kind != AST_STMT_NULL)
                    return false;
            }
            return false;
        }
        if (item && item->kind == AST_STMT_COMPOUND &&
            compound_has_safe_early_return(item, decl, target))
            return true;
    }
    return false;
}

static bool node_declares_name(const AstNode *n, const char *name)
{
    u32 i;

    if (!n)
        return false;
    if (n->kind == AST_DECL && n->name && strcmp(n->name, name) == 0)
        return true;
    if (node_declares_name(n->body, name) || node_declares_name(n->lhs, name) ||
        node_declares_name(n->rhs, name) || node_declares_name(n->mid, name) ||
        node_declares_name(n->init, name))
        return true;
    for (i = 0; i < n->nitems; i++)
        if (node_declares_name(n->items[i], name))
            return true;
    for (i = 0; i < n->nargs; i++)
        if (node_declares_name(n->args[i], name))
            return true;
    return false;
}

static bool function_declares_name(const AstNode *function, const char *name)
{
    u32 i;

    if (!function || function->kind != AST_FUNC_DEF)
        return false;
    for (i = 0; i < function->nparam_syms; i++)
        if (function->param_syms[i] && function->param_syms[i]->name &&
            strcmp(function->param_syms[i]->name, name) == 0)
            return true;
    return node_declares_name(function->body, name);
}

static bool tu_has_safe_early_return(const AstNode *tu, const AstNode *decl,
                                     Span target)
{
    u32 i;

    if (!tu || !decl || !decl->sym || tu->kind != AST_TRANSLATION_UNIT)
        return false;
    for (i = 0; i < tu->ndecls; i++)
        if (tu->decls[i] && tu->decls[i]->kind == AST_FUNC_DEF &&
            compound_has_safe_early_return(tu->decls[i]->body, decl, target)) {
            /* The inserted call must still name the external deallocator.
             * A parameter or block declaration can shadow it even when the
             * original function never spells `free` itself. */
            return !function_declares_name(tu->decls[i], "free");
        }
    return false;
}

static bool free_prototype_type(const Type *type)
{
    const Type *param;

    if (!type || type->kind != TY_FUNC || !type->base ||
        type->base->kind != TY_VOID || !type->has_proto || type->variadic ||
        type->nparams != 1)
        return false;
    param = type->params[0];
    return param && param->kind == TY_PTR && param->base &&
           param->base->kind == TY_VOID;
}

static void scan_free_declarations(const AstNode *decl, bool *compatible,
                                   bool *defined, u32 insertion_seq)
{
    u32 i;

    if (!decl)
        return;
    if ((decl->kind == AST_DECL || decl->kind == AST_FUNC_DEF) && decl->name &&
        strcmp(decl->name, "free") == 0 && decl->sym) {
        if (decl->sym->kind != SYM_FUNC ||
            decl->sym->linkage != LINK_EXTERNAL || decl->sym->defined) {
            *defined = true;
        } else if (decl->span.seq && decl->span.seq < insertion_seq &&
                   free_prototype_type(decl->sem_type)) {
            *compatible = true;
        }
    }
    for (i = 0; i < decl->nitems; i++)
        scan_free_declarations(decl->items[i], compatible, defined,
                               insertion_seq);
}

static bool tu_has_external_free_prototype(const AstNode *tu, u32 insertion_seq)
{
    bool compatible = false, defined = false;
    u32 i;

    if (!tu || tu->kind != AST_TRANSLATION_UNIT || !insertion_seq)
        return false;
    for (i = 0; i < tu->ndecls; i++)
        scan_free_declarations(tu->decls[i], &compatible, &defined,
                               insertion_seq);
    return compatible && !defined;
}

static const AstNode *strip_expr(const AstNode *e)
{
    while (e && (e->kind == AST_EXPR_PAREN ||
                 (e->kind == AST_EXPR_CAST && e->implicit)))
        e = e->lhs;
    return e;
}

static bool expr_has_allocator_on_line(const AstNode *e, Span allocation)
{
    const AstNode *callee;
    u32 i;

    if (!e)
        return false;
    if (e->kind == AST_EXPR_CALL && e->span.file_id == allocation.file_id &&
        e->span.line == allocation.line) {
        callee = strip_expr(e->lhs);
        if (callee && callee->kind == AST_EXPR_IDENT && callee->name &&
            (strcmp(callee->name, "malloc") == 0 ||
             strcmp(callee->name, "calloc") == 0 ||
             strcmp(callee->name, "realloc") == 0 ||
             strcmp(callee->name, "aligned_alloc") == 0))
            return true;
    }
    if (expr_has_allocator_on_line(e->lhs, allocation) ||
        expr_has_allocator_on_line(e->rhs, allocation) ||
        expr_has_allocator_on_line(e->mid, allocation) ||
        expr_has_allocator_on_line(e->init, allocation))
        return true;
    for (i = 0; i < e->nargs; i++)
        if (expr_has_allocator_on_line(e->args[i], allocation))
            return true;
    for (i = 0; i < e->nitems; i++)
        if (expr_has_allocator_on_line(e->items[i], allocation))
            return true;
    return false;
}

static void collect_leak_bindings(const AstNode *n, Span allocation,
                                  const char **binding,
                                  const AstNode **binding_decl, u32 *count)
{
    u32 i;

    if (!n)
        return;
    if (n->kind == AST_DECL && n->name && n->init &&
        expr_has_allocator_on_line(n->init, allocation)) {
        *binding = n->name;
        *binding_decl = n;
        (*count)++;
    }
    collect_leak_bindings(n->body, allocation, binding, binding_decl, count);
    collect_leak_bindings(n->lhs, allocation, binding, binding_decl, count);
    collect_leak_bindings(n->rhs, allocation, binding, binding_decl, count);
    collect_leak_bindings(n->mid, allocation, binding, binding_decl, count);
    if (n->kind != AST_DECL)
        collect_leak_bindings(n->init, allocation, binding, binding_decl,
                              count);
    for (i = 0; i < n->nitems; i++)
        collect_leak_bindings(n->items[i], allocation, binding, binding_decl,
                              count);
    for (i = 0; i < n->nargs; i++)
        collect_leak_bindings(n->args[i], allocation, binding, binding_decl,
                              count);
    for (i = 0; i < n->ndecls; i++)
        collect_leak_bindings(n->decls[i], allocation, binding, binding_decl,
                              count);
}

/* Bind the allocation to a source declaration through the typed AST. Textual
 * `first '=' on the line` recovery can name the wrong object when declarations
 * share a line. Ambiguous multiple allocator declarations are suppressed. */
static bool leak_binding_name(const AstNode *tu, Span allocation, char *name,
                              size_t name_cap, const AstNode **decl_out)
{
    const char *binding = NULL;
    const AstNode *binding_decl = NULL;
    u32 count = 0;
    size_t len;

    if (!tu || (allocation.origin & SPAN_ORIGIN_ANY_MACRO))
        return false;
    collect_leak_bindings(tu, allocation, &binding, &binding_decl, &count);
    if (count != 1 || !binding || !binding_decl || !binding_decl->sym)
        return false;
    len = strlen(binding);
    if (len + 1 > name_cap)
        return false;
    memcpy(name, binding, len + 1);
    *decl_out = binding_decl;
    return true;
}

static bool leak_fixit(WarnCtx *warnings, const MsIssue *issue,
                       const AstNode *tu, const Preprocessor *pp,
                       DiagFixit *fixit, char *replacement,
                       size_t replacement_cap, char *name, size_t name_cap)
{
    const DiagCtx *dc = warn_diag(warnings);
    const char *source;
    const AstNode *binding_decl = NULL;
    MsEvent allocation = {0};
    size_t len, at, line_start, indent;
    u32 i;
    int n;

    if (issue->kind != MS_ISSUE_LEAK || issue->file_resource || !pp ||
        !issue->loc.file_id || (issue->loc.origin & SPAN_ORIGIN_ANY_MACRO))
        return false;
    for (i = 0; i < issue->trace.len; i++) {
        MsEvent event;

        if (ms_trace_event(&issue->trace, i, &event) &&
            event.kind == MS_EV_ALLOC) {
            allocation = event;
            break;
        }
    }
    if (!allocation.loc.file_id ||
        !tu_has_external_free_prototype(tu, issue->loc.seq) ||
        pp_macro_lookup_at_seq(pp, "free", issue->loc.seq) ||
        !leak_binding_name(tu, allocation.loc, name, name_cap, &binding_decl) ||
        !tu_has_safe_early_return(tu, binding_decl, issue->loc) ||
        !source_span_offset(dc, issue->loc, &at, &line_start))
        return false;
    source = diag_file_source(dc, issue->loc.file_id, &len);
    (void)len;
    indent = at - line_start;
    if (indent > 128)
        return false;
    for (i = 0; i < indent; i++)
        if (source[line_start + i] != ' ' && source[line_start + i] != '\t')
            return false;
    n = snprintf(replacement, replacement_cap, "free(%s);\n%.*s", name,
                 (int)indent, source + line_start);
    if (n < 0 || (size_t)n >= replacement_cap)
        return false;
    memset(fixit, 0, sizeof(*fixit));
    fixit->where = issue->loc;
    fixit->where.len = 0;
    fixit->insert = replacement;
    fixit->machine_applicable = false;
    return true;
}

static void emit_issue(WarnCtx *warnings, const MsIssue *issue,
                       const AstNode *tu, const Preprocessor *pp)
{
    WarnId id = issue_warn_id(issue);
    DiagFixit fixit;
    char replacement[512], name[128];
    bool suggested;
    u32 i;

    if (id == WARN_NONE || !warn_enabled(warnings, id, issue->loc))
        return;
    suggested = leak_fixit(warnings, issue, tu, pp, &fixit, replacement,
                           sizeof(replacement), name, sizeof(name));
    if (suggested) {
        warn_at_fixits(warnings, id, issue->loc, &fixit, 1, "%s",
                       issue_message(issue));
        diag_emit(warn_diag(warnings), DIAG_NOTE, issue->loc,
                  "suggested fix: insert 'free(%s);' before the return", name);
    } else {
        warn_at(warnings, id, issue->loc, "%s", issue_message(issue));
    }
    for (i = 0; i < issue->trace.len; i++) {
        MsEvent event;

        if (ms_trace_event(&issue->trace, i, &event))
            diag_emit(warn_diag(warnings), DIAG_NOTE, event.loc, "%s",
                      event.note);
    }
}

static const char *event_name(MsEventKind kind);

static void dump_result(const MsFunctionResult *result, FILE *out)
{
    u32 bi, si, ei;

    fprintf(out, "memsafe function=%s sites=%u splits=%u degraded=%s\n",
            result->function->name, result->nsites, result->splits,
            result->degraded ? "may" : "no");
    for (bi = 0; bi < result->function->nblocks; bi++)
        fprintf(out, "block=%u name=%s states=%u\n", bi,
                result->function->blocks[bi].name, result->blocks[bi].nslots);
    for (si = 0; si < result->nsites; si++) {
        const AllocSite *site = alias_alloc_site_at(result->alias, si);
        const IrInst *call = alias_alloc_site_call(site);
        const char *name = call_name(result->module, call);

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
                        alias_alloc_site_id(site), ei, event_name(event.kind),
                        line, col, event.note);
            }
        }
    }
}

static void splice_runtime_check(MsFunctionResult *result,
                                 const MsRuntimeAccess *access, u32 callee)
{
    IrModule *module = result->module;
    IrFunc *function = result->function;
    IrInst *check =
        arena_alloc(module->arena, sizeof(*check), _Alignof(IrInst));
    IrOperand *args =
        arena_alloc(module->arena, 4 * sizeof(*args), _Alignof(IrOperand));
    u32 bi;

    memset(check, 0, sizeof(*check));
    args[0] = access->pointer;
    args[1] = access->size;
    args[2] = ir_op_iconst(IRT_I32, access->kind);
    args[3] = ir_op_iconst(IRT_I32, access->site_id);
    check->op = IR_CALL;
    check->type = IRT_VOID;
    check->subop = FUNCREF_EXTERNAL;
    check->callee = callee;
    check->loc = access->inst->loc;
    check->ops = args;
    check->nops = 4;

    for (bi = 0; bi < function->nblocks; bi++) {
        IrBlock *block = &function->blocks[bi];
        IrInst *in = block->first;
        IrInst *previous = NULL;

        while (in && in != access->inst) {
            previous = in;
            in = in->next;
        }
        if (!in)
            continue;
        check->next = in;
        if (previous)
            previous->next = check;
        else
            block->first = check;
        block->ninsts++;
        return;
    }
    CGF_ICE("memsafe instrumentation lost its target in @%s", function->name);
}

static void splice_runtime_derive(MsFunctionResult *result, IrInst *derived,
                                  u32 callee, u32 site_id)
{
    IrModule *module = result->module;
    IrOperand *args;
    IrInst *check;

    if (!derived || derived->op != IR_PTRADD || derived->nops < 2 ||
        derived->result.v == 0)
        return;
    check = arena_alloc(module->arena, sizeof(*check), _Alignof(IrInst));
    args = arena_alloc(module->arena, 4 * sizeof(*args), _Alignof(IrOperand));
    memset(check, 0, sizeof(*check));
    args[0] = derived->ops[0];
    args[1] = derived->ops[1];
    args[2] = ir_op_value(result->function, derived->result);
    args[3] = ir_op_iconst(IRT_I32, site_id);
    check->op = IR_CALL;
    check->type = IRT_VOID;
    check->subop = FUNCREF_EXTERNAL;
    check->callee = callee;
    check->loc = derived->loc;
    check->ops = args;
    check->nops = 4;
    check->next = derived->next;
    derived->next = check;
}

static bool round_trip_anchor(const IrFunc *function, IrOperand bits,
                              IrOperand *origin, u32 depth)
{
    const IrInst *def;
    IrOperand next;

    if (depth > function->nvals || bits.kind != IROP_VALUE)
        return false;
    def = value_def(function, bits);
    if (!def)
        return false;
    if (def->op == IR_BITCAST && def->type == IRT_I64 && def->nops == 1 &&
        def->ops[0].type == IRT_PTR) {
        *origin = def->ops[0];
        return true;
    }
    if (def->nops != 2)
        return false;
    switch (def->op) {
    case IR_IADD:
    case IR_AND:
    case IR_OR:
        if (def->ops[1].kind == IROP_ICONST)
            next = def->ops[0];
        else if (def->ops[0].kind == IROP_ICONST)
            next = def->ops[1];
        else
            return false;
        break;
    case IR_ISUB:
        if (def->ops[1].kind != IROP_ICONST)
            return false;
        next = def->ops[0];
        break;
    default:
        return false;
    }
    return round_trip_anchor(function, next, origin, depth + 1);
}

static void splice_runtime_round_trip(MsFunctionResult *result, IrInst *derived,
                                      IrOperand origin, u32 callee, u32 site_id)
{
    IrModule *module = result->module;
    IrOperand *args;
    IrInst *check;

    check = arena_alloc(module->arena, sizeof(*check), _Alignof(IrInst));
    args = arena_alloc(module->arena, 3 * sizeof(*args), _Alignof(IrOperand));
    memset(check, 0, sizeof(*check));
    args[0] = origin;
    args[1] = ir_op_value(result->function, derived->result);
    args[2] = ir_op_iconst(IRT_I32, site_id);
    check->op = IR_CALL;
    check->type = IRT_VOID;
    check->subop = FUNCREF_EXTERNAL;
    check->callee = callee;
    check->loc = derived->loc;
    check->ops = args;
    check->nops = 3;
    check->next = derived->next;
    derived->next = check;
}

static void splice_allocation_site(MsFunctionResult *result,
                                   const MsRuntimeAlloc *alloc, u32 callee)
{
    IrModule *module = result->module;
    IrFunc *function = result->function;
    IrInst *setter =
        arena_alloc(module->arena, sizeof(*setter), _Alignof(IrInst));
    IrOperand *args =
        arena_alloc(module->arena, sizeof(*args), _Alignof(IrOperand));
    u32 bi;

    memset(setter, 0, sizeof(*setter));
    args[0] = ir_op_iconst(IRT_I32, alloc->site_id);
    setter->op = IR_CALL;
    setter->type = IRT_VOID;
    setter->subop = FUNCREF_EXTERNAL;
    setter->callee = callee;
    setter->loc = alloc->call->loc;
    setter->ops = args;
    setter->nops = 1;

    for (bi = 0; bi < function->nblocks; bi++) {
        IrBlock *block = &function->blocks[bi];
        IrInst *in = block->first;
        IrInst *previous = NULL;

        while (in && in != alloc->call) {
            previous = in;
            in = in->next;
        }
        if (!in)
            continue;
        setter->next = in;
        if (previous)
            previous->next = setter;
        else
            block->first = setter;
        block->ninsts++;
        return;
    }
    CGF_ICE("memsafe instrumentation lost allocation site in @%s",
            function->name);
}

static void instrument_result(MsFunctionResult *result, MsCheckStats *stats)
{
    u32 callee = UINT32_MAX;
    u32 derive_callee = UINT32_MAX;
    u32 round_trip_callee = UINT32_MAX;
    u32 setter = UINT32_MAX;
    u32 i, bi;
    u32 derive_site = result->naccesses + 1;
    bool changed = false;

    for (i = 0; i < result->naccesses; i++) {
        const MsRuntimeAccess *access = &result->accesses[i];

        stats->total++;
        if (access->discharged) {
            stats->discharged++;
            continue;
        }
        if (callee == UINT32_MAX)
            callee = ir_sym(result->module, "cgf_safe_check");
        splice_runtime_check(result, access, callee);
        stats->emitted++;
        changed = true;
    }
    for (i = 0; i < result->nallocs; i++) {
        if (setter == UINT32_MAX)
            setter = ir_sym(result->module, "cgf_safe_set_next_site");
        splice_allocation_site(result, &result->allocs[i], setter);
        changed = true;
    }
    /* MS-C-05: check a derived pointer while its origin is still
       registry-locatable.  Waiting until a later access lets a far result
       look like an unrelated foreign pointer and escape the generic check. */
    for (bi = 0; bi < result->function->nblocks; bi++) {
        IrInst *in;

        for (in = result->function->blocks[bi].first; in; in = in->next) {
            IrOperand origin;

            if (is_runtime_index_check(result->module, in)) {
                in->ops[5] = ir_op_iconst(IRT_I32, derive_site++);
                changed = true;
            } else if (in->op == IR_PTRADD && in->result.v != 0) {
                /* The raw-index guard is stronger than a check of the
                   already-wrapped byte offset and must diagnose first. */
                if (is_runtime_index_check(result->module, in->next))
                    continue;
                if (derive_callee == UINT32_MAX)
                    derive_callee =
                        ir_sym(result->module, "cgf_safe_check_derive");
                splice_runtime_derive(result, in, derive_callee, derive_site++);
                result->function->blocks[bi].ninsts++;
                changed = true;
            } else if (in->op == IR_BITCAST && in->type == IRT_PTR &&
                       in->nops == 1 && in->ops[0].type == IRT_I64 &&
                       round_trip_anchor(result->function, in->ops[0], &origin,
                                         0)) {
                /* MS-C-05: the documented uintptr_t grammar lowers through
                   modular integer operations, so recognize its constant-op
                   chain and check the cast-back against the original pointer.
                 */
                if (round_trip_callee == UINT32_MAX)
                    round_trip_callee =
                        ir_sym(result->module, "cgf_safe_check_round_trip");
                splice_runtime_round_trip(result, in, origin, round_trip_callee,
                                          derive_site++);
                result->function->blocks[bi].ninsts++;
                changed = true;
            }
        }
    }
    if (changed)
        ir_func_renumber(result->module->arena, result->function);
}

static const char *event_name(MsEventKind kind)
{
    static const char *const names[] = {
        "alloc", "free", "realloc", "escape", "use", "branch", "call", "return",
    };

    return (u32)kind < CGF_ARRAY_LEN(names) ? names[kind] : "unknown";
}

static void process_module(WarnCtx *warnings, IrModule *module,
                           bool no_strict_aliasing, FILE *dump, bool instrument,
                           MsCheckStats *stats, const struct AstNode *tu,
                           const struct Preprocessor *pp)
{
    Arena analysis;
    MsCheckStats counts = {0};
    OptConfig cfg;
    u32 fi;

    if (stats)
        memset(stats, 0, sizeof(*stats));
    if (!module || (!warnings && !dump && !instrument))
        return;
    /* L1 is an SSA/path analysis even at -O0. Promotion is analysis
     * normalization; safe mode intentionally applies it to the emitted
     * module before taking the terminal instrumentation residue. */
    if (warnings || instrument) {
        opt_config_init(&cfg, OPT_O0);
        cfg.no_strict_aliasing = no_strict_aliasing;
        (void)opt_mem2reg(module, &cfg);
    }
    arena_init(&analysis);
    {
        MsSummarySet *summaries =
            ms_summary_build(&analysis, module, no_strict_aliasing, warnings);
        MsFunctionResult **results = arena_alloc(
            &analysis, (module->nfuncs ? module->nfuncs : 1) * sizeof(*results),
            _Alignof(MsFunctionResult *));

        if (warnings && tu)
            ms_summary_suggest_annotations(warnings, summaries, tu, pp);
        if (dump)
            ms_summary_dump(summaries, dump);
        for (fi = 0; fi < module->nfuncs; fi++) {
            MsFunctionResult *result = ms_analyze_function_with_summaries(
                &analysis, module, &module->funcs[fi], no_strict_aliasing,
                summaries);

            results[fi] = result;
            if (warnings) {
                u32 i;

                for (i = 0; i < result->nissues; i++)
                    emit_issue(warnings, &result->issues[i], tu, pp);
            }
            if (dump)
                dump_result(result, dump);
            /* Mutation is legal only after this function's AliasCtx has been
             * destroyed. The retained access plan is arena-owned and uses no
             * alias query during splicing. */
            ms_result_free(result);
        }
        /* No IR changes occur until every function has been analyzed and
         * every AliasCtx has been destroyed. Symbol-table growth and list
         * splicing therefore cannot perturb a later function's proof. */
        if (instrument)
            for (fi = 0; fi < module->nfuncs; fi++)
                instrument_result(results[fi], &counts);
    }
    if (dump && instrument)
        fprintf(dump, "checks: %u total, %u discharged, %u emitted\n",
                counts.total, counts.discharged, counts.emitted);
    if (stats)
        *stats = counts;
    arena_free_all(&analysis);
}

void ms_process_module(WarnCtx *warnings, IrModule *module,
                       bool no_strict_aliasing, FILE *dump, bool instrument,
                       MsCheckStats *stats)
{
    process_module(warnings, module, no_strict_aliasing, dump, instrument,
                   stats, NULL, NULL);
}

void ms_process_module_with_tu(WarnCtx *warnings, IrModule *module,
                               bool no_strict_aliasing, FILE *dump,
                               bool instrument, MsCheckStats *stats,
                               const struct AstNode *tu,
                               const struct Preprocessor *pp)
{
    process_module(warnings, module, no_strict_aliasing, dump, instrument,
                   stats, tu, pp);
}

void ms_warn_module(WarnCtx *warnings, IrModule *module,
                    bool no_strict_aliasing)
{
    ms_process_module(warnings, module, no_strict_aliasing, NULL, false, NULL);
}

void ms_dump_module(IrModule *module, bool no_strict_aliasing, FILE *out)
{
    ms_process_module(NULL, module, no_strict_aliasing, out, false, NULL);
}
