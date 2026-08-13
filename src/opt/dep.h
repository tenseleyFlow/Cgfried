#ifndef CGF_OPT_DEP_H
#define CGF_OPT_DEP_H

#include <stdint.h>

#include "opt/alias.h"

/* A proven dependence is not the same thing as distance zero.  Keep the
 * three outcomes explicit so clients cannot accidentally turn an unknown
 * answer into permission to reorder memory operations. */
typedef enum DepKind {
    DEP_UNKNOWN,
    DEP_INDEPENDENT,
    DEP_DISTANCE,
} DepKind;

typedef struct DepResult {
    DepKind kind;
    /* iteration_b - iteration_a.  For equal-stride accesses
     *   base + k*i + c_a, base + k*j + c_b
     * equality gives distance = (c_a - c_b) / k. */
    int64_t distance;
    const char *reason;
} DepResult;

typedef struct DepAccess {
    IrOperand base;
    int64_t stride;
    int64_t offset;
    uint64_t size;
    EffTypeId etype;
} DepAccess;

/* Reusable immutable-function context for affine range queries. Building the
 * dominator and loop trees is the expensive part; clients that ask more than
 * one question about the same function must share this context rather than
 * rebuilding both analyses per pointer expression. Any CFG mutation
 * invalidates the context. */
typedef struct DepRangeCtx DepRangeCtx;

DepRangeCtx *dep_range_ctx_new(const IrFunc *f);
void dep_range_ctx_free(DepRangeCtx *ctx);
bool dep_affine_range_ctx(const DepRangeCtx *ctx, IrOperand operand,
                          int64_t *lo, int64_t *hi);

/* Exact for equal, non-zero affine strides.  Distinct bases prove
 * independence only when their non-unknown points-to object sets are
 * disjoint; byte-offset/type alias facts alone cannot prove that two pointer
 * values name different objects.  Every unsupported or may-alias shape
 * remains DEP_UNKNOWN. */
DepResult dep_query(AliasCtx *alias, DepAccess a, DepAccess b);

/* Recognize ptradd(base, k*iv+c), including nested integer add/sub and a
 * constant multiply.  The base is never guessed through loads or selects. */
bool dep_access_from_ptr(const IrFunc *f, IrOperand ptr, ValueId iv,
                         uint64_t size, EffTypeId etype, DepAccess *out,
                         const char **reason);

/* Exact signed range for an affine integer expression over a constant-trip
 * induction variable.  This is the shared optimizer/diagnostic seam for
 * loop byte offsets; unsupported, wrapping, or runtime-trip shapes return
 * false rather than a speculative interval. */
bool dep_affine_range(const IrFunc *f, IrOperand operand, int64_t *lo,
                      int64_t *hi);
bool dep_affine_ptr_range(const IrFunc *f, IrOperand pointer, int64_t *lo,
                          int64_t *hi);

/* Diagnostic clients additionally need proof that the memory access itself,
 * not merely the affine pointer definition, executes at every endpoint used
 * by the range.  The `_at` forms therefore require one loop exit and require
 * `access_block` to be in the recognized loop and to dominate its latch. */
bool dep_affine_range_at(const IrFunc *f, IrOperand operand,
                         BlockId access_block, int64_t *lo, int64_t *hi);
bool dep_affine_ptr_range_at(const IrFunc *f, IrOperand pointer,
                             BlockId access_block, int64_t *lo, int64_t *hi);

#endif
