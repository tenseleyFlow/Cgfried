#include "opt/opt.h"

typedef enum { STAGE_O1, STAGE_O2, STAGE_O3 } PipelineStage;

typedef struct {
    const Pass *pass;
    PipelineStage introduced_at;
} PipelineEntry;

/* The one-line-to-change law: every real pass gets exactly one row. A level
 * selects the rows introduced at or below its stage, so inherited pipelines
 * cannot drift and a new O2/O3 pass needs no orchestration plumbing. */
static const PipelineEntry pipeline[] = {
    {&OPT_PASS_MEM2REG, STAGE_O1},
    {&OPT_PASS_SCCP, STAGE_O1},
    {&OPT_PASS_SIMPLIFY, STAGE_O1},
    {&OPT_PASS_CSE, STAGE_O1},
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
    const Pass *passes[CGF_ARRAY_LEN(pipeline)];
    PipelineStage stage = STAGE_O1;
    u32 npasses = 0;
    u32 i;

    if (!level_stage(cfg->level, &stage))
        return opt_run_pass_sequence(m, cfg, NULL, 0);
    for (i = 0; i < CGF_ARRAY_LEN(pipeline); i++)
        if (pipeline[i].introduced_at <= stage)
            passes[npasses++] = pipeline[i].pass;
    if (cfg->level == OPT_O1)
        return opt_run_pass_sequence(m, cfg, passes, npasses);
    return opt_run_fixpoint(m, cfg, passes, npasses, 10);
}
