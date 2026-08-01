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

typedef struct Pass {
    const char *name;
    bool (*run)(IrModule *m, const OptConfig *cfg);
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

#endif
