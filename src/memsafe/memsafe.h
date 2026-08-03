#ifndef CGF_MEMSAFE_MEMSAFE_H
#define CGF_MEMSAFE_MEMSAFE_H

#include <stdbool.h>
#include <stdio.h>

#include "diag.h"
#include "opt/alias.h"
#include "util/arena.h"
#include "util/base.h"

/* Abstract state of one allocation site on one analyzed path.  The scalar
 * lattice deliberately loses every disagreement to UNKNOWN.  The bounded
 * lifetime worklist preserves ALLOCATED/FREED alternatives by keeping paths
 * split while its budget permits; that policy does not belong in join. */
typedef enum MsState {
    MS_UNALLOCATED,
    MS_ALLOCATED,
    MS_FREED,
    MS_ESCAPED,
    MS_UNKNOWN,
    MS_STATE_COUNT
} MsState;

MsState ms_state_join(MsState a, MsState b);
const char *ms_state_name(MsState state);

typedef enum MsAction {
    MS_ACT_ALLOC,
    MS_ACT_FREE,
    MS_ACT_ESCAPE,
    MS_ACT_DEREF
} MsAction;

typedef enum MsOutcome {
    MS_OUTCOME_OK,
    MS_OUTCOME_NOOP,
    MS_OUTCOME_DOUBLE_FREE,
    MS_OUTCOME_UAF_ESCAPE,
    MS_OUTCOME_UAF
} MsOutcome;

typedef struct MsTransition {
    MsState state;
    MsOutcome outcome;
} MsTransition;

/* is_null is meaningful for FREE only.  A null free is an explicit no-op,
 * independent of the abstract state associated with the expression. */
MsTransition ms_transition(MsState from, MsAction action, bool is_null);

typedef enum MsEventKind {
    MS_EV_ALLOC,
    MS_EV_FREE,
    MS_EV_REALLOC,
    MS_EV_ESCAPE,
    MS_EV_USE,
    MS_EV_BRANCH,
    MS_EV_CALL,
    MS_EV_RETURN
} MsEventKind;

typedef struct MsEvent {
    Span loc;
    MsEventKind kind;
    const char *note; /* arena-owned, pre-rendered, interned within the trace */
} MsEvent;

typedef struct MsTraceNode MsTraceNode;

/* A persistent event chain makes a path split an ordinary shallow copy.
 * Push never mutates an existing node, so sibling paths cannot corrupt one
 * another's proof chains. */
typedef struct MsTrace {
    Arena *arena;
    const MsTraceNode *tail;
    u32 len;
} MsTrace;

void ms_trace_init(MsTrace *trace, Arena *arena);
void ms_trace_push(MsTrace *trace, Span loc, MsEventKind kind, const char *fmt,
                   ...);
/* Program-order access: ordinal zero is the oldest event. */
bool ms_trace_event(const MsTrace *trace, u32 ordinal, MsEvent *out);

#define MS_NO_ARG UINT32_MAX

typedef enum MsAllocSuccess {
    MS_ALLOC_SUCCESS_DIRECT,
    MS_ALLOC_SUCCESS_STATUS_ZERO,
    MS_ALLOC_SUCCESS_STATUS_NONNEG,
} MsAllocSuccess;

/* Libc ownership semantics are data, not name-special cases in the analysis.
 * alloc_out_arg == MS_NO_ARG means a direct result when allocates is true.
 * frees_on_success distinguishes realloc-family consumption from free(). */
typedef struct MsAllocFamily {
    const char *name;
    bool allocates;
    u32 alloc_out_arg;
    u32 frees_arg;
    bool frees_on_success;
    bool returns_ownership;
    bool zeroes;
    bool fully_written;
    /* Direct-return allocators are born allocated and are later refined by
     * null tests.  Fallible out-parameter allocators remain pending until
     * their integer status is proven successful or failed. */
    MsAllocSuccess success;
    /* Constant extent, when recoverable, is size_arg multiplied by
     * size_arg2.  MS_NO_ARG means that factor is absent/unknown. */
    u32 size_arg;
    u32 size_arg2;
    bool is_file_resource;
} MsAllocFamily;

const MsAllocFamily *ms_alloc_family_lookup(const char *name);
bool ms_alloc_seed_for_call(const IrModule *module, const IrInst *call,
                            AliasAllocSeed *out);
u32 ms_alias_alloc_seeds(Arena *arena, const IrModule *module,
                         const IrFunc *function, AliasAllocSeed **out);

/* Precision budgets are part of the public analysis contract: exceeding one
 * silently joins toward may-information, suppressing claims that required
 * the discarded path distinction. */
#define MS_MAX_STATES_PER_BLOCK 8u
#define MS_MAX_SPLITS_PER_FUNCTION 256u
#define MS_MAX_PREDICATES_PER_PATH 4u

typedef struct MsFunctionResult MsFunctionResult;

typedef struct MsParamSummary {
    bool dereferenced;
    bool written;
    bool escapes;
    bool may_free;
    bool must_free;
    bool returned_alias;
    bool write_range_known;
    bool write_range_unknown;
    i64 write_lo;
    i64 write_hi; /* half-open */
    /* First deterministic body witnesses used by annotation-mismatch notes.
     * A zero span means the effect came only from a declaration contract. */
    Span free_span;
    Span escape_span;
    Span return_alias_span;
    /* Annotation strength is per dimension: one contract never suppresses
     * unrelated facts inferred from the body. */
    bool annot_borrow;
    bool annot_no_escape;
    bool annot_takes_ownership;
    bool annot_returns_borrowed;
} MsParamSummary;

typedef struct MsSummary {
    const char *name;
    u32 nparams;
    MsParamSummary *params;
    /* Union fact consumed by the warning analysis: at least one path returns
     * fresh ownership. The separate must fact is the intersection required
     * before suggesting a public ownership contract. */
    bool returns_ownership;
    bool must_return_ownership;
    Span returns_ownership_span;
    bool annot_returns_owned;
    bool top;
    bool partial; /* external annotation: unspecified dimensions stay TOP */
} MsSummary;

/* Compact built-in contracts cover external libc calls without inventing
 * external IR functions.  Masks use zero-based argument ordinals. */
typedef struct MsLibSummary {
    const char *name;
    u64 deref_mask;
    u64 write_mask;
    u64 escape_mask;
    u64 free_mask;
    i32 return_alias; /* -1 when the result does not alias an argument */
    bool returns_ownership;
    /* Argument ordinals supplying a constant write extent.  The second
     * factor is -1 when the first argument is the complete byte count. */
    i32 write_size_arg;
    i32 write_size_arg2;
} MsLibSummary;

typedef struct MsSummarySet MsSummarySet;
struct WarnCtx;
struct AstNode;
struct Preprocessor;
MsSummarySet *ms_summary_build(Arena *arena, IrModule *module,
                               bool no_strict_aliasing,
                               struct WarnCtx *warnings);
const MsSummary *ms_summary_get(const MsSummarySet *set, u32 function);
const MsSummary *ms_summary_for_call(const MsSummarySet *set,
                                     const IrInst *call);
const MsLibSummary *ms_lib_summary_lookup(const char *name);
/* UINT32_MAX for non-variadic rows; otherwise the first variadic argument. */
u32 ms_lib_summary_variadic_from(const MsLibSummary *summary);
void ms_summary_dump(const MsSummarySet *set, FILE *out);
/* Emits source-copy annotation suggestions on declarations in this
 * translation unit. Only must-summary facts produce edits. */
void ms_summary_suggest_annotations(struct WarnCtx *warnings,
                                    const MsSummarySet *set,
                                    const struct AstNode *tu,
                                    const struct Preprocessor *pp);

typedef enum MsIssueKind {
    MS_ISSUE_USE_AFTER_FREE,
    MS_ISSUE_DOUBLE_FREE,
    MS_ISSUE_LEAK,
    MS_ISSUE_OUT_OF_BOUNDS,
    MS_ISSUE_UNINIT_READ,
    MS_ISSUE_FREE_NONHEAP,
    MS_ISSUE_REALLOC_ZERO,
    MS_ISSUE_COUNT
} MsIssueKind;

typedef struct MsIssue {
    MsIssueKind kind;
    Span loc;
    u32 site_id; /* zero for issues that do not name a heap allocation */
    bool strict;
    bool file_resource;
    MsTrace trace;
} MsIssue;

/* Sprint 44: every surviving IR memory access is classified by the same
 * path/alias analysis that emits -Wmem diagnostics.  A proven access needs
 * no runtime call; every other access remains in the instrumentation
 * residue. */
typedef struct MsCheckStats {
    u32 total;
    u32 discharged;
    u32 emitted;
} MsCheckStats;

MsFunctionResult *ms_analyze_function(Arena *arena, IrModule *module,
                                      IrFunc *function,
                                      bool no_strict_aliasing);
void ms_result_free(MsFunctionResult *result);
bool ms_result_degraded(const MsFunctionResult *result);
u32 ms_result_split_count(const MsFunctionResult *result);
u32 ms_result_block_state_count(const MsFunctionResult *result,
                                u32 block_index);
u32 ms_result_exit_count(const MsFunctionResult *result);
MsState ms_result_exit_state(const MsFunctionResult *result, u32 exit_index,
                             u32 site_index);
const MsTrace *ms_result_exit_trace(const MsFunctionResult *result,
                                    u32 exit_index, u32 site_index);
u32 ms_result_issue_count(const MsFunctionResult *result);
const MsIssue *ms_result_issue_at(const MsFunctionResult *result, u32 index);
void ms_warn_module(struct WarnCtx *warnings, IrModule *module,
                    bool no_strict_aliasing);
void ms_dump_module(IrModule *module, bool no_strict_aliasing, FILE *out);
void ms_process_module(struct WarnCtx *warnings, IrModule *module,
                       bool no_strict_aliasing, FILE *dump, bool instrument,
                       MsCheckStats *stats);
/* Driver-only extension: keeps the stable analysis API above while making
 * typed prototype locations available to Sprint 45's annotation ratchet. */
void ms_process_module_with_tu(struct WarnCtx *warnings, IrModule *module,
                               bool no_strict_aliasing, FILE *dump,
                               bool instrument, MsCheckStats *stats,
                               const struct AstNode *tu,
                               const struct Preprocessor *pp);

#endif
