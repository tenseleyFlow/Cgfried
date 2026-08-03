#ifndef CGF_OPT_ALIAS_H
#define CGF_OPT_ALIAS_H

#include <stdbool.h>
#include <stdint.h>

#include "ir/ir.h"

/* Shared alias-analysis service: one points-to engine, two clients.
 *
 * Sprint 32's optimizer and Sprint 41's memory-safety analysis consume this
 * exact service.  New memsafe facts are upstreamed here; duplicating
 * points-to or byte-offset logic under src/memsafe/ is an architecture-review
 * rejection criterion.
 * Phase 9 sign-off (Sprint 32): this contract was reviewed against
 * .docs/sprints/09-memory-safety/s41-analysis-foundation.md.  The result is
 * one points-to engine with two clients; a duplicate memsafe analysis is
 * explicitly rejected.
 *
 * Soundness contract:
 * - ALIAS_NO is a proof.  ALIAS_MAY is the only honest fallback.
 * - ALIAS_MUST requires one identical abstract object and an identical,
 *   fully-known byte range.  Partial overlap is never MUST.
 * - ETYPE_CHAR and ETYPE_UNION are wildcards and never justify a type-based
 *   ALIAS_NO.  Proven distinct objects and disjoint byte ranges remain NO.
 *   Equal-width signed and unsigned integer accesses share one EffTypeId.
 * - Disabling strict aliasing removes only effective-type NoAlias results;
 *   distinct-object and disjoint-byte-range proofs remain valid.
 * - Queries are pure.  Any CFG or instruction mutation invalidates AliasCtx;
 *   a mutating pass must free and rebuild it.
 *
 * The classic strict-aliasing case is intentionally mode-dependent:
 *
 *   int f(float *fp, int *ip) { *fp = 1; *ip = 2; return (int)*fp; }
 *
 * With strict aliasing, float and i32 accesses prove ALIAS_NO.  With
 * -fno-strict-aliasing they are ALIAS_MAY.  Passing the same object to both
 * parameters in strict mode is user undefined behavior, not compiler license
 * to weaken the no-strict mode.
 */

typedef enum AliasResult { ALIAS_NO, ALIAS_MAY, ALIAS_MUST } AliasResult;

typedef struct MemLoc {
    IrOperand base;
    int64_t off_lo;
    int64_t off_hi;
    uint64_t size;
    EffTypeId etype;
} MemLoc;

typedef struct AllocSite AllocSite;

#define ALIAS_NO_OUT_PARAM UINT32_MAX

/* Allocation-family recognition belongs to the client.  The alias service
 * knows only that a preclassified call creates one fresh abstract object and
 * where the owning pointer is published.  out_param is an argument ordinal
 * (the indirect callee operand, when present, is not an argument). */
typedef struct AliasAllocSeed {
    const IrInst *call;
    bool owns_result;
    uint32_t out_param;
} AliasAllocSeed;

/* A summarized call may return an alias of one or more pointer arguments.
 * Distinct (call, arg) entries form a set.  The client classifies the call;
 * the shared engine unions every matching argument into the result so
 * interprocedural clients do not grow a second alias analysis. */
typedef struct AliasReturnSeed {
    const IrInst *call;
    uint32_t arg;
} AliasReturnSeed;

typedef struct AliasConfig {
    /* ValueIds are function-local, so each context deliberately analyzes one
     * function even though symbols live at module scope. */
    IrFunc *func;
    bool no_strict_aliasing;
    const AliasAllocSeed *alloc_seeds;
    uint32_t nalloc_seeds;
    const AliasReturnSeed *return_seeds;
    uint32_t nreturn_seeds;
    bool track_param_origins;
} AliasConfig;

typedef struct AliasCtx AliasCtx;

/* Read-only bitset view.  Object ids are service-private and stable only for
 * the lifetime of c.  Sprint 41 consumes set membership, never id meaning. */
typedef struct PtsSet {
    const uint64_t *words;
    uint32_t nwords;
    bool has_unknown;
} PtsSet;

AliasCtx *alias_build(IrModule *m, const AliasConfig *cfg);
void alias_free(AliasCtx *c);
AliasResult alias_query(AliasCtx *c, MemLoc a, MemLoc b);
bool alias_escapes(AliasCtx *c, IrOperand base);

PtsSet alias_points_to(AliasCtx *c, IrOperand ptr);
bool alias_offset_range(AliasCtx *c, IrOperand ptr, int64_t *lo, int64_t *hi);
/* True only when every possible pointee is a symbol or stack allocation.
 * Restrict-parameter abstract objects remain unknown here: a caller may pass
 * heap storage.  The query is deliberately phrased as a proof so clients can
 * diagnose invalid deallocation without learning private object ids. */
bool alias_pts_must_be_nonheap(const AliasCtx *c, PtsSet pts);
/* Parameter-origin markers are optional analysis-only objects.  They are
 * added alongside (never instead of) UNKNOWN, preserving ordinary alias
 * answers while allowing a summary client to ask where a value originated. */
bool alias_pts_has_param(const AliasCtx *c, PtsSet pts, uint32_t param);

/* Allocation-site identity and iteration are stable only for the lifetime of
 * c.  Iteration follows the seed order supplied to alias_build. */
const AllocSite *alias_alloc_site(const AliasCtx *c, const IrInst *call);
uint32_t alias_alloc_site_count(const AliasCtx *c);
const AllocSite *alias_alloc_site_at(const AliasCtx *c, uint32_t index);
uint32_t alias_alloc_site_id(const AllocSite *site);
const IrInst *alias_alloc_site_call(const AllocSite *site);
bool alias_pts_has_alloc_site(const AliasCtx *c, PtsSet pts,
                              const AllocSite *site);
const AllocSite *alias_pts_unique_alloc_site(const AliasCtx *c, PtsSet pts);

/* Enumerate allocation sites named by root or transitively by explicit
 * pointer stores/out-parameter publications.  The return value is the full
 * count and may exceed capacity; output is truncated to capacity in stable
 * site-id order.  UNKNOWN reachability is reported separately and is never
 * expanded into invented site membership. */
uint32_t alias_reachable_alloc_sites(const AliasCtx *c, IrOperand root,
                                     const AllocSite **out, uint32_t capacity,
                                     bool *has_unknown);

/* Helpers used by optimizer clients.  alias_memloc starts at the pointer's
 * own tracked byte offset; explicit subranges may be added in MemLoc before a
 * query.  alias_covers is directional: outer must overwrite every byte inner
 * may access, and both must name one identical abstract object. */
MemLoc alias_memloc(AliasCtx *c, IrOperand ptr, uint64_t size, EffTypeId etype);
bool alias_covers(AliasCtx *c, MemLoc outer, MemLoc inner);

#endif
