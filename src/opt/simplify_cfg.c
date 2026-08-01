#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

struct OptCfgInfo {
    CfgRemoved *removed;
    u32 nremoved;
    u32 cap_removed;
};

const Pass OPT_PASS_SIMPLIFY_CFG = {"simplify_cfg", opt_simplify_cfg,
                                    PASS_PINNED_EXACT};

const CfgRemoved *opt_cfg_removed_log(const IrFunc *f, u32 *n)
{
    const struct OptCfgInfo *info = f ? f->opt_cfg_info : NULL;

    if (n)
        *n = info ? info->nremoved : 0;
    return info ? info->removed : NULL;
}

static void *arena_grow(Arena *arena, const void *old, u32 oldn, u32 newn,
                        size_t size, size_t align)
{
    void *p = arena_alloc(arena, newn * size, align);

    if (oldn)
        memcpy(p, old, oldn * size);
    return p;
}

static void log_removed(IrModule *m, IrFunc *f, BlockId block)
{
    struct OptCfgInfo *info = f->opt_cfg_info;
    const IrBlock *blk = ir_block(f, block);
    const IrInst *in;
    Span loc = {0};

    if (!info) {
        info =
            arena_alloc(m->arena, sizeof(*info), _Alignof(struct OptCfgInfo));
        memset(info, 0, sizeof(*info));
        f->opt_cfg_info = info;
    }
    if (info->nremoved == info->cap_removed) {
        u32 cap = info->cap_removed ? info->cap_removed * 2 : 8;

        info->removed =
            arena_grow(m->arena, info->removed, info->nremoved, cap,
                       sizeof(*info->removed), _Alignof(CfgRemoved));
        info->cap_removed = cap;
    }
    for (in = blk ? blk->first : NULL; in; in = in->next)
        if (in->loc) {
            loc = ir_inst_span(m, in);
            break;
        }
    if (!loc.file_id)
        loc = ir_debug_loc(m, f->loc);
    info->removed[info->nremoved].block = block;
    info->removed[info->nremoved].loc = loc;
    info->nremoved++;
}

static u64 int_mask(IrType type)
{
    switch (type) {
    case IRT_I8:
        return 0xff;
    case IRT_I16:
        return 0xffff;
    case IRT_I32:
        return 0xffffffffu;
    case IRT_I64:
    case IRT_PTR:
        return ~(u64)0;
    default:
        return 0;
    }
}

static bool const_nonzero(IrOperand op)
{
    return op.kind == IROP_ICONST && (op.a & int_mask((IrType)op.type)) != 0;
}

static bool const_is_case(IrOperand op, i64 case_val)
{
    u64 mask = int_mask((IrType)op.type);

    return op.kind == IROP_ICONST && mask &&
           (op.a & mask) == ((u64)case_val & mask);
}

static void make_br(IrInst *term, IrEdge edge)
{
    IrEdge *dst = term->edges;

    /* Every condbr/switch already owns at least one arena edge slot. */
    dst[0] = edge;
    dst[0].case_val = 0;
    term->op = IR_BR;
    term->type = IRT_VOID;
    term->subop = 0;
    term->flags = 0;
    term->nops = 0;
    term->ops = NULL;
    term->nedges = 1;
}

static bool fold_const_edges(IrFunc *f)
{
    bool changed = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *term = f->blocks[bi].last;

        if (!term || term->nops != 1 || term->ops[0].kind != IROP_ICONST)
            continue;
        if (term->op == IR_CONDBR && term->nedges == 2) {
            make_br(term, term->edges[const_nonzero(term->ops[0]) ? 0 : 1]);
            changed = true;
        } else if (term->op == IR_SWITCH && term->nedges) {
            u32 ei;
            u32 chosen = 0;

            for (ei = 1; ei < term->nedges; ei++)
                if (const_is_case(term->ops[0], term->edges[ei].case_val)) {
                    chosen = ei;
                    break;
                }
            make_br(term, term->edges[chosen]);
            changed = true;
        }
    }
    return changed;
}

static bool *find_reachable(Arena *arena, const IrFunc *f)
{
    bool *reachable =
        arena_alloc(arena, f->nblocks * sizeof(*reachable), _Alignof(bool));
    u32 *work = arena_alloc(arena, f->nblocks * sizeof(*work), _Alignof(u32));
    u32 nwork = 0;

    memset(reachable, 0, f->nblocks * sizeof(*reachable));
    if (!f->nblocks)
        return reachable;
    reachable[0] = true;
    work[nwork++] = 0;
    while (nwork) {
        u32 bi = work[--nwork];
        const IrInst *term = f->blocks[bi].last;
        u32 ei;

        if (!term)
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;

            if (target && target <= f->nblocks && !reachable[target - 1]) {
                reachable[target - 1] = true;
                work[nwork++] = target - 1;
            }
        }
    }
    return reachable;
}

static bool remove_semantic_unreachable(IrModule *m, IrFunc *f)
{
    Arena scratch;
    bool *reachable;
    bool changed = false;
    u32 bi;

    arena_init(&scratch);
    reachable = find_reachable(&scratch, f);
    for (bi = 0; bi < f->nblocks; bi++)
        if (!reachable[bi]) {
            log_removed(m, f, (BlockId){bi + 1});
            changed = true;
        }
    if (changed)
        ir_func_remove_unreachable(f);
    arena_free_all(&scratch);
    return changed;
}

static void predecessor_counts(const IrFunc *f, const bool *dead, u32 *preds)
{
    u32 bi;

    memset(preds, 0, f->nblocks * sizeof(*preds));
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *term;
        u32 ei;

        if (dead && dead[bi])
            continue;
        term = f->blocks[bi].last;
        if (!term)
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;

            if (target && target <= f->nblocks && (!dead || !dead[target - 1]))
                preds[target - 1]++;
        }
    }
}

static bool speculation_safe(IrOp op)
{
    switch (op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_ICMP:
    case IR_FCMP:
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FNEG:
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
    case IR_FPEXT:
    case IR_FPTRUNC:
    case IR_SITOFP:
    case IR_UITOFP:
    case IR_BITCAST:
    case IR_PTRADD:
    case IR_SELECT:
        return true;
    default:
        return false;
    }
}

static bool arm_is_safe(const IrBlock *arm)
{
    const IrInst *in;

    if (arm->nparams || !arm->last || arm->last->op != IR_BR ||
        arm->last->nedges != 1 || arm->last->nops)
        return false;
    for (in = arm->first; in && in != arm->last; in = in->next)
        if (!in->result.v || !speculation_safe((IrOp)in->op))
            return false;
    return true;
}

static ValueId new_value(IrModule *m, IrFunc *f, IrType type, BlockId block)
{
    ValueId value;

    if (f->nvals == f->cap_vals) {
        u32 cap = f->cap_vals ? f->cap_vals * 2 : 16;

        f->vals = arena_grow(m->arena, f->vals, f->nvals, cap, sizeof(*f->vals),
                             _Alignof(IrValInfo));
        f->cap_vals = cap;
    }
    memset(&f->vals[f->nvals], 0, sizeof(*f->vals));
    f->vals[f->nvals].type = (u8)type;
    f->vals[f->nvals].def_kind = VDEF_INST;
    f->vals[f->nvals].def_block = block;
    value.v = ++f->nvals;
    return value;
}

static IrInst *new_select(IrModule *m, IrFunc *f, BlockId block, u32 loc,
                          IrOperand cond, IrOperand yes, IrOperand no)
{
    IrInst *select = arena_alloc(m->arena, sizeof(*select), _Alignof(IrInst));

    memset(select, 0, sizeof(*select));
    select->op = IR_SELECT;
    select->type = yes.type;
    select->result = new_value(m, f, (IrType)yes.type, block);
    select->loc = loc;
    select->nops = 3;
    select->ops =
        arena_alloc(m->arena, 3 * sizeof(*select->ops), _Alignof(IrOperand));
    select->ops[0] = cond;
    select->ops[1] = yes;
    select->ops[2] = no;
    return select;
}

static IrInst *before_last(IrBlock *block)
{
    IrInst *in;

    if (block->first == block->last)
        return NULL;
    for (in = block->first; in && in->next != block->last; in = in->next)
        ;
    return in;
}

static bool collapse_diamonds(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    u32 *preds;
    bool changed = false;
    u32 bi;

    arena_init(&scratch);
    preds = arena_alloc(&scratch, f->nblocks * sizeof(*preds), _Alignof(u32));
    predecessor_counts(f, NULL, preds);
    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *header = &f->blocks[bi];
        IrInst *term = header->last;
        IrEdge *te, *fe;
        IrBlock *tb, *fb, *join;
        IrInst *tt, *ft, *select, *tail;
        IrOperand tv, fv;
        u32 ti, fi, ji;

        if (!term || term->op != IR_CONDBR || term->nops != 1 ||
            term->nedges != 2)
            continue;
        te = &term->edges[0];
        fe = &term->edges[1];
        ti = te->target.v;
        fi = fe->target.v;
        if (!ti || !fi || ti > f->nblocks || fi > f->nblocks || ti == fi ||
            te->nargs || fe->nargs || preds[ti - 1] != 1 || preds[fi - 1] != 1)
            continue;
        tb = &f->blocks[ti - 1];
        fb = &f->blocks[fi - 1];
        tt = tb->last;
        ft = fb->last;
        if (!tt || !ft || tt->op != IR_BR || ft->op != IR_BR ||
            tt->nedges != 1 || ft->nedges != 1 ||
            tt->edges[0].target.v != ft->edges[0].target.v)
            continue;
        ji = tt->edges[0].target.v;
        if (!ji || ji > f->nblocks || ji == ti || ji == fi ||
            tt->edges[0].nargs != 1 || ft->edges[0].nargs != 1 ||
            f->blocks[ji - 1].nparams != 1)
            continue;
        tv = tt->edges[0].args[0];
        fv = ft->edges[0].args[0];
        join = &f->blocks[ji - 1];
        if (tv.type != fv.type ||
            ir_value_type(f, join->params[0]) != (IrType)tv.type ||
            !arm_is_safe(tb) || !arm_is_safe(fb)) {
            OPT_BAIL(cfg, "simplify_cfg", "cfg_select_unspeculatable");
            continue;
        }

        tail = before_last(header);
        if (!tail)
            tail = term;
        else
            tail->next = NULL;
        if (tail == term) {
            /* The terminator-only header has no body to terminate yet. */
            header->first = NULL;
            tail = NULL;
        }
        if (before_last(tb)) {
            if (tail)
                tail->next = tb->first;
            else
                header->first = tb->first;
            tail = before_last(tb);
        }
        if (before_last(fb)) {
            if (tail)
                tail->next = fb->first;
            else
                header->first = fb->first;
            tail = before_last(fb);
        }
        select = new_select(m, f, (BlockId){bi + 1}, term->loc, term->ops[0],
                            tv, fv);
        if (tail)
            tail->next = select;
        else
            header->first = select;
        select->next = term;
        term->op = IR_BR;
        term->type = IRT_VOID;
        term->nops = 0;
        term->ops = NULL;
        term->nedges = 1;
        term->edges[0] = tt->edges[0];
        term->edges[0].case_val = 0;
        term->edges[0].nargs = 1;
        term->edges[0].args =
            arena_alloc(m->arena, sizeof(IrOperand), _Alignof(IrOperand));
        term->edges[0].args[0] = ir_op_value(f, select->result);
        header->last = term;
        header->ninsts =
            header->ninsts - 1 + (tb->ninsts - 1) + (fb->ninsts - 1) + 2;
        changed = true;
    }
    arena_free_all(&scratch);
    if (changed)
        ir_func_remove_unreachable(f);
    return changed;
}

static void replace_value(IrFunc *f, ValueId from, IrOperand to)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 oi, ei, ai;

            for (oi = 0; oi < in->nops; oi++)
                if (in->ops[oi].kind == IROP_VALUE && in->ops[oi].a == from.v)
                    in->ops[oi] = to;
            for (ei = 0; ei < in->nedges; ei++)
                for (ai = 0; ai < in->edges[ei].nargs; ai++)
                    if (in->edges[ei].args[ai].kind == IROP_VALUE &&
                        in->edges[ei].args[ai].a == from.v)
                        in->edges[ei].args[ai] = to;
        }
    }
}

static void splice_successor(IrFunc *f, u32 pred_index, u32 succ_index,
                             const IrEdge *edge)
{
    IrBlock *pred = &f->blocks[pred_index];
    IrBlock *succ = &f->blocks[succ_index];
    IrInst *before = before_last(pred);
    u32 i;

    for (i = 0; i < succ->nparams; i++)
        replace_value(f, succ->params[i], edge->args[i]);
    if (before)
        before->next = succ->first;
    else
        pred->first = succ->first;
    pred->last = succ->last;
    pred->ninsts = pred->ninsts - 1 + succ->ninsts;
}

static bool merge_straight_lines(IrFunc *f)
{
    Arena scratch;
    bool *dead;
    u32 *preds;
    bool changed = false;
    bool progress;

    arena_init(&scratch);
    dead = arena_alloc(&scratch, f->nblocks * sizeof(*dead), _Alignof(bool));
    preds = arena_alloc(&scratch, f->nblocks * sizeof(*preds), _Alignof(u32));
    memset(dead, 0, f->nblocks * sizeof(*dead));
    do {
        u32 bi;

        progress = false;
        predecessor_counts(f, dead, preds);
        for (bi = 0; bi < f->nblocks; bi++) {
            IrInst *term;
            IrEdge edge;
            u32 si;

            if (dead[bi])
                continue;
            term = f->blocks[bi].last;
            if (!term || term->op != IR_BR || term->nedges != 1)
                continue;
            edge = term->edges[0];
            if (edge.target.v <= 1 || edge.target.v > f->nblocks)
                continue;
            si = edge.target.v - 1;
            if (si == bi || dead[si] || preds[si] != 1 ||
                edge.nargs != f->blocks[si].nparams)
                continue;
            splice_successor(f, bi, si, &edge);
            dead[si] = true;
            changed = progress = true;
            break;
        }
    } while (progress);
    if (changed)
        ir_func_remove_unreachable(f);
    arena_free_all(&scratch);
    return changed;
}

static bool simplify_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    OptConfig local = *cfg;
    bool changed = false;

    local.current_func = f->name;
    if (fold_const_edges(f))
        changed = true;
    if (remove_semantic_unreachable(m, f))
        changed = true;
    if (collapse_diamonds(m, f, &local))
        changed = true;
    if (merge_straight_lines(f))
        changed = true;
    if (changed)
        ir_func_renumber(m->arena, f);
    return changed;
}

bool opt_simplify_cfg(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 fi;

    if (!m || !cfg)
        return false;
    for (fi = 0; fi < m->nfuncs; fi++)
        if (simplify_func(m, &m->funcs[fi], cfg))
            changed = true;
    return changed;
}
