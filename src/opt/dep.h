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

#endif
