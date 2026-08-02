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
void ms_dump_module(IrModule *module, bool no_strict_aliasing, FILE *out);

#endif
