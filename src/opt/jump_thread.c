#include "opt/opt.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "util/arena.h"

#define JT_MAX_BLOCK_INSTS 12u
#define JT_NONE UINT32_MAX

typedef struct {
    const IrFunc *f;
    u32 pred;
    u32 pred_edge;
    u32 clone;
    u32 clone_succ;
} VirtualCfg;

static bool operand_eq(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static bool cloneable_op(IrOp op)
{
    switch (op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
    case IR_ICMP:
    case IR_FCMP:
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
    case IR_FNEG:
    case IR_SEXT:
    case IR_ZEXT:
    case IR_TRUNC:
    case IR_FPEXT:
    case IR_FPTRUNC:
    case IR_FPTOSI:
    case IR_FPTOUI:
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

static u32 func_inst_count(const IrFunc *f)
{
    u32 bi;
    u32 n = 0;

    for (bi = 0; bi < f->nblocks; bi++)
        n += f->blocks[bi].ninsts;
    return n;
}

static bool block_cloneable(const IrBlock *blk)
{
    const IrInst *in;
    u32 n = 1; /* the cloned condbr becomes a branch, but still costs one */

    if (!blk->last || blk->last->op != IR_CONDBR || blk->last->nops != 1 ||
        blk->last->nedges != 2)
        return false;
    for (in = blk->first; in && in != blk->last; in = in->next) {
        if (++n > JT_MAX_BLOCK_INSTS || !cloneable_op((IrOp)in->op))
            return false;
    }
    return true;
}

static u32 virtual_succ_count(const VirtualCfg *g, u32 block)
{
    const IrInst *in;
    u32 n = 0;

    if (block == g->clone)
        return 1;
    for (in = g->f->blocks[block].first; in; in = in->next)
        n += in->nedges;
    return n;
}

static u32 virtual_succ_at(const VirtualCfg *g, u32 block, u32 pos)
{
    const IrInst *in;
    u32 at = 0;

    if (block == g->clone)
        return pos == 0 ? g->clone_succ : JT_NONE;
    for (in = g->f->blocks[block].first; in; in = in->next) {
        u32 ei;

        for (ei = 0; ei < in->nedges; ei++, at++) {
            if (at != pos)
                continue;
            if (block == g->pred && in == g->f->blocks[block].last &&
                ei == g->pred_edge)
                return g->clone;
            if (in->edges[ei].target.v >= 1 &&
                in->edges[ei].target.v <= g->f->nblocks)
                return in->edges[ei].target.v - 1;
            return JT_NONE;
        }
    }
    return JT_NONE;
}

static u32 intersect(const u32 *idom, const u32 *rpo, u32 a, u32 b)
{
    while (a != b) {
        while (rpo[a] > rpo[b])
            a = idom[a];
        while (rpo[b] > rpo[a])
            b = idom[b];
    }
    return a;
}

static bool idom_dominates(const u32 *idom, u32 a, u32 b)
{
    if (a == b)
        return true;
    while (idom[b] != JT_NONE) {
        b = idom[b];
        if (a == b)
            return true;
    }
    return false;
}

/* Validate the candidate graph without mutating the IR.  The DFS order is
 * deterministic (block layout, then edge order); every retreating edge must
 * be a natural-loop backedge whose target dominates its source. */
static bool virtual_reducible(const VirtualCfg *g)
{
    Arena scratch;
    u32 n = g->f->nblocks + 1;
    u32 *post, *stack, *next_edge, *rpo, *idom;
    u8 *color;
    u32 npost = 0, sp = 0, i;
    bool changed;
    bool ok = true;

    arena_init(&scratch);
    post = arena_alloc(&scratch, n * sizeof(*post), _Alignof(u32));
    stack = arena_alloc(&scratch, n * sizeof(*stack), _Alignof(u32));
    next_edge = arena_alloc(&scratch, n * sizeof(*next_edge), _Alignof(u32));
    rpo = arena_alloc(&scratch, n * sizeof(*rpo), _Alignof(u32));
    idom = arena_alloc(&scratch, n * sizeof(*idom), _Alignof(u32));
    color = arena_alloc(&scratch, n * sizeof(*color), _Alignof(u8));
    memset(color, 0, n * sizeof(*color));
    for (i = 0; i < n; i++) {
        rpo[i] = JT_NONE;
        idom[i] = JT_NONE;
    }

    stack[sp] = 0;
    next_edge[sp] = 0;
    color[0] = 1;
    sp++;
    while (sp) {
        u32 b = stack[sp - 1];
        u32 pos = next_edge[sp - 1];

        if (pos >= virtual_succ_count(g, b)) {
            color[b] = 2;
            post[npost++] = b;
            sp--;
            continue;
        }
        next_edge[sp - 1]++;
        i = virtual_succ_at(g, b, pos);
        if (i == JT_NONE || color[i])
            continue;
        color[i] = 1;
        stack[sp] = i;
        next_edge[sp] = 0;
        sp++;
    }
    for (i = 0; i < npost; i++)
        rpo[post[i]] = npost - 1 - i;

    idom[0] = 0;
    do {
        changed = false;
        for (i = npost; i-- > 0;) {
            u32 b = post[i];
            u32 new_idom = JT_NONE;
            u32 p;

            if (b == 0)
                continue;
            for (p = 0; p < n; p++) {
                u32 ei;

                if (rpo[p] == JT_NONE || idom[p] == JT_NONE)
                    continue;
                for (ei = 0; ei < virtual_succ_count(g, p); ei++) {
                    if (virtual_succ_at(g, p, ei) != b)
                        continue;
                    new_idom = new_idom == JT_NONE
                                   ? p
                                   : intersect(idom, rpo, new_idom, p);
                    break;
                }
            }
            if (new_idom != JT_NONE && idom[b] != new_idom) {
                idom[b] = new_idom;
                changed = true;
            }
        }
    } while (changed);
    idom[0] = JT_NONE;

    memset(color, 0, n * sizeof(*color));
    sp = 0;
    stack[sp] = 0;
    next_edge[sp] = 0;
    color[0] = 1;
    sp++;
    while (sp && ok) {
        u32 b = stack[sp - 1];
        u32 pos = next_edge[sp - 1];

        if (pos >= virtual_succ_count(g, b)) {
            color[b] = 2;
            sp--;
            continue;
        }
        next_edge[sp - 1]++;
        i = virtual_succ_at(g, b, pos);
        if (i == JT_NONE)
            continue;
        if (color[i] == 1) {
            if (!idom_dominates(idom, i, b))
                ok = false;
        } else if (color[i] == 0) {
            color[i] = 1;
            stack[sp] = i;
            next_edge[sp] = 0;
            sp++;
        }
    }
    arena_free_all(&scratch);
    return ok;
}

static void rewrite_operand(IrOperand *op, const u32 *map, u32 nmap)
{
    if (op->kind == IROP_VALUE && op->a < nmap && map[op->a])
        op->a = map[op->a];
}

static ValueId new_inst_value(IrModule *m, IrFunc *f, IrType type,
                              BlockId block, u32 pos)
{
    IrValInfo *nv;
    ValueId result;

    if (f->nvals == f->cap_vals) {
        u32 cap = f->cap_vals ? f->cap_vals * 2 : 16;

        nv = arena_alloc(m->arena, cap * sizeof(*nv), _Alignof(IrValInfo));
        if (f->nvals)
            memcpy(nv, f->vals, f->nvals * sizeof(*nv));
        f->vals = nv;
        f->cap_vals = cap;
    }
    memset(&f->vals[f->nvals], 0, sizeof(f->vals[f->nvals]));
    f->vals[f->nvals].type = (u8)type;
    f->vals[f->nvals].def_kind = VDEF_INST;
    f->vals[f->nvals].def_block = block;
    f->vals[f->nvals].def_pos = pos;
    result.v = ++f->nvals;
    return result;
}

static IrOperand *copy_operands(IrModule *m, const IrOperand *src, u32 n,
                                const u32 *map, u32 nmap)
{
    IrOperand *dst;
    u32 i;

    if (!n)
        return NULL;
    dst = arena_alloc(m->arena, n * sizeof(*dst), _Alignof(IrOperand));
    memcpy(dst, src, n * sizeof(*dst));
    for (i = 0; i < n; i++)
        rewrite_operand(&dst[i], map, nmap);
    return dst;
}

static void append_inst(IrBlock *blk, IrInst *in)
{
    if (blk->last)
        blk->last->next = in;
    else
        blk->first = in;
    blk->last = in;
    blk->ninsts++;
}

static bool block_name_exists(const IrFunc *f, const char *name)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++)
        if (f->blocks[bi].name && strcmp(f->blocks[bi].name, name) == 0)
            return true;
    return false;
}

static const char *fresh_clone_name(IrModule *m, const IrFunc *f, u32 *serial)
{
    char name[64];

    if (f->nblocks == UINT32_MAX)
        CGF_ICE("jump_thread: exhausted block id space");
    for (;;) {
        if (*serial == UINT32_MAX)
            CGF_ICE("jump_thread: exhausted clone label namespace");
        int n =
            snprintf(name, sizeof(name), "jt.%u.%u", f->nblocks + 1, *serial);

        if (n < 0 || (size_t)n >= sizeof(name))
            CGF_ICE("jump_thread: clone label formatting overflow");
        if (!block_name_exists(f, name)) {
            (*serial)++;
            return arena_strdup(m->arena, name);
        }
        (*serial)++;
    }
}

static BlockId commit_clone(IrModule *m, IrFunc *f, u32 source, u32 chosen_edge,
                            u32 *serial)
{
    const IrBlock *src;
    const IrInst *old;
    IrBlock *dst;
    IrInst *copy;
    BlockId block;
    u32 old_nvals = f->nvals;
    u32 *map =
        arena_alloc(m->arena, (old_nvals + 1) * sizeof(*map), _Alignof(u32));
    u32 i, pos = 0;

    memset(map, 0, (old_nvals + 1) * sizeof(*map));
    src = &f->blocks[source];
    block = ir_block_new(m, f, fresh_clone_name(m, f, serial));
    src = &f->blocks[source];
    for (i = 0; i < src->nparams; i++) {
        ValueId v =
            ir_block_param(m, f, block, ir_value_type(f, src->params[i]));

        map[src->params[i].v] = v.v;
    }
    src = &f->blocks[source];
    dst = ir_block(f, block);
    for (old = src->first; old && old != src->last; old = old->next, pos++) {
        copy = arena_alloc(m->arena, sizeof(*copy), _Alignof(IrInst));
        memcpy(copy, old, sizeof(*copy));
        copy->next = NULL;
        copy->ops = copy_operands(m, old->ops, old->nops, map, old_nvals + 1);
        copy->edges = NULL;
        copy->nedges = 0;
        if (old->result.v) {
            copy->result = new_inst_value(m, f, (IrType)old->type, block, pos);
            map[old->result.v] = copy->result.v;
        }
        append_inst(dst, copy);
    }
    old = src->last;
    copy = arena_alloc(m->arena, sizeof(*copy), _Alignof(IrInst));
    memset(copy, 0, sizeof(*copy));
    copy->op = IR_BR;
    copy->type = IRT_VOID;
    copy->loc = old->loc;
    copy->nedges = 1;
    copy->edges = arena_alloc(m->arena, sizeof(*copy->edges), _Alignof(IrEdge));
    memset(copy->edges, 0, sizeof(*copy->edges));
    copy->edges[0] = old->edges[chosen_edge];
    copy->edges[0].args =
        copy_operands(m, old->edges[chosen_edge].args,
                      old->edges[chosen_edge].nargs, map, old_nvals + 1);
    append_inst(dst, copy);
    return block;
}

static void compute_reachable(const IrFunc *f, bool *reach, u32 *work)
{
    u32 nwork = 0;

    memset(reach, 0, f->nblocks * sizeof(*reach));
    if (!f->nblocks)
        return;
    reach[0] = true;
    work[nwork++] = 0;
    while (nwork) {
        u32 b = work[--nwork];
        const IrInst *in;

        for (in = f->blocks[b].first; in; in = in->next) {
            u32 ei;

            for (ei = 0; ei < in->nedges; ei++) {
                u32 to = in->edges[ei].target.v;

                if (to && to <= f->nblocks && !reach[to - 1]) {
                    reach[to - 1] = true;
                    work[nwork++] = to - 1;
                }
            }
        }
    }
}

static bool jump_thread_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    OptConfig fc = *cfg;
    u32 original = func_inst_count(f);
    u32 growth = 0;
    u32 serial = 0;
    bool changed = false;

    fc.current_func = f->name;
    for (;;) {
        Arena scratch;
        bool *reach;
        u32 *work;
        u32 pred;
        bool committed = false;

        arena_init(&scratch);
        reach = arena_alloc(&scratch,
                            (f->nblocks ? f->nblocks : 1) * sizeof(*reach),
                            _Alignof(bool));
        work =
            arena_alloc(&scratch, (f->nblocks ? f->nblocks : 1) * sizeof(*work),
                        _Alignof(u32));
        compute_reachable(f, reach, work);
        for (pred = 0; pred < f->nblocks && !committed; pred++) {
            IrInst *first = f->blocks[pred].last;
            u32 ei;

            if (!reach[pred] || !first || first->op != IR_CONDBR ||
                first->nops != 1 || first->nedges != 2 ||
                first->ops[0].kind != IROP_VALUE)
                continue;
            for (ei = 0; ei < 2; ei++) {
                u32 target = first->edges[ei].target.v;
                IrBlock *mid;
                u32 cost;
                VirtualCfg virtual_cfg;
                BlockId clone;

                if (!target || target > f->nblocks || target - 1 == pred ||
                    !reach[target - 1])
                    continue;
                mid = &f->blocks[target - 1];
                if (!mid->last || mid->last->op != IR_CONDBR ||
                    mid->last->nops != 1 || mid->last->nedges != 2 ||
                    !operand_eq(first->ops[0], mid->last->ops[0]))
                    continue;
                if (!block_cloneable(mid)) {
                    OPT_BAIL(&fc, "jump_thread", "jt_block_not_cloneable");
                    continue;
                }
                cost = mid->ninsts;
                virtual_cfg.f = f;
                virtual_cfg.pred = pred;
                virtual_cfg.pred_edge = ei;
                virtual_cfg.clone = f->nblocks;
                virtual_cfg.clone_succ = mid->last->edges[ei].target.v - 1;
                if (!virtual_reducible(&virtual_cfg)) {
                    OPT_BAIL(&fc, "jump_thread", "jt_would_create_irreducible");
                    continue;
                }
                if (((u64)original + growth + cost) * 100ull >
                    (u64)original * 115ull) {
                    OPT_BAIL(&fc, "jump_thread", "jt_growth_cap");
                    continue;
                }
                clone = commit_clone(m, f, target - 1, ei, &serial);
                first->edges[ei].target = clone;
                growth += cost;
                changed = true;
                committed = true;
                break;
            }
        }
        arena_free_all(&scratch);
        if (!committed)
            break;
    }
    if (changed) {
        ir_func_remove_unreachable(f);
        ir_func_renumber(m->arena, f);
    }
    return changed;
}

bool opt_jump_thread(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= jump_thread_func(m, &m->funcs[i], cfg);
    return changed;
}

const Pass OPT_PASS_JUMP_THREAD = {"jump_thread", opt_jump_thread};
