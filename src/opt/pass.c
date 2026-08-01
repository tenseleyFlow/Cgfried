#include "opt/opt.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util/buf.h"

typedef struct {
    const Pass *pass;
    u32 invocations;
    double seconds;
} PassTiming;

typedef struct {
    PassTiming *timings;
    u32 ntimings;
    u32 cap_timings;
    bool any_changed;
} RunCtx;

static FILE *report_file(const OptConfig *cfg)
{
    return cfg->report ? cfg->report : stderr;
}

void opt_config_init(OptConfig *cfg, OptLevel level)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->level = level;
    switch (level) {
    case OPT_O0:
    case OPT_O1:
        cfg->inline_threshold = 0;
        break;
    case OPT_OS:
        cfg->inline_threshold = 20;
        break;
    case OPT_O2:
        cfg->inline_threshold = 40;
        break;
    case OPT_O3:
    case OPT_OFAST:
        cfg->inline_threshold = 80;
        break;
    default:
        CGF_ICE("opt: unknown optimization level %d", (int)level);
    }
    cfg->unroll_threshold = level == OPT_OS ? 8 : 32;
    cfg->report = stderr;
    if (level == OPT_OFAST) {
        cfg->fast_math.reassoc = true;
        cfg->fast_math.no_nans = true;
        cfg->fast_math.no_infs = true;
        cfg->fast_math.no_signed_zeros = true;
        cfg->fast_math.reciprocal_math = true;
    }
}

void opt_bail(const OptConfig *cfg, const char *pass, const char *reason)
{
    if (!cfg->bail_log)
        return;
    fprintf(report_file(cfg), "bail: %s %s func=@%s\n", pass, reason,
            cfg->current_func ? cfg->current_func : "?");
}

static double now_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        CGF_ICE("opt: clock_gettime(CLOCK_MONOTONIC) failed");
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static PassTiming *timing_for(RunCtx *r, const Pass *pass)
{
    u32 i;

    for (i = 0; i < r->ntimings; i++)
        if (r->timings[i].pass == pass ||
            strcmp(r->timings[i].pass->name, pass->name) == 0)
            return &r->timings[i];
    if (r->ntimings == r->cap_timings) {
        u32 nc = r->cap_timings ? r->cap_timings * 2 : 8;
        PassTiming *nt = cgf_xrealloc(r->timings, nc * sizeof(*nt));

        r->timings = nt;
        r->cap_timings = nc;
    }
    memset(&r->timings[r->ntimings], 0, sizeof(*r->timings));
    r->timings[r->ntimings].pass = pass;
    return &r->timings[r->ntimings++];
}

static void dump_bad_ir(const OptConfig *cfg, const IrModule *m,
                        const char *why)
{
    FILE *out;

    if (!cfg->dump_bad_ir)
        return;
    out = fopen(cfg->dump_bad_ir, "wb");
    if (!out)
        return;
    ir_print_module(out, m);
    fprintf(out, "// verify failed after optimization: %s\n", why);
    fclose(out);
}

static bool buf_differs(const Buf *a, const Buf *b)
{
    return a->len != b->len || (a->len && memcmp(a->data, b->data, a->len));
}

static bool run_one(RunCtx *r, IrModule *m, const OptConfig *cfg,
                    const Pass *pass)
{
    Buf before = {0}, after = {0};
    Arena verify_scratch;
    IrVolatileSnapshot *volatile_before = NULL;
    PassTiming *timing = NULL;
    double start = 0.0;
    bool changed;

    if (cfg->verify_after_each) {
        arena_init(&verify_scratch);
        buf_init(&before);
        ir_print_module_buf(&before, m);
        volatile_before = arena_alloc(
            &verify_scratch, ((size_t)m->nfuncs + 1) * sizeof(*volatile_before),
            _Alignof(IrVolatileSnapshot));
        ir_snapshot_volatile_order(&verify_scratch, m, volatile_before);
    }
    if (cfg->time_report) {
        timing = timing_for(r, pass);
        start = now_seconds();
    }
    changed = pass->run(m, cfg);
    if (timing) {
        timing->seconds += now_seconds() - start;
        timing->invocations++;
    }
    r->any_changed |= changed;

    if (cfg->verify_after_each) {
        char why[256];
        u32 bad_func = 0;
        bool different;

        buf_init(&after);
        ir_print_module_buf(&after, m);
        different = buf_differs(&before, &after);
        if (changed != different) {
            dump_bad_ir(cfg, m, "changed-flag mismatch");
            CGF_ICE("opt: pass '%s' changed-flag mismatch: returned %s but "
                    "IR was %s",
                    pass->name, changed ? "true" : "false",
                    different ? "mutated" : "unchanged");
        }
        bool pinned_ok;

        switch (pass->pinned_policy) {
        case PASS_PINNED_EXACT:
            pinned_ok =
                ir_volatile_order_matches(m, volatile_before, &bad_func);
            break;
        case PASS_PINNED_INLINE_CLONES:
            pinned_ok = ir_pinned_inline_matches(m, volatile_before, &bad_func);
            break;
        case PASS_PINNED_DELETE_FUNCS:
            pinned_ok =
                ir_pinned_delete_funcs_matches(m, volatile_before, &bad_func);
            break;
        default:
            CGF_ICE("opt: pass '%s' has unknown pinned policy %d", pass->name,
                    (int)pass->pinned_policy);
        }
        if (!pinned_ok)
            CGF_ICE("opt: pass '%s' changed pinned operations in '@%s'",
                    pass->name,
                    bad_func < m->nfuncs ? m->funcs[bad_func].name : "?");
        if (!ir_verify_report(m->dc, m, why, sizeof(why))) {
            dump_bad_ir(cfg, m, why);
            CGF_ICE("opt: pass '%s' produced invalid IR (%s)", pass->name, why);
        }
        buf_free(&before);
        buf_free(&after);
        arena_free_all(&verify_scratch);
    }
    return changed;
}

static void print_timings(const OptConfig *cfg, const RunCtx *r)
{
    FILE *out;
    u32 i;

    if (!cfg->time_report)
        return;
    out = report_file(cfg);
    fprintf(out, "optimization time report:\n");
    fprintf(out, "  %-20s %11s %12s\n", "pass", "invocations", "wall-ms");
    for (i = 0; i < r->ntimings; i++)
        fprintf(out, "  %-20s %11u %12.3f\n", r->timings[i].pass->name,
                r->timings[i].invocations, r->timings[i].seconds * 1000.0);
}

static void run_ctx_free(RunCtx *r)
{
    free(r->timings);
}

bool opt_run_pass_sequence(IrModule *m, const OptConfig *cfg,
                           const Pass *const *passes, u32 npasses)
{
    RunCtx r = {0};
    u32 i;

    for (i = 0; i < npasses; i++)
        (void)run_one(&r, m, cfg, passes[i]);
    print_timings(cfg, &r);
    run_ctx_free(&r);
    return r.any_changed;
}

bool opt_run_fixpoint(IrModule *m, const OptConfig *cfg,
                      const Pass *const *passes, u32 npasses, u32 cap)
{
    RunCtx r = {0};
    bool *last_changed;
    u32 iteration, i;

    if (cap == 0)
        CGF_ICE("opt: fixpoint iteration cap must be nonzero");
    last_changed = cgf_xmalloc((npasses ? npasses : 1) * sizeof(bool));
    for (iteration = 0; iteration < cap; iteration++) {
        bool any = false;

        memset(last_changed, 0, npasses * sizeof(bool));
        for (i = 0; i < npasses; i++) {
            last_changed[i] = run_one(&r, m, cfg, passes[i]);
            any |= last_changed[i];
        }
        if (!any) {
            print_timings(cfg, &r);
            free(last_changed);
            run_ctx_free(&r);
            return r.any_changed;
        }
    }
    {
        Buf names;

        buf_init(&names);
        for (i = 0; i < npasses; i++) {
            if (!last_changed[i])
                continue;
            if (names.len)
                buf_append(&names, ", ", 2);
            buf_append(&names, passes[i]->name, strlen(passes[i]->name));
        }
        buf_push_u8(&names, 0);
        CGF_ICE("opt: fixpoint did not converge after %u iterations; still "
                "changing: %s",
                cap, names.data);
    }
}
