#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

static bool dce_root(const IrInst *in)
{
    if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
        return true;
    switch ((IrOp)in->op) {
    case IR_STORE:
    case IR_MEMCPY:
    case IR_MEMSET:
    case IR_CALL:
    case IR_VA_START:
    case IR_STACKSAVE:
    case IR_STACKRESTORE:
    case IR_ATOMICRMW:
    case IR_CMPXCHG:
    case IR_RET:
    case IR_BR:
    case IR_CONDBR:
    case IR_SWITCH:
    case IR_UNREACHABLE:
        return true;
    default:
        return false;
    }
}

static void enqueue_operand(IrOperand op, bool *live, u32 nvals, u32 *work,
                            u32 *nwork)
{
    u32 v;

    if (op.kind != IROP_VALUE)
        return;
    v = (u32)op.a;
    if (v < 1 || v > nvals)
        CGF_ICE("dce: operand references invalid value %u", v);
    if (!live[v]) {
        live[v] = true;
        work[(*nwork)++] = v;
    }
}

static void enqueue_inst_uses(const IrInst *in, bool *live, u32 nvals,
                              u32 *work, u32 *nwork)
{
    u32 i, j;

    for (i = 0; i < in->nops; i++)
        enqueue_operand(in->ops[i], live, nvals, work, nwork);
    for (i = 0; i < in->nedges; i++)
        for (j = 0; j < in->edges[i].nargs; j++)
            enqueue_operand(in->edges[i].args[j], live, nvals, work, nwork);
}

static bool dce_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    OptConfig fc = *cfg;
    IrInst **defs;
    u32 *bp_block;
    u32 *bp_index;
    bool *live;
    u32 *work;
    u32 nwork = 0;
    bool call_bailed = false;
    bool changed = false;
    u32 bi;

    fc.current_func = f->name;
    arena_init(&scratch);
    defs = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*defs),
                       _Alignof(IrInst *));
    bp_block = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*bp_block),
                           _Alignof(u32));
    bp_index = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*bp_index),
                           _Alignof(u32));
    live = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*live), 1);
    work = arena_alloc(&scratch, (f->nvals + 1) * sizeof(*work), _Alignof(u32));
    memset(defs, 0, (f->nvals + 1) * sizeof(*defs));
    memset(bp_block, 0, (f->nvals + 1) * sizeof(*bp_block));
    memset(bp_index, 0, (f->nvals + 1) * sizeof(*bp_index));
    memset(live, 0, (f->nvals + 1) * sizeof(*live));

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];
        IrInst *in;
        u32 pi;

        for (pi = 0; pi < blk->nparams; pi++) {
            u32 v = blk->params[pi].v;

            if (v) {
                bp_block[v] = bi + 1;
                bp_index[v] = pi;
            }
        }
        for (in = blk->first; in; in = in->next) {
            if (in->result.v)
                defs[in->result.v] = in;
            if (!dce_root(in))
                continue;
            if (in->op == IR_CALL && !call_bailed) {
                OPT_BAIL(&fc, "dce", "dce_call_side_effects");
                call_bailed = true;
            }
            if (in->result.v)
                enqueue_operand(ir_op_value(f, in->result), live, f->nvals,
                                work, &nwork);
            else
                enqueue_inst_uses(in, live, f->nvals, work, &nwork);
        }
    }

    while (nwork) {
        u32 v = work[--nwork];
        IrInst *def = defs[v];

        if (def) {
            enqueue_inst_uses(def, live, f->nvals, work, &nwork);
        } else if (bp_block[v]) {
            u32 want_block = bp_block[v];
            u32 want_index = bp_index[v];
            u32 pbi;

            for (pbi = 0; pbi < f->nblocks; pbi++) {
                IrInst *term = f->blocks[pbi].last;
                u32 ei;

                if (!term)
                    continue;
                for (ei = 0; ei < term->nedges; ei++)
                    if (term->edges[ei].target.v == want_block &&
                        want_index < term->edges[ei].nargs)
                        enqueue_operand(term->edges[ei].args[want_index], live,
                                        f->nvals, work, &nwork);
            }
        }
    }

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];
        IrInst *in = blk->first;
        IrInst *prev = NULL;

        while (in) {
            IrInst *next = in->next;

            if (in->result.v && !live[in->result.v]) {
                if (prev)
                    prev->next = next;
                else
                    blk->first = next;
                if (blk->last == in)
                    blk->last = prev;
                blk->ninsts--;
                changed = true;
            } else {
                prev = in;
            }
            in = next;
        }
    }
    if (changed)
        ir_func_renumber(m->arena, f);
    arena_free_all(&scratch);
    return changed;
}

bool opt_dce(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= dce_func(m, &m->funcs[i], cfg);
    return changed;
}

const Pass OPT_PASS_DCE = {"dce", opt_dce};
