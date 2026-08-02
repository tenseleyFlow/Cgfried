#ifndef CGF_WARN_FLOW_H
#define CGF_WARN_FLOW_H

#include <stdbool.h>

#include "ir/ir.h"
#include "util/arena.h"

/* Shared flow-analysis boundary.  Sprint 40 owns CFG reachability,
 * dominance, and decisive-edge reporting here; Sprint 42 may consume these
 * services for memory diagnostics, but memory-safety policy must not leak
 * back into this module.  Until then this header is internal to src/warn/. */
typedef struct FlowCtx FlowCtx;

FlowCtx *flow_ctx_new(Arena *arena, IrModule *module, IrFunc *function);
bool flow_reachable(const FlowCtx *fc, BlockId block);
bool flow_dominates(const FlowCtx *fc, BlockId dominator, BlockId block);
const char *flow_path_note(FlowCtx *fc, BlockId from, BlockId to);

#endif
