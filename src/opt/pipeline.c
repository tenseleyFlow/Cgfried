#include "opt/opt.h"

typedef enum { STAGE_O1, STAGE_O2, STAGE_O3 } PipelineStage;
typedef enum { GROUP_SCALAR, GROUP_LOOP, GROUP_UNROLL } PipelineGroup;

typedef struct {
    const Pass *pass;
    PipelineStage introduced_at;
    PipelineGroup group;
    bool at_os;
} PipelineEntry;

/* The one-line-to-change law: every real pass gets exactly one row.  Loop
 * passes form a second fixpoint after scalar/IPO convergence: running CFG
 * cleanup after loop canonicalization would delete dedicated preheaders and
 * exits, then rebuild them forever.  `at_os` is explicit because size mode
 * inherits O2 but permits the unroller's smaller-only full-unroll policy. */
static const PipelineEntry pipeline[] = {
    {&OPT_PASS_MEM2REG, STAGE_O1, GROUP_SCALAR, true},
    {&OPT_PASS_SCCP, STAGE_O1, GROUP_SCALAR, true},
    {&OPT_PASS_SIMPLIFY, STAGE_O1, GROUP_SCALAR, true},
    {&OPT_PASS_CSE, STAGE_O1, GROUP_SCALAR, true},
    {&OPT_PASS_DCE, STAGE_O1, GROUP_SCALAR, true},
    {&OPT_PASS_SIMPLIFY_CFG, STAGE_O1, GROUP_SCALAR, true},
    {&OPT_PASS_GVN, STAGE_O2, GROUP_SCALAR, true},
    {&OPT_PASS_DSE, STAGE_O2, GROUP_SCALAR, true},
    {&OPT_PASS_JUMP_THREAD, STAGE_O2, GROUP_SCALAR, true},
    {&OPT_PASS_IPO, STAGE_O2, GROUP_SCALAR, true},
    {&OPT_PASS_INLINE, STAGE_O2, GROUP_SCALAR, true},
    {&OPT_PASS_LICM, STAGE_O2, GROUP_LOOP, true},
    {&OPT_PASS_STRENGTH, STAGE_O2, GROUP_LOOP, true},
    {&OPT_PASS_UNROLL, STAGE_O3, GROUP_UNROLL, true},
};

static bool level_stage(OptLevel level, PipelineStage *out)
{
    switch (level) {
    case OPT_O0:
        return false;
    case OPT_O1:
        *out = STAGE_O1;
        return true;
    case OPT_O2:
    case OPT_OS:
        *out = STAGE_O2;
        return true;
    case OPT_O3:
    case OPT_OFAST:
        *out = STAGE_O3;
        return true;
    }
    CGF_ICE("opt: unknown optimization level %d", (int)level);
}

bool opt_run_pipeline(IrModule *m, const OptConfig *cfg)
{
    const Pass *scalar[CGF_ARRAY_LEN(pipeline)];
    const Pass *loops[CGF_ARRAY_LEN(pipeline)];
    const Pass *unroll[CGF_ARRAY_LEN(pipeline)];
    PipelineStage stage = STAGE_O1;
    u32 nscalar = 0, nloops = 0, nunroll = 0;
    u32 i;
    bool changed = false;

    if (!level_stage(cfg->level, &stage))
        return opt_run_pass_sequence(m, cfg, NULL, 0);
    for (i = 0; i < CGF_ARRAY_LEN(pipeline); i++) {
        const PipelineEntry *e = &pipeline[i];
        bool selected = e->introduced_at <= stage;

        if (cfg->level == OPT_OS)
            selected = e->at_os;
        if (!selected)
            continue;
        switch (e->group) {
        case GROUP_SCALAR:
            scalar[nscalar++] = e->pass;
            break;
        case GROUP_LOOP:
            loops[nloops++] = e->pass;
            break;
        case GROUP_UNROLL:
            unroll[nunroll++] = e->pass;
            break;
        }
    }
    if (cfg->level == OPT_O1)
        return opt_run_pass_sequence(m, cfg, scalar, nscalar);

    changed |= opt_run_fixpoint(m, cfg, scalar, nscalar, 10);
    changed |= opt_run_fixpoint(m, cfg, loops, nloops, 10);
    if (nunroll) {
        bool unrolled = opt_run_pass_sequence(m, cfg, unroll, nunroll);

        changed |= unrolled;
        if (unrolled)
            changed |= opt_run_fixpoint(m, cfg, loops, nloops, 10);
    }
    return changed;
}
