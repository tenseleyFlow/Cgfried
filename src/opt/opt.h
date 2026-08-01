#ifndef CGF_OPT_OPT_H
#define CGF_OPT_OPT_H

#include <stdbool.h>
#include <stdio.h>

#include "ir/ir.h"
#include "util/base.h"

/* Optimization levels are shared by the driver and the pass pipeline.
 * Keep this a closed enum: pipeline.c must account for every level. */
typedef enum { OPT_O0, OPT_O1, OPT_O2, OPT_O3, OPT_OS, OPT_OFAST } OptLevel;

typedef struct {
    bool reassoc;
    bool no_nans;
    bool no_infs;
    bool no_signed_zeros;
    bool reciprocal_math;
} OptFastMath;

typedef struct OptConfig {
    OptLevel level;
    u32 inline_threshold;
    u32 unroll_threshold;
    OptFastMath fast_math;
    bool no_strict_aliasing;
    bool fwrapv;
    bool debug_info;

    /* Runtime controls are resolved once by the driver. Tests may set
     * these directly without mutating the process environment. */
    bool verify_after_each;
    bool bail_log;
    bool time_report;
    const char *dump_bad_ir;
    FILE *report;

    /* Per-function context for the three-argument OPT_BAIL convention.
     * A function pass copies the config and fills this field. */
    const char *current_func;
} OptConfig;

typedef enum PassPinnedPolicy {
    PASS_PINNED_EXACT,
    PASS_PINNED_INLINE_CLONES,
    PASS_PINNED_DELETE_FUNCS,
} PassPinnedPolicy;

typedef struct Pass {
    const char *name;
    bool (*run)(IrModule *m, const OptConfig *cfg);
    PassPinnedPolicy pinned_policy;
} Pass;

void opt_config_init(OptConfig *cfg, OptLevel level);
void opt_bail(const OptConfig *cfg, const char *pass, const char *reason);
/* Adjacent-string concatenation makes a non-literal reason a compile error.
 * Stable literal reasons are part of the optimizer's greppable contract. */
#define OPT_BAIL(cfg, pass, reason) opt_bail((cfg), (pass), "" reason)

/* Injectable manager seams: production pipelines and adversarial unit tests
 * use the same changed-flag, verifier, volatile, timing and fixpoint code. */
bool opt_run_pass_sequence(IrModule *m, const OptConfig *cfg,
                           const Pass *const *passes, u32 npasses);
bool opt_run_fixpoint(IrModule *m, const OptConfig *cfg,
                      const Pass *const *passes, u32 npasses, u32 cap);
bool opt_run_pipeline(IrModule *m, const OptConfig *cfg);

/* The first pass. Later passes add one exported descriptor and one pipeline
 * array entry; no placeholder descriptors are allowed. */
extern const Pass OPT_PASS_MEM2REG;
bool opt_mem2reg(IrModule *m, const OptConfig *cfg);

/* Sprint 31 local workhorses. Folding is shared by SCCP and simplify so
 * target arithmetic and undef handling cannot drift between passes. */
extern const Pass OPT_PASS_SCCP;
extern const Pass OPT_PASS_SIMPLIFY;
extern const Pass OPT_PASS_CSE;
bool opt_fold_inst(const IrInst *in, IrOperand *out, const OptConfig *cfg);
bool opt_sccp(IrModule *m, const OptConfig *cfg);
bool opt_simplify(IrModule *m, const OptConfig *cfg);
bool opt_cse(IrModule *m, const OptConfig *cfg);

/* Sprint 32 memory/global workhorses.  DCE and CFG cleanup join O1; GVN,
 * DSE, and jump threading are O2 rows. */
extern const Pass OPT_PASS_GVN;
extern const Pass OPT_PASS_DCE;
extern const Pass OPT_PASS_DSE;
extern const Pass OPT_PASS_SIMPLIFY_CFG;
extern const Pass OPT_PASS_JUMP_THREAD;
bool opt_gvn(IrModule *m, const OptConfig *cfg);
bool opt_dce(IrModule *m, const OptConfig *cfg);
bool opt_dse(IrModule *m, const OptConfig *cfg);
bool opt_simplify_cfg(IrModule *m, const OptConfig *cfg);
bool opt_jump_thread(IrModule *m, const OptConfig *cfg);

/* Sprint 33 interprocedural work.  The callgraph owns heap-backed analysis
 * storage and indexes nodes by IrModule function index.  SCC enumeration is
 * deterministic; ipo_callgraph_bottom_up_scc() maps bottom-up ordinals to
 * Tarjan component ids so clients process callees before callers. */
typedef struct Callgraph Callgraph;
Callgraph *ipo_callgraph_build(IrModule *m);
void ipo_callgraph_free(Callgraph *cg);
u32 ipo_callgraph_node_count(const Callgraph *cg);
u32 ipo_callgraph_edge_count(const Callgraph *cg, u32 caller);
u32 ipo_callgraph_edge(const Callgraph *cg, u32 caller, u32 ordinal);
bool ipo_callgraph_has_unknown_callees(const Callgraph *cg, u32 caller);
bool ipo_callgraph_address_taken(const Callgraph *cg, u32 func);
u32 ipo_callgraph_scc_count(const Callgraph *cg);
u32 ipo_callgraph_scc_of(const Callgraph *cg, u32 func);
u32 ipo_callgraph_scc_size(const Callgraph *cg, u32 scc);
u32 ipo_callgraph_scc_member(const Callgraph *cg, u32 scc, u32 ordinal);
u32 ipo_callgraph_bottom_up_scc(const Callgraph *cg, u32 ordinal);

extern const Pass OPT_PASS_IPO;
extern const Pass OPT_PASS_INLINE;
bool opt_ipo(IrModule *m, const OptConfig *cfg);
bool opt_inline(IrModule *m, const OptConfig *cfg);

/* Sprint 34 loop infrastructure.  Analysis is scratch-arena-owned and pure;
 * canonicalization is a separate mutation because adding block parameters,
 * edges, preheaders, and exits requires the module arena.  Rebuild dominance
 * and the loop tree after loop_canonicalize() reports a change. */
typedef struct Loop Loop;
typedef struct LoopTree LoopTree;

LoopTree *loop_tree_build(Arena *arena, const IrFunc *f, const IrDomTree *dt);
u32 loop_tree_count(const LoopTree *lt);
const Loop *loop_tree_at(const LoopTree *lt, u32 ordinal);
bool loop_tree_irreducible(const LoopTree *lt);
const Loop *loop_tree_innermost(const LoopTree *lt, BlockId block);
BlockId loop_header(const Loop *loop);
BlockId loop_preheader(const Loop *loop);
const Loop *loop_parent(const Loop *loop);
u32 loop_depth(const Loop *loop);
u32 loop_block_count(const Loop *loop);
BlockId loop_block(const Loop *loop, u32 ordinal);
bool loop_contains(const Loop *loop, BlockId block);
u32 loop_latch_count(const Loop *loop);
BlockId loop_latch(const Loop *loop, u32 ordinal);
u32 loop_exit_count(const Loop *loop);
BlockId loop_exit_source(const Loop *loop, u32 ordinal);
BlockId loop_exit_target(const Loop *loop, u32 ordinal);

bool loop_canonicalize(IrModule *m, IrFunc *f, const LoopTree *lt);
bool loop_tree_verify_canonical(const LoopTree *lt, const IrFunc *f, char *why,
                                size_t why_cap);

/* Sprint 34 loop transforms run only after the scalar/IPO fixpoint, so CFG
 * cleanup cannot tear down their dedicated preheaders and exits. */
extern const Pass OPT_PASS_LICM;
extern const Pass OPT_PASS_STRENGTH;
extern const Pass OPT_PASS_UNROLL;
bool opt_licm(IrModule *m, const OptConfig *cfg);
bool opt_strength(IrModule *m, const OptConfig *cfg);
bool opt_unroll(IrModule *m, const OptConfig *cfg);
bool opt_unroll_trip_count(IrType type, IrIcmp pred, u64 start, u64 step,
                           u64 end, bool modular, u64 *trip);

/* An IR location is represented by its resolved Span rather than the PP
 * table-local SrcLoc id. This keeps the record queryable after PP teardown. */
typedef struct UndefUse {
    u32 alloca_ord;
    BlockId block;
    Span loc;
    const char *name;
    Span decl_loc;
} UndefUse;

const UndefUse *opt_mem2reg_undef_log(const IrFunc *f, u32 *n);

/* Unreachable-code provenance retained before simplify_cfg deletes blocks.
 * Sprint 40 consumes these resolved Spans after PP state has gone away. */
typedef struct CfgRemoved {
    BlockId block;
    Span loc;
} CfgRemoved;

const CfgRemoved *opt_cfg_removed_log(const IrFunc *f, u32 *n);

#endif
