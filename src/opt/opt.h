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
    bool disable_unswitch;
    bool disable_bce;
    bool disable_fusion;
    bool disable_vectorize;

    /* Runtime controls are resolved once by the driver. Tests may set
     * these directly without mutating the process environment. */
    bool verify_after_each;
    bool bail_log;
    bool time_report;
    const char *dump_bad_ir;
    /* CGF_DUMP_IR=all plumbing.  The driver owns the sequence counter so
     * separate pass-manager groups still produce one total pipeline order. */
    const char *dump_ir_dir;
    u32 *dump_ir_sequence;
    u32 *dump_ir_fixpoint;
    FILE *report;

    /* Per-function context for the three-argument OPT_BAIL convention.
     * A function pass copies the config and fills this field. */
    const char *current_func;
} OptConfig;

typedef enum PassPinnedPolicy {
    PASS_PINNED_EXACT,
    PASS_PINNED_INLINE_CLONES,
    /* The pass keeps every original pinned operation in order and may add
     * metadata-identical clones. The transform owns the dynamic-count/order
     * proof; the manager audits the static clone fidelity. */
    PASS_PINNED_METADATA_CLONES,
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
/* Exact constant-edge query shared by simplify-cfg and analysis clients.
 * It recursively asks the common scalar folder for the bounded definition
 * tree feeding a terminator, without mutating the function. */
bool opt_cfg_edge_feasible(const IrFunc *f, const IrInst *term, u32 edge);

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

/* IDs in loop plans are transaction-stable: appending blocks/values may move
 * the arena-backed arrays but leaves the numeric IDs valid.  Block compaction
 * or ir_func_renumber invalidates every plan and requires all analyses to be
 * rebuilt.  No loop plan may retain an IrBlock/IrInst/IrEdge pointer. */
typedef struct LoopInduction {
    BlockId header;
    BlockId preheader;
    BlockId latch;
    ValueId iv;
    ValueId update;
    ValueId compare;
    u32 param_index;
    IrType type;
    IrIcmp pred;
    IrOperand start;
    IrOperand step;
    IrOperand bound;
    u8 continue_edge;
    bool subtract_step;
    bool signed_no_wrap;
    bool modular;
} LoopInduction;

typedef enum LoopTripKind {
    LOOP_TRIP_UNKNOWN,
    LOOP_TRIP_CONSTANT,
    LOOP_TRIP_RUNTIME,
} LoopTripKind;

typedef struct TripInfo {
    LoopTripKind kind;
    LoopInduction induction;
    u64 constant;
} TripInfo;

/* The returned analysis is valid only until the next CFG/value mutation. */
bool loop_trip_analyze(const IrFunc *f, const Loop *loop, bool fwrapv,
                       TripInfo *out, const char **reason);
bool loop_operand_invariant(const IrFunc *f, const Loop *loop, IrOperand op);

typedef enum LoopClonePinnedPolicy {
    LOOP_CLONE_REJECT_PINNED,
    LOOP_CLONE_PATH_EXCLUSIVE,
} LoopClonePinnedPolicy;

typedef struct LoopCloneMap {
    /* Old numeric IDs index these maps.  Non-region entries are zero/none. */
    BlockId *blocks;
    u32 nblocks;
    IrOperand *values;
    u32 nvalues;
    BlockId entry;
    bool cloned_pinned;
} LoopCloneMap;

/* Clone a complete region without compacting or renumbering the function.
 * Internal targets and all value uses are remapped; external exit targets
 * stay shared and their LCSSA argument lists are deep-copied/remapped. */
bool loop_clone_region(IrModule *m, IrFunc *f, const BlockId *region,
                       u32 nregion, BlockId entry,
                       LoopClonePinnedPolicy pinned_policy,
                       const char *name_prefix, LoopCloneMap *out,
                       const char **reason);
BlockId loop_clone_block(const LoopCloneMap *map, BlockId old);
IrOperand loop_clone_operand(const LoopCloneMap *map, IrOperand old);

/* Sprint 34 loop transforms run only after the scalar/IPO fixpoint, so CFG
 * cleanup cannot tear down their dedicated preheaders and exits. */
extern const Pass OPT_PASS_LICM;
extern const Pass OPT_PASS_STRENGTH;
extern const Pass OPT_PASS_UNROLL;
extern const Pass OPT_PASS_UNSWITCH;
extern const Pass OPT_PASS_BCE;
extern const Pass OPT_PASS_FUSION;
extern const Pass OPT_PASS_VECTORIZE;
bool opt_licm(IrModule *m, const OptConfig *cfg);
bool opt_strength(IrModule *m, const OptConfig *cfg);
bool opt_unroll(IrModule *m, const OptConfig *cfg);
bool opt_unswitch(IrModule *m, const OptConfig *cfg);
bool opt_bce(IrModule *m, const OptConfig *cfg);
bool opt_fusion(IrModule *m, const OptConfig *cfg);
bool opt_vectorize(IrModule *m, const OptConfig *cfg);
bool opt_func_has_vector_ir(const IrFunc *f);
bool opt_module_has_vector_ir(const IrModule *m);
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
    /* 1 = undef on every incoming path; 2 = both undef and defined paths.
     * The latter may carry the branch that admits the undef value. */
    u8 classification;
    u8 decision_kind; /* 0 none/path, 1 true edge, 2 false edge */
    bool self_init;
    bool suppress_same_predicate;
    bool path_undecided;    /* bounded witness could not prove correlation */
    u32 decision_predicate; /* SSA value id when the witness is condbr */
    Span decision_loc;
} UndefUse;

enum { UNDEF_USE_DEFINITE = 1, UNDEF_USE_MAYBE = 2 };

const UndefUse *opt_mem2reg_undef_log(const IrFunc *f, u32 *n);

/* Unreachable-code provenance retained before simplify_cfg deletes blocks.
 * Sprint 40 consumes these resolved Spans after PP state has gone away. */
typedef IrCfgRemoved CfgRemoved;

const CfgRemoved *opt_cfg_removed_log(const IrFunc *f, u32 *n);

#endif
