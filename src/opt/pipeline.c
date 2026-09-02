#include "opt/opt.h"

typedef enum { STAGE_O1, STAGE_O2, STAGE_O3 } PipelineStage;
typedef enum {
    GROUP_SCALAR,
    GROUP_FUSION,
    GROUP_VECTOR,
    GROUP_LOOP,
    GROUP_UNROLL
} PipelineGroup;

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
    /* Fusion must see source affine addresses before strength reduction
     * rewrites them into accumulator block parameters. */
    {&OPT_PASS_FUSION, STAGE_O3, GROUP_FUSION, false},
    {&OPT_PASS_VECTORIZE, STAGE_O3, GROUP_VECTOR, false},
    {&OPT_PASS_LICM, STAGE_O2, GROUP_LOOP, true},
    {&OPT_PASS_STRENGTH, STAGE_O2, GROUP_LOOP, true},
    {&OPT_PASS_BCE, STAGE_O2, GROUP_LOOP, true},
    {&OPT_PASS_UNSWITCH, STAGE_O3, GROUP_UNROLL, false},
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
    const Pass *semantic[] = {&OPT_PASS_PRUNE_CFG};
    const Pass *mandatory[] = {&OPT_PASS_FORCE_INLINE,
                               &OPT_PASS_STRIP_INLINE_ONLY};
    const Pass *scalar[CGF_ARRAY_LEN(pipeline)];
    const Pass *fusion[CGF_ARRAY_LEN(pipeline)];
    const Pass *vector[CGF_ARRAY_LEN(pipeline)];
    const Pass *loops[CGF_ARRAY_LEN(pipeline)];
    const Pass *unroll[CGF_ARRAY_LEN(pipeline)];
    PipelineStage stage = STAGE_O1;
    u32 nscalar = 0, nfusion = 0, nvector = 0, nloops = 0, nunroll = 0;
    u32 i;
    bool changed = false;
    bool needs_force_inline = false;

    /* Growth accounting spans every fixpoint visit inside this top-level run,
     * but a caller may deliberately run a new pipeline over the same module
     * (for example O2 followed by O3).  That is a fresh optimization budget. */
    for (i = 0; i < m->nfuncs; i++) {
        m->funcs[i].opt_inline_growth_left = 0;
        m->funcs[i].opt_inline_growth_initialized = false;
        needs_force_inline |= m->funcs[i].always_inline;
    }
    /* Constant control-flow is a semantic cleanup even at O0: retaining an
     * impossible reference can make a valid translation unit fail to link.
     * Keep this narrower than simplify_cfg so O0 block shape is otherwise
     * untouched, and run it through the normal verifier/pinned audit. */
    changed |= opt_run_pass_sequence(m, cfg, semantic, CGF_ARRAY_LEN(semantic));
    if (diag_had_error(m->dc))
        goto done;
    /* GNU always_inline is a source contract, not a profitability choice.
     * Run it before the optimization-level gate, then discard C inline-only
     * bodies that existed solely as splice input.  Keeping these as two
     * passes lets the pass manager audit pinned clones first and whole-body
     * deletion second, with the exact policy appropriate to each mutation. */
    if (needs_force_inline)
        changed |=
            opt_run_pass_sequence(m, cfg, mandatory, CGF_ARRAY_LEN(mandatory));
    if (diag_had_error(m->dc))
        goto done;
    if (!level_stage(cfg->level, &stage))
        goto done;
    for (i = 0; i < CGF_ARRAY_LEN(pipeline); i++) {
        const PipelineEntry *e = &pipeline[i];
        bool selected = e->introduced_at <= stage;

        if (cfg->level == OPT_OS)
            selected = e->at_os;
        if ((e->pass == &OPT_PASS_BCE && cfg->disable_bce) ||
            (e->pass == &OPT_PASS_FUSION && cfg->disable_fusion) ||
            (e->pass == &OPT_PASS_VECTORIZE && cfg->disable_vectorize) ||
            (e->pass == &OPT_PASS_UNSWITCH && cfg->disable_unswitch))
            selected = false;
        if (!selected)
            continue;
        switch (e->group) {
        case GROUP_SCALAR:
            scalar[nscalar++] = e->pass;
            break;
        case GROUP_FUSION:
            fusion[nfusion++] = e->pass;
            break;
        case GROUP_VECTOR:
            vector[nvector++] = e->pass;
            break;
        case GROUP_LOOP:
            loops[nloops++] = e->pass;
            break;
        case GROUP_UNROLL:
            unroll[nunroll++] = e->pass;
            break;
        }
    }
    if (cfg->level == OPT_O1) {
        bool scalar_changed = opt_run_pass_sequence(m, cfg, scalar, nscalar);

        changed |= scalar_changed;
        goto done;
    }

    changed |= opt_run_fixpoint(m, cfg, scalar, nscalar, 10);
    changed |= opt_run_fixpoint(m, cfg, fusion, nfusion, 10);
    changed |= opt_run_pass_sequence(m, cfg, vector, nvector);
    /* Each later loop transform explicitly skips vector-containing
     * functions.  Non-vector siblings still receive their normal O3 work. */
    changed |= opt_run_fixpoint(m, cfg, loops, nloops, 10);
    if (nunroll) {
        bool unrolled = opt_run_pass_sequence(m, cfg, unroll, nunroll);

        changed |= unrolled;
        if (unrolled)
            changed |= opt_run_fixpoint(m, cfg, loops, nloops, 10);
    }
done:
    /* IR text embeds asm records in print order.  CFG cleanup and cloning
     * may have removed or duplicated instructions since lowering, so make
     * the table match that textual representation before callers inspect or
     * serialize the optimized module. */
    ir_module_canonicalize_asms(m);
    return changed;
}
