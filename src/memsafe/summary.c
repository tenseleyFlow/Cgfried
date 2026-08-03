#include "memsafe/memsafe.h"

#include <limits.h>
#include <string.h>

#include "opt/opt.h"
#include "warn/warn.h"

struct MsSummarySet {
    IrModule *module;
    MsSummary *functions;
    MsSummary *externals;
};

#define B(n, d, w, e, f, r, o) {n, d, w, e, f, r, o, -1, -1}
#define W(n, d, w, e, f, r, o, s1, s2) {n, d, w, e, f, r, o, s1, s2}
#define A(n) (1ull << (n))
/* This table intentionally includes non-owning library routines.  Unknown
 * externals remain conservative; a named row is a reviewed contract. */
static const MsLibSummary libc_summaries[] = {
    W("memcpy", A(0) | A(1), A(0), 0, 0, 0, false, 2, -1),
    W("memmove", A(0) | A(1), A(0), 0, 0, 0, false, 2, -1),
    W("memset", A(0), A(0), 0, 0, 0, false, 2, -1),
    B("memcmp", A(0) | A(1), 0, 0, 0, -1, false),
    B("memchr", A(0), 0, 0, 0, 0, false),
    B("strlen", A(0), 0, 0, 0, -1, false),
    B("strnlen", A(0), 0, 0, 0, -1, false),
    B("strcmp", A(0) | A(1), 0, 0, 0, -1, false),
    B("strncmp", A(0) | A(1), 0, 0, 0, -1, false),
    B("strcasecmp", A(0) | A(1), 0, 0, 0, -1, false),
    B("strncasecmp", A(0) | A(1), 0, 0, 0, -1, false),
    B("strcpy", A(0) | A(1), A(0), 0, 0, 0, false),
    W("strncpy", A(0) | A(1), A(0), 0, 0, 0, false, 2, -1),
    B("strcat", A(0) | A(1), A(0), 0, 0, 0, false),
    B("strncat", A(0) | A(1), A(0), 0, 0, 0, false),
    B("strchr", A(0), 0, 0, 0, 0, false),
    B("strrchr", A(0), 0, 0, 0, 0, false),
    B("strstr", A(0) | A(1), 0, 0, 0, 0, false),
    B("strpbrk", A(0) | A(1), 0, 0, 0, 0, false),
    B("strspn", A(0) | A(1), 0, 0, 0, -1, false),
    B("strcspn", A(0) | A(1), 0, 0, 0, -1, false),
    B("strtok", A(0) | A(1), A(0), A(0), 0, 0, false),
    B("strerror_r", A(1), A(1), 0, 0, -1, false),
    W("bcopy", A(0) | A(1), A(1), 0, 0, -1, false, 2, -1),
    W("bzero", A(0), A(0), 0, 0, -1, false, 1, -1),
    W("fread", A(0) | A(3), A(0), 0, 0, -1, false, 1, 2),
    B("fwrite", A(0) | A(3), 0, 0, 0, -1, false),
    B("fgets", A(0) | A(2), A(0), 0, 0, 0, false),
    B("fputs", A(0) | A(1), 0, 0, 0, -1, false),
    B("puts", A(0), 0, 0, 0, -1, false),
    B("printf", A(0), 0, 0, 0, -1, false),
    B("fprintf", A(0) | A(1), 0, 0, 0, -1, false),
    W("snprintf", A(0) | A(2), A(0), 0, 0, -1, false, 1, -1),
    B("sprintf", A(0) | A(1), A(0), 0, 0, -1, false),
    B("scanf", A(0), 0, 0, 0, -1, false),
    B("sscanf", A(0) | A(1), 0, 0, 0, -1, false),
    B("fscanf", A(0) | A(1), 0, 0, 0, -1, false),
    B("getenv", A(0), 0, 0, 0, -1, false),
    W("getcwd", A(0), A(0), 0, 0, 0, false, 1, -1),
    B("realpath", A(0) | A(1), A(1), 0, 0, 1, false),
    W("read", A(1), A(1), 0, 0, -1, false, 2, -1),
    B("write", A(1), 0, 0, 0, -1, false),
    W("pread", A(1), A(1), 0, 0, -1, false, 2, -1),
    B("pwrite", A(1), 0, 0, 0, -1, false),
    B("qsort", A(0), A(0), 0, 0, -1, false),
    B("bsearch", A(0) | A(1), 0, 0, 0, 1, false),
    B("free", 0, 0, 0, A(0), -1, false),
    B("fopen", A(0) | A(1), 0, 0, 0, -1, true),
    B("fdopen", A(1), 0, 0, 0, -1, true),
    B("tmpfile", 0, 0, 0, 0, -1, true),
    B("popen", A(0) | A(1), 0, 0, 0, -1, true),
    /* freopen closes the old stream even when opening the replacement fails. */
    B("freopen", A(0) | A(1) | A(2), 0, 0, A(2), -1, true),
    B("fflush", A(0), 0, 0, 0, -1, false),
    B("fclose", A(0), 0, 0, A(0), -1, false),
    B("pclose", A(0), 0, 0, A(0), -1, false),
};
#undef A
#undef B
#undef W

const MsLibSummary *ms_lib_summary_lookup(const char *name)
{
    u32 i;

    if (!name)
        return NULL;
    for (i = 0; i < CGF_ARRAY_LEN(libc_summaries); i++)
        if (strcmp(libc_summaries[i].name, name) == 0)
            return &libc_summaries[i];
    return NULL;
}

u32 ms_lib_summary_variadic_from(const MsLibSummary *summary)
{
    const char *name = summary ? summary->name : NULL;

    if (!name)
        return UINT32_MAX;
    if (strcmp(name, "printf") == 0 || strcmp(name, "scanf") == 0)
        return 1;
    if (strcmp(name, "fprintf") == 0 || strcmp(name, "sprintf") == 0 ||
        strcmp(name, "sscanf") == 0 || strcmp(name, "fscanf") == 0)
        return 2;
    if (strcmp(name, "snprintf") == 0)
        return 3;
    return UINT32_MAX;
}

static u32 attr_param(const CgfAttr *attr)
{
    u32 one_based = attr->ir_arg ? attr->ir_arg : attr->arg;

    return one_based ? one_based - 1 : UINT32_MAX;
}

static bool span_present(Span span)
{
    return span.file_id != 0;
}

static void apply_contract(MsSummary *summary, const CgfAttr *attrs,
                           WarnCtx *warnings, bool compare, Span primary)
{
    const CgfAttr *attr;
    bool warned[CGF_ATTR_COUNT] = {false};

    for (attr = attrs; attr; attr = attr->next) {
        u32 p = attr_param(attr);
        MsParamSummary *effect =
            p < summary->nparams ? &summary->params[p] : NULL;
        bool mismatch = false;
        Span witness = {0};

        switch (attr->kind) {
        case CGF_ATTR_RETURNS_OWNED:
            /* Absence of a fresh return is not contrary evidence: NULL and
             * unknown paths are legal.  A proven borrowed alias is. */
            if (compare) {
                u32 i;
                for (i = 0; i < summary->nparams; i++) {
                    mismatch |= summary->params[i].returned_alias;
                    if (!span_present(witness) &&
                        summary->params[i].returned_alias)
                        witness = summary->params[i].return_alias_span;
                }
            }
            summary->returns_ownership = true;
            summary->annot_returns_owned = true;
            break;
        case CGF_ATTR_TAKES_OWNERSHIP:
            if (!effect)
                break;
            effect->may_free = true;
            effect->must_free = true;
            effect->annot_takes_ownership = true;
            break;
        case CGF_ATTR_BORROWS:
            if (!effect)
                break;
            mismatch = effect->may_free || effect->escapes;
            witness =
                effect->may_free ? effect->free_span : effect->escape_span;
            effect->may_free = false;
            effect->must_free = false;
            effect->escapes = false;
            effect->annot_borrow = true;
            break;
        case CGF_ATTR_RETURNS_BORROWED:
            if (!effect)
                break;
            mismatch = summary->returns_ownership;
            witness = summary->returns_ownership_span;
            summary->returns_ownership = false;
            effect->returned_alias = true;
            effect->annot_returns_borrowed = true;
            break;
        case CGF_ATTR_NO_ESCAPE:
            if (!effect)
                break;
            mismatch = effect->escapes;
            witness = effect->escape_span;
            effect->escapes = false;
            effect->annot_no_escape = true;
            break;
        case CGF_ATTR_COUNT:
            break;
        }
        if (compare && mismatch && warnings && !warned[attr->kind] &&
            warn_enabled(warnings, WARN_MEM_ANNOTATION_MISMATCH, primary)) {
            warned[attr->kind] = true;
            warn_at(warnings, WARN_MEM_ANNOTATION_MISMATCH, primary,
                    "function annotated %s contradicts its body",
                    cgf_attr_name(attr->kind));
            diag_emit(warn_diag(warnings), DIAG_NOTE, attr->span,
                      "ownership annotation is here");
            if (span_present(witness))
                diag_emit(warn_diag(warnings), DIAG_NOTE, witness,
                          "contradicting body effect is here");
        }
    }
}

static bool pts_param(AliasCtx *alias, IrOperand operand, u32 param)
{
    return operand.type == IRT_PTR &&
           alias_pts_has_param(alias, alias_points_to(alias, operand), param);
}

static const IrInst *summary_value_def(const IrFunc *function,
                                       IrOperand operand)
{
    const IrValInfo *info;
    const IrInst *in;

    if (operand.kind != IROP_VALUE || operand.a == 0 ||
        operand.a > function->nvals)
        return NULL;
    info = &function->vals[operand.a - 1];
    if (info->def_kind != VDEF_INST || info->def_block.v == 0 ||
        info->def_block.v > function->nblocks)
        return NULL;
    for (in = function->blocks[info->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == operand.a)
            return in;
    return NULL;
}

static bool summary_operand_constant(const IrFunc *function, IrOperand operand,
                                     i64 *value, u32 depth)
{
    const IrInst *def;
    IrInst folded;
    IrOperand ops[3], out;
    OptConfig cfg;
    u32 i;

    if (operand.kind == IROP_ICONST) {
        *value = (i64)operand.a;
        return true;
    }
    if (depth > function->nvals)
        return false;
    def = summary_value_def(function, operand);
    if (!def || def->nops > CGF_ARRAY_LEN(ops))
        return false;
    if (def->op == IR_BITCAST && def->nops == 1)
        return summary_operand_constant(function, def->ops[0], value,
                                        depth + 1);
    switch (def->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
        break;
    default:
        return false;
    }
    folded = *def;
    for (i = 0; i < def->nops; i++) {
        i64 constant;

        if (!summary_operand_constant(function, def->ops[i], &constant,
                                      depth + 1))
            return false;
        ops[i] = ir_op_iconst((IrType)def->ops[i].type, constant);
    }
    folded.ops = ops;
    opt_config_init(&cfg, OPT_O0);
    if (!opt_fold_inst(&folded, &out, &cfg) || out.kind != IROP_ICONST)
        return false;
    *value = (i64)out.a;
    return true;
}

static bool summary_local_address(const IrFunc *function, IrOperand operand,
                                  u32 depth)
{
    const IrInst *def;

    if (depth > function->nvals)
        return false;
    def = summary_value_def(function, operand);
    if (!def)
        return false;
    if (def->op == IR_ALLOCA)
        return true;
    if ((def->op == IR_PTRADD || def->op == IR_BITCAST) && def->nops >= 1)
        return summary_local_address(function, def->ops[0], depth + 1);
    return false;
}

static bool summary_call_arg(const IrInst *call, u32 index, IrOperand *out)
{
    u32 first = call->subop == FUNCREF_INDIRECT ? 1 : 0;

    if (call->op != IR_CALL || first + index >= call->nops)
        return false;
    *out = call->ops[first + index];
    return true;
}

static bool lib_write_extent(const IrFunc *function, const MsLibSummary *lib,
                             const IrInst *call, u64 *extent)
{
    IrOperand operand;
    i64 first, second = 1;

    if (!lib || lib->write_size_arg < 0 ||
        !summary_call_arg(call, (u32)lib->write_size_arg, &operand) ||
        !summary_operand_constant(function, operand, &first, 0) || first < 0)
        return false;
    if (lib->write_size_arg2 >= 0 &&
        (!summary_call_arg(call, (u32)lib->write_size_arg2, &operand) ||
         !summary_operand_constant(function, operand, &second, 0) ||
         second < 0))
        return false;
    if ((u64)second && (u64)first > UINT64_MAX / (u64)second)
        return false;
    *extent = (u64)first * (u64)second;
    return true;
}

static void mark_operand_params(AliasCtx *alias, MsSummary *summary,
                                IrOperand operand, u64 deref, u64 write,
                                u64 escape, u64 free_mask, bool returned,
                                Span loc)
{
    u32 p;

    if (operand.type != IRT_PTR)
        return;
    for (p = 0; p < summary->nparams; p++) {
        MsParamSummary *effect;

        if (!pts_param(alias, operand, p))
            continue;
        effect = &summary->params[p];
        effect->dereferenced |= deref != 0;
        effect->written |= write != 0;
        effect->escapes |= escape != 0;
        effect->may_free |= free_mask != 0;
        effect->returned_alias |= returned;
        if (free_mask != 0 && !span_present(effect->free_span))
            effect->free_span = loc;
        if (escape != 0 && !span_present(effect->escape_span))
            effect->escape_span = loc;
        if (returned && !span_present(effect->return_alias_span))
            effect->return_alias_span = loc;
    }
}

static void mark_write_subrange(AliasCtx *alias, MsSummary *summary,
                                IrOperand operand, i64 rel_lo, i64 rel_hi)
{
    i64 lo, hi;
    u32 p;

    if (rel_hi < rel_lo || !alias_offset_range(alias, operand, &lo, &hi) ||
        (rel_lo > 0 && lo > INT64_MAX - rel_lo) ||
        (rel_lo < 0 && lo < INT64_MIN - rel_lo) ||
        (rel_hi > 0 && hi > INT64_MAX - rel_hi) ||
        (rel_hi < 0 && hi < INT64_MIN - rel_hi))
        return;
    lo += rel_lo;
    hi += rel_hi;
    for (p = 0; p < summary->nparams; p++) {
        MsParamSummary *e = &summary->params[p];

        if (!pts_param(alias, operand, p))
            continue;
        if (e->write_range_unknown)
            continue;
        if (lo == hi) {
            if (!e->write_range_known) {
                e->write_lo = lo;
                e->write_hi = hi;
                e->write_range_known = true;
            }
            continue;
        }
        if (!e->write_range_known) {
            e->write_lo = lo;
            e->write_hi = hi;
            e->write_range_known = true;
        } else {
            if (e->write_lo == e->write_hi) {
                e->write_lo = lo;
                e->write_hi = hi;
                continue;
            }
            if (hi < e->write_lo || lo > e->write_hi) {
                e->write_range_known = false;
                e->write_range_unknown = true;
                continue;
            }
            if (lo < e->write_lo)
                e->write_lo = lo;
            if (hi > e->write_hi)
                e->write_hi = hi;
        }
    }
}

static void mark_write_range(AliasCtx *alias, MsSummary *summary,
                             IrOperand operand, u64 size)
{
    if (size <= INT64_MAX)
        mark_write_subrange(alias, summary, operand, 0, (i64)size);
}

static void mark_unknown_write(AliasCtx *alias, MsSummary *summary,
                               IrOperand operand)
{
    u32 p;

    for (p = 0; p < summary->nparams; p++) {
        MsParamSummary *effect = &summary->params[p];

        if (!pts_param(alias, operand, p))
            continue;
        effect->write_range_known = false;
        effect->write_range_unknown = true;
    }
}

static const MsSummary *call_summary(const MsSummarySet *set,
                                     const IrInst *call)
{
    if (!set || !call || call->op != IR_CALL)
        return NULL;
    if (call->subop == FUNCREF_INTERNAL && call->callee < set->module->nfuncs)
        return &set->functions[call->callee];
    if (call->subop == FUNCREF_EXTERNAL && call->callee < set->module->nsyms &&
        set->externals[call->callee].name)
        return &set->externals[call->callee];
    return NULL;
}

static bool returned_call_proves_ownership(const MsSummarySet *set,
                                           const IrInst *call)
{
    const MsSummary *callee;
    const MsLibSummary *lib = NULL;
    AliasAllocSeed seed;

    if (!call || call->op != IR_CALL)
        return false;
    callee = call_summary(set, call);
    if (callee)
        return callee->returns_ownership;
    if (call->subop == FUNCREF_EXTERNAL && call->callee < set->module->nsyms)
        lib = ms_lib_summary_lookup(set->module->syms[call->callee]);
    return (lib && lib->returns_ownership) ||
           (ms_alloc_seed_for_call(set->module, call, &seed) &&
            seed.owns_result);
}

static bool call_within_scc(const Callgraph *cg, u32 scc, const IrInst *call)
{
    return cg && scc != UINT32_MAX && call && call->op == IR_CALL &&
           call->subop == FUNCREF_INTERNAL &&
           ipo_callgraph_scc_of(cg, call->callee) == scc;
}

static void collect_alias_seeds(Arena *arena, IrModule *module, IrFunc *func,
                                const MsSummarySet *set, const Callgraph *cg,
                                u32 active_scc, AliasAllocSeed **alloc_out,
                                u32 *nalloc_out, AliasReturnSeed **return_out,
                                u32 *nreturn_out)
{
    u32 bi, na = 0, nr = 0, cap = 0, return_cap = 0;
    AliasAllocSeed *allocs;
    AliasReturnSeed *returns;

    for (bi = 0; bi < func->nblocks; bi++) {
        const IrInst *in;
        for (in = func->blocks[bi].first; in; in = in->next) {
            const MsSummary *s = call_summary(set, in);
            const MsLibSummary *lib = NULL;
            u32 p;

            cap++;
            if (call_within_scc(cg, active_scc, in))
                continue;
            if (in->op == IR_CALL && in->subop == FUNCREF_EXTERNAL &&
                in->callee < module->nsyms)
                lib = ms_lib_summary_lookup(module->syms[in->callee]);
            if (s) {
                for (p = 0; p < s->nparams; p++)
                    if (!s->annot_returns_owned &&
                        s->params[p].returned_alias &&
                        (!s->top || s->params[p].annot_returns_borrowed))
                        return_cap++;
            } else if (lib && lib->return_alias >= 0) {
                return_cap++;
            }
        }
    }
    allocs = arena_alloc(arena, (cap ? cap : 1) * sizeof(*allocs),
                         _Alignof(AliasAllocSeed));
    returns =
        arena_alloc(arena, (return_cap ? return_cap : 1) * sizeof(*returns),
                    _Alignof(AliasReturnSeed));
    for (bi = 0; bi < func->nblocks; bi++) {
        const IrInst *in;
        for (in = func->blocks[bi].first; in; in = in->next) {
            const MsSummary *s = call_summary(set, in);
            const MsLibSummary *lib = NULL;

            if (call_within_scc(cg, active_scc, in))
                continue;
            if (in->op == IR_CALL && in->subop == FUNCREF_EXTERNAL &&
                in->callee < module->nsyms)
                lib = ms_lib_summary_lookup(module->syms[in->callee]);
            if (ms_alloc_seed_for_call(module, in, &allocs[na]))
                na++;
            else if (s && s->returns_ownership &&
                     (!s->top || s->annot_returns_owned))
                allocs[na++] = (AliasAllocSeed){in, true, ALIAS_NO_OUT_PARAM};
            if (s) {
                u32 p;
                for (p = 0; p < s->nparams; p++)
                    if (!s->annot_returns_owned &&
                        s->params[p].returned_alias &&
                        (!s->top || s->params[p].annot_returns_borrowed))
                        returns[nr++] = (AliasReturnSeed){in, p};
            } else if (lib && lib->return_alias >= 0) {
                returns[nr++] = (AliasReturnSeed){in, (u32)lib->return_alias};
            }
        }
    }
    *alloc_out = allocs;
    *nalloc_out = na;
    *return_out = returns;
    *nreturn_out = nr;
}

static void infer_call(AliasCtx *alias, IrFunc *func, MsSummary *summary,
                       const MsSummarySet *set, const Callgraph *cg,
                       u32 active_scc, const IrInst *call)
{
    const MsSummary *callee = call_summary(set, call);
    const MsLibSummary *lib = NULL;
    const MsAllocFamily *family = NULL;
    Span loc = ir_inst_span(set->module, call);
    u64 lib_extent = 0;
    bool lib_extent_known;
    u32 first = call->subop == FUNCREF_INDIRECT ? 1 : 0;
    u32 ai;

    if (call_within_scc(cg, active_scc, call))
        return;
    if (call->subop == FUNCREF_EXTERNAL && call->callee < set->module->nsyms) {
        lib = ms_lib_summary_lookup(set->module->syms[call->callee]);
        family = ms_alloc_family_lookup(set->module->syms[call->callee]);
    }
    if (call->subop == FUNCREF_INDIRECT ||
        (!callee && !lib && !(family && family->allocates))) {
        summary->top = true;
        return;
    }
    lib_extent_known = lib_write_extent(func, lib, call, &lib_extent);
    for (ai = first; ai < call->nops; ai++) {
        u32 arg = ai - first;
        u64 d = 0, w = 0, e = 0, f = 0;

        if (callee && (!callee->top || callee->partial) &&
            arg < callee->nparams) {
            d = callee->params[arg].dereferenced;
            w = callee->params[arg].written;
            e = callee->params[arg].escapes;
            f = callee->params[arg].may_free;
            if (callee->partial) {
                d = 1;
                w = 1;
                e = !callee->params[arg].annot_no_escape &&
                    !callee->params[arg].annot_borrow;
            }
        } else if (lib && arg < 64) {
            u64 bit = 1ull << arg;
            d = lib->deref_mask & bit;
            w = lib->write_mask & bit;
            e = lib->escape_mask & bit;
            f = lib->free_mask & bit;
            if (arg >= ms_lib_summary_variadic_from(lib))
                d = w = e = 1;
        } else if (callee && callee->top) {
            d = w = e = 1;
            if (arg < callee->nparams) {
                e = !callee->params[arg].annot_no_escape &&
                    !callee->params[arg].annot_borrow;
                f = !callee->params[arg].annot_borrow;
            }
        }
        mark_operand_params(alias, summary, call->ops[ai], d, w, e, f, false,
                            loc);
        if (w && callee && arg < callee->nparams &&
            callee->params[arg].write_range_known)
            mark_write_subrange(alias, summary, call->ops[ai],
                                callee->params[arg].write_lo,
                                callee->params[arg].write_hi);
        else if (w && lib && lib_extent_known)
            mark_write_range(alias, summary, call->ops[ai], lib_extent);
        else if (w)
            mark_unknown_write(alias, summary, call->ops[ai]);
    }
}

static void infer_function(Arena *arena, MsSummarySet *set, u32 fi,
                           bool no_strict_aliasing, WarnCtx *warnings,
                           const Callgraph *cg, u32 active_scc)
{
    IrFunc *func = &set->module->funcs[fi];
    MsSummary *summary = &set->functions[fi];
    AliasAllocSeed *allocs;
    AliasReturnSeed *returns;
    AliasConfig cfg = {0};
    AliasCtx *alias;
    u32 na, nr, bi;

    collect_alias_seeds(arena, set->module, func, set, cg, active_scc, &allocs,
                        &na, &returns, &nr);
    cfg.func = func;
    cfg.no_strict_aliasing = no_strict_aliasing;
    cfg.alloc_seeds = allocs;
    cfg.nalloc_seeds = na;
    cfg.return_seeds = returns;
    cfg.nreturn_seeds = nr;
    cfg.track_param_origins = true;
    alias = alias_build(set->module, &cfg);
    for (bi = 0; bi < func->nblocks; bi++) {
        const IrInst *in;
        for (in = func->blocks[bi].first; in; in = in->next) {
            Span loc = ir_inst_span(set->module, in);
            u32 p;
            switch ((IrOp)in->op) {
            case IR_LOAD:
                if (in->nops)
                    mark_operand_params(alias, summary, in->ops[0], 1, 0, 0, 0,
                                        false, loc);
                break;
            case IR_STORE:
                if (in->nops >= 2) {
                    mark_operand_params(alias, summary, in->ops[1], 1, 1, 0, 0,
                                        false, loc);
                    mark_write_range(alias, summary, in->ops[1],
                                     ir_type_size((IrType)in->ops[0].type));
                    if (in->ops[0].type == IRT_PTR &&
                        !summary_local_address(func, in->ops[1], 0))
                        mark_operand_params(alias, summary, in->ops[0], 0, 0, 1,
                                            0, false, loc);
                }
                break;
            case IR_MEMCPY:
                if (in->nops >= 2) {
                    i64 size;

                    mark_operand_params(alias, summary, in->ops[0], 1, 1, 0, 0,
                                        false, loc);
                    mark_operand_params(alias, summary, in->ops[1], 1, 0, 0, 0,
                                        false, loc);
                    if (in->nops >= 3 &&
                        summary_operand_constant(func, in->ops[2], &size, 0) &&
                        size >= 0)
                        mark_write_range(alias, summary, in->ops[0], (u64)size);
                    else
                        mark_unknown_write(alias, summary, in->ops[0]);
                }
                break;
            case IR_MEMSET:
                if (in->nops) {
                    i64 size;

                    mark_operand_params(alias, summary, in->ops[0], 1, 1, 0, 0,
                                        false, loc);
                    if (in->nops >= 3 &&
                        summary_operand_constant(func, in->ops[2], &size, 0) &&
                        size >= 0)
                        mark_write_range(alias, summary, in->ops[0], (u64)size);
                    else
                        mark_unknown_write(alias, summary, in->ops[0]);
                }
                break;
            case IR_CALL:
                infer_call(alias, func, summary, set, cg, active_scc, in);
                break;
            case IR_RET:
                if (in->nops == 1 && in->ops[0].type == IRT_PTR) {
                    PtsSet pts = alias_points_to(alias, in->ops[0]);
                    const IrInst *def = summary_value_def(func, in->ops[0]);
                    bool returns_allocation =
                        alias_pts_unique_alloc_site(alias, pts) != NULL ||
                        returned_call_proves_ownership(set, def);

                    if (returns_allocation) {
                        summary->returns_ownership = true;
                        if (!span_present(summary->returns_ownership_span))
                            summary->returns_ownership_span = loc;
                    }
                    for (p = 0; p < summary->nparams; p++)
                        if (alias_pts_has_param(alias, pts, p)) {
                            summary->params[p].returned_alias = true;
                            if (!span_present(
                                    summary->params[p].return_alias_span))
                                summary->params[p].return_alias_span = loc;
                        }
                }
                break;
            default:
                break;
            }
        }
    }
    apply_contract(summary, func->cgf_attrs, warnings, true,
                   func->loc && func->loc <= set->module->nlocs
                       ? set->module->locs[func->loc - 1]
                       : (Span){0});
    alias_free(alias);
}

static bool recursive_scc(const Callgraph *cg, u32 scc)
{
    u32 fi, i;

    if (ipo_callgraph_scc_size(cg, scc) > 1)
        return true;
    fi = ipo_callgraph_scc_member(cg, scc, 0);
    for (i = 0; i < ipo_callgraph_edge_count(cg, fi); i++)
        if (ipo_callgraph_edge(cg, fi, i) == fi)
            return true;
    return false;
}

MsSummarySet *ms_summary_build(Arena *arena, IrModule *module,
                               bool no_strict_aliasing, WarnCtx *warnings)
{
    MsSummarySet *set;
    Callgraph *cg;
    u32 i;

    if (!arena || !module)
        return NULL;
    set = arena_alloc(arena, sizeof(*set), _Alignof(MsSummarySet));
    set->module = module;
    set->functions = arena_alloc(
        arena, (module->nfuncs ? module->nfuncs : 1) * sizeof(*set->functions),
        _Alignof(MsSummary));
    memset(set->functions, 0, module->nfuncs * sizeof(*set->functions));
    set->externals = arena_alloc(
        arena, (module->nsyms ? module->nsyms : 1) * sizeof(*set->externals),
        _Alignof(MsSummary));
    memset(set->externals, 0, module->nsyms * sizeof(*set->externals));
    for (i = 0; i < module->nfuncs; i++) {
        MsSummary *s = &set->functions[i];
        s->name = module->funcs[i].name;
        s->nparams = module->funcs[i].nparams;
        s->params = arena_alloc(
            arena, (s->nparams ? s->nparams : 1) * sizeof(*s->params),
            _Alignof(MsParamSummary));
        memset(s->params, 0, s->nparams * sizeof(*s->params));
    }
    for (i = 0; i < module->nsyms; i++) {
        const CgfAttr *attrs =
            module->sym_cgf_attrs ? module->sym_cgf_attrs[i] : NULL;
        const CgfAttr *a;
        u32 maxp = 0;
        MsSummary *s;
        if (!attrs)
            continue;
        for (a = attrs; a; a = a->next) {
            u32 p = attr_param(a);
            if (p != UINT32_MAX && p + 1 > maxp)
                maxp = p + 1;
        }
        s = &set->externals[i];
        s->name = module->syms[i];
        s->partial = true;
        s->nparams = maxp;
        s->params = arena_alloc(arena, (maxp ? maxp : 1) * sizeof(*s->params),
                                _Alignof(MsParamSummary));
        memset(s->params, 0, maxp * sizeof(*s->params));
        apply_contract(s, attrs, NULL, false, (Span){0});
    }
    cg = ipo_callgraph_build(module);
    for (i = 0; i < ipo_callgraph_scc_count(cg); i++) {
        u32 scc = ipo_callgraph_bottom_up_scc(cg, i);
        u32 mi;
        if (recursive_scc(cg, scc)) {
            for (mi = 0; mi < ipo_callgraph_scc_size(cg, scc); mi++) {
                u32 fi = ipo_callgraph_scc_member(cg, scc, mi);
                set->functions[fi].top = true;
                infer_function(arena, set, fi, no_strict_aliasing, warnings, cg,
                               scc);
            }
            continue;
        }
        infer_function(arena, set, ipo_callgraph_scc_member(cg, scc, 0),
                       no_strict_aliasing, warnings, cg, UINT32_MAX);
    }
    ipo_callgraph_free(cg);
    return set;
}

const MsSummary *ms_summary_get(const MsSummarySet *set, u32 function)
{
    return set && function < set->module->nfuncs ? &set->functions[function]
                                                 : NULL;
}

const MsSummary *ms_summary_for_call(const MsSummarySet *set,
                                     const IrInst *call)
{
    return set && call && call->op == IR_CALL ? call_summary(set, call) : NULL;
}

void ms_summary_dump(const MsSummarySet *set, FILE *out)
{
    u32 fi;

    if (!set || !out)
        return;
    for (fi = 0; fi < set->module->nfuncs; fi++) {
        const MsSummary *s = &set->functions[fi];
        u32 p;
        fprintf(out, "summary function=%s top=%s returns-owned=%s\n", s->name,
                s->top ? "yes" : "no", s->returns_ownership ? "yes" : "no");
        for (p = 0; p < s->nparams; p++) {
            const MsParamSummary *e = &s->params[p];
            fprintf(out,
                    "summary-param function=%s param=%u deref=%s write=%s "
                    "escape=%s free=%s return-alias=%s\n",
                    s->name, p + 1, e->dereferenced ? "yes" : "no",
                    e->written ? "yes" : "no", e->escapes ? "yes" : "no",
                    e->must_free  ? "must"
                    : e->may_free ? "may"
                                  : "no",
                    e->returned_alias ? "yes" : "no");
        }
    }
}
