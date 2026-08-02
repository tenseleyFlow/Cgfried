#include "warn/flow.h"

#include <stdio.h>
#include <string.h>

#include "opt/opt.h"
#include "warn/warn.h"

struct FlowCtx {
    Arena *arena;
    IrModule *module;
    IrFunc *function;
    IrDomTree *dom;
    bool *reachable;
    bool *noreturn_cut;
};

static bool block_noreturn_cut(const IrBlock *block)
{
    const IrInst *in;

    for (in = block->first; in; in = in->next)
        if (in->op == IR_CALL && (in->flags & IRF_NORETURN))
            return true;
    return false;
}

FlowCtx *flow_ctx_new(Arena *arena, IrModule *module, IrFunc *function)
{
    FlowCtx *fc = arena_alloc(arena, sizeof(*fc), _Alignof(FlowCtx));
    u32 *work;
    u32 nwork = 0;
    u32 i;

    memset(fc, 0, sizeof(*fc));
    fc->arena = arena;
    fc->module = module;
    fc->function = function;
    fc->reachable =
        arena_alloc(arena, function->nblocks ? function->nblocks : 1, 1);
    fc->noreturn_cut =
        arena_alloc(arena, function->nblocks ? function->nblocks : 1, 1);
    work = arena_alloc(
        arena, (function->nblocks ? function->nblocks : 1) * sizeof(*work),
        _Alignof(u32));
    memset(fc->reachable, 0, function->nblocks);
    memset(fc->noreturn_cut, 0, function->nblocks);
    for (i = 0; i < function->nblocks; i++)
        fc->noreturn_cut[i] = block_noreturn_cut(&function->blocks[i]);
    if (function->nblocks) {
        fc->reachable[0] = true;
        work[nwork++] = 0;
    }
    while (nwork) {
        u32 bi = work[--nwork];
        const IrInst *term = function->blocks[bi].last;
        u32 ei;

        if (!term || fc->noreturn_cut[bi])
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;

            if (!opt_cfg_edge_feasible(function, term, ei) || target == 0 ||
                target > function->nblocks || fc->reachable[target - 1])
                continue;
            fc->reachable[target - 1] = true;
            work[nwork++] = target - 1;
        }
    }
    fc->dom = ir_domtree_build(arena, function);
    return fc;
}

bool flow_reachable(const FlowCtx *fc, BlockId block)
{
    return fc && block.v >= 1 && block.v <= fc->function->nblocks &&
           fc->reachable[block.v - 1];
}

bool flow_dominates(const FlowCtx *fc, BlockId dominator, BlockId block)
{
    return fc && flow_reachable(fc, dominator) && flow_reachable(fc, block) &&
           ir_dominates(fc->dom, dominator, block);
}

const char *flow_path_note(FlowCtx *fc, BlockId from, BlockId to)
{
    const IrBlock *block;
    const IrInst *term;
    const char *path = "selected";
    Span span = {0};
    char text[192];
    u32 i;

    if (!fc || from.v < 1 || from.v > fc->function->nblocks)
        return NULL;
    block = &fc->function->blocks[from.v - 1];
    term = block->last;
    if (!term)
        return NULL;
    for (i = 0; i < term->nedges; i++)
        if (term->edges[i].target.v == to.v) {
            if (term->op == IR_CONDBR)
                path = i == 0 ? "true" : "false";
            else if (term->op == IR_SWITCH)
                path = i == 0 ? "default" : "case";
            break;
        }
    span = ir_inst_span(fc->module, term);
    snprintf(text, sizeof(text), "the branch at line %u takes the %s path",
             (unsigned)(span.presumed_line ? span.presumed_line : span.line),
             path);
    return arena_strdup(fc->arena, text);
}

static bool block_in_cycle(const FlowCtx *fc, BlockId start)
{
    bool *seen;
    u32 *work;
    u32 nwork = 0;

    if (!flow_reachable(fc, start))
        return false;
    seen = arena_alloc(fc->arena, fc->function->nblocks, 1);
    work = arena_alloc(fc->arena, fc->function->nblocks * sizeof(*work),
                       _Alignof(u32));
    memset(seen, 0, fc->function->nblocks);
    seen[start.v - 1] = true;
    work[nwork++] = start.v - 1;
    while (nwork) {
        u32 bi = work[--nwork];
        const IrInst *term = fc->function->blocks[bi].last;
        u32 ei;

        if (fc->noreturn_cut[bi])
            continue;
        if (!term)
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;

            if (!opt_cfg_edge_feasible(fc->function, term, ei) || !target ||
                target > fc->function->nblocks)
                continue;
            if (target == start.v)
                return true;
            if (!seen[target - 1]) {
                seen[target - 1] = true;
                work[nwork++] = target - 1;
            }
        }
    }
    return false;
}

static void emit_uninitialized(WarnCtx *warnings, FlowCtx *fc)
{
    const UndefUse *uses;
    u32 nuses = 0;
    u32 i;

    uses = opt_mem2reg_undef_log(fc->function, &nuses);
    for (i = 0; i < nuses; i++) {
        const UndefUse *use = &uses[i];
        const char *name = use->name ? use->name : "variable";
        WarnId id;

        if (!flow_reachable(fc, use->block))
            continue;
        if (use->self_init) {
            if (warn_enabled(warnings, WARN_INIT_SELF, use->loc)) {
                warn_at(warnings, WARN_INIT_SELF, use->loc,
                        "'%s' is initialized with itself", name);
                if (use->decl_loc.file_id)
                    diag_emit(warn_diag(warnings), DIAG_NOTE, use->decl_loc,
                              "'%s' was declared here", name);
            }
            continue;
        }
        id = use->classification == UNDEF_USE_DEFINITE
                 ? WARN_UNINITIALIZED
                 : WARN_MAYBE_UNINITIALIZED;
        if (!warn_enabled(warnings, id, use->loc))
            continue;
        if (id == WARN_MAYBE_UNINITIALIZED &&
            (use->suppress_same_predicate || use->path_undecided ||
             block_in_cycle(fc, use->block) || use->decision_kind == 0) &&
            !warn_maybe_uninitialized_strict(warnings))
            continue;
        if (id == WARN_UNINITIALIZED)
            warn_at(warnings, id, use->loc,
                    "'%s' is used uninitialized in this function", name);
        else
            warn_at(warnings, id, use->loc,
                    "'%s' may be used uninitialized in this function", name);
        if (id == WARN_MAYBE_UNINITIALIZED && use->decision_loc.file_id)
            diag_emit(warn_diag(warnings), DIAG_NOTE, use->decision_loc,
                      "'%s' is uninitialized when this branch takes the %s "
                      "path",
                      name,
                      use->decision_kind == 1   ? "true"
                      : use->decision_kind == 2 ? "false"
                      : use->decision_kind == 3 ? "default"
                                                : "case");
        if (use->decl_loc.file_id)
            diag_emit(warn_diag(warnings), DIAG_NOTE, use->decl_loc,
                      "'%s' was declared here", name);
    }
}

static bool *reverse_reachable(FlowCtx *fc, BlockId target)
{
    bool *can = arena_alloc(fc->arena, fc->function->nblocks, 1);
    bool moved;
    u32 bi;

    memset(can, 0, fc->function->nblocks);
    if (flow_reachable(fc, target))
        can[target.v - 1] = true;
    do {
        moved = false;
        for (bi = 0; bi < fc->function->nblocks; bi++) {
            const IrInst *term = fc->function->blocks[bi].last;
            u32 ei;

            if (!fc->reachable[bi] || can[bi] || fc->noreturn_cut[bi] || !term)
                continue;
            for (ei = 0; ei < term->nedges; ei++) {
                u32 to = term->edges[ei].target.v;

                if (opt_cfg_edge_feasible(fc->function, term, ei) && to &&
                    to <= fc->function->nblocks && can[to - 1]) {
                    can[bi] = true;
                    moved = true;
                    break;
                }
            }
        }
    } while (moved);
    return can;
}

static void emit_return_type(WarnCtx *warnings, FlowCtx *fc)
{
    IrFunc *f = fc->function;
    BlockId falloff = BLOCK_INVALID;
    Span where = ir_debug_loc(fc->module, f->loc);
    u32 bi;

    for (bi = 0; bi < f->nblocks && !falloff.v; bi++) {
        const IrInst *in;

        if (!fc->reachable[bi])
            continue;
        for (in = f->blocks[bi].first; in; in = in->next) {
            if (in->op == IR_CALL && (in->flags & IRF_NORETURN))
                break;
            if (in->op == IR_RET && (in->flags & IRF_FLOW_PROVENANCE)) {
                falloff.v = bi + 1;
                break;
            }
        }
    }
    if (!falloff.v || !warn_enabled(warnings, WARN_RETURN_TYPE, where))
        return;
    warn_at(warnings, WARN_RETURN_TYPE, where,
            "control reaches end of non-void function");
    {
        bool *can = reverse_reachable(fc, falloff);

        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *term = f->blocks[bi].last;

            if (!fc->reachable[bi] || !term || term->op != IR_CONDBR ||
                term->nedges != 2)
                continue;
            if (can[term->edges[0].target.v - 1] !=
                can[term->edges[1].target.v - 1]) {
                u32 edge = can[term->edges[0].target.v - 1] ? 0u : 1u;
                Span at = ir_inst_span(fc->module, term);

                diag_emit(warn_diag(warnings), DIAG_NOTE, at,
                          "when the %s branch is taken, no return statement "
                          "is reached",
                          edge == 0 ? "true" : "false");
                break;
            }
        }
    }
}

static bool same_span(Span a, Span b)
{
    return a.file_id == b.file_id && a.line == b.line && a.col == b.col &&
           a.len == b.len && a.seq == b.seq && a.origin == b.origin;
}

static void emit_unreachable(WarnCtx *warnings, FlowCtx *fc)
{
    const CfgRemoved *removed;
    Span function_span = ir_debug_loc(fc->module, fc->function->loc);
    u32 nremoved = 0;
    u32 i, j;

    removed = opt_cfg_removed_log(fc->function, &nremoved);
    for (i = 0; i < nremoved; i++) {
        Span at = removed[i].loc;
        bool duplicate = false;

        if (removed[i].region)
            for (j = 0; j < i; j++)
                if (removed[j].region == removed[i].region) {
                    duplicate = true;
                    break;
                }
        if (!at.file_id || same_span(at, function_span) ||
            (removed[i].flags &
             (IR_CFG_REMOVED_CONFIG | IR_CFG_REMOVED_DEFENSIVE_BREAK)))
            continue;
        for (j = 0; !duplicate && j < i; j++)
            if (same_span(removed[j].loc, at)) {
                duplicate = true;
                break;
            }
        if (!duplicate && warn_enabled(warnings, WARN_UNREACHABLE_CODE, at))
            warn_at(warnings, WARN_UNREACHABLE_CODE, at,
                    "code will never be executed");
    }
    /* A noreturn call cuts control before the syntactic terminator in the
     * same lowered block.  CFG deletion cannot expose that intra-block
     * boundary, so report the first later sourced instruction directly. */
    for (i = 0; i < fc->function->nblocks; i++) {
        const IrInst *in;
        bool after_cut = false;
        Span cut_span = {0};

        if (!fc->reachable[i])
            continue;
        for (in = fc->function->blocks[i].first; in; in = in->next) {
            Span at;

            if (!after_cut) {
                if (in->op == IR_CALL && (in->flags & IRF_NORETURN)) {
                    after_cut = true;
                    cut_span = ir_inst_span(fc->module, in);
                }
                continue;
            }
            at = ir_inst_span(fc->module, in);
            if (!at.file_id || same_span(at, cut_span))
                continue;
            if (warn_enabled(warnings, WARN_UNREACHABLE_CODE, at))
                warn_at(warnings, WARN_UNREACHABLE_CODE, at,
                        "code will never be executed");
            break;
        }
    }
}

static bool block_calls_self_before_cut(const IrFunc *f, u32 fidx, u32 bi)
{
    const IrInst *in;

    for (in = f->blocks[bi].first; in; in = in->next) {
        if (in->op == IR_CALL && in->subop == FUNCREF_INTERNAL &&
            in->callee == fidx)
            return true;
        if (in->op == IR_CALL && (in->flags & IRF_NORETURN))
            return false;
    }
    return false;
}

static void emit_infinite_recursion(WarnCtx *warnings, FlowCtx *fc, u32 fidx)
{
    IrFunc *f = fc->function;
    bool *must;
    bool moved;
    Span where = ir_debug_loc(fc->module, f->loc);
    u32 bi;

    if (!warn_enabled(warnings, WARN_INFINITE_RECURSION, where))
        return;
    must = arena_alloc(fc->arena, f->nblocks ? f->nblocks : 1, 1);
    memset(must, 0, f->nblocks);
    do {
        moved = false;
        for (bi = f->nblocks; bi-- > 0;) {
            const IrInst *term;
            bool next = false;
            u32 ei;

            if (!fc->reachable[bi] || must[bi])
                continue;
            if (block_calls_self_before_cut(f, fidx, bi))
                next = true;
            else if (!fc->noreturn_cut[bi]) {
                term = f->blocks[bi].last;
                if (term && term->nedges) {
                    next = true;
                    for (ei = 0; ei < term->nedges; ei++) {
                        u32 to = term->edges[ei].target.v;

                        if (opt_cfg_edge_feasible(f, term, ei) &&
                            (!to || to > f->nblocks || !must[to - 1])) {
                            next = false;
                            break;
                        }
                    }
                }
            }
            if (next) {
                must[bi] = true;
                moved = true;
            }
        }
    } while (moved);
    if (f->nblocks && must[0])
        warn_at(warnings, WARN_INFINITE_RECURSION, where,
                "all paths through this function will call itself");
}

void warn_flow_module(WarnCtx *warnings, const IrModule *source)
{
    Arena analysis;
    IrModule *module;
    OptConfig cfg;
    u32 i;

    if (!warnings || !source || !warn_flow_needed(warnings))
        return;
    arena_init(&analysis);
    module = ir_module_clone(&analysis, source);
    opt_config_init(&cfg, OPT_O0);
    (void)opt_mem2reg(module, &cfg);
    for (i = 0; i < module->nfuncs; i++) {
        FlowCtx *fc = flow_ctx_new(&analysis, module, &module->funcs[i]);

        emit_uninitialized(warnings, fc);
    }
    (void)opt_simplify_cfg(module, &cfg);
    for (i = 0; i < module->nfuncs; i++) {
        FlowCtx *fc = flow_ctx_new(&analysis, module, &module->funcs[i]);

        emit_return_type(warnings, fc);
        emit_unreachable(warnings, fc);
        emit_infinite_recursion(warnings, fc, i);
    }
    arena_free_all(&analysis);
}
