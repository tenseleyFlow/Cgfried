#include "ir/ir.h"

#include <string.h>

/* Module construction is ARENA-ONLY (a DoD line item): growable arrays
 * double into fresh arena space and abandon the old copy. The waste is
 * bounded (a geometric series) and what it buys is that freeing a module
 * is freeing its arena — no per-inst malloc, no leak surface at all. */

static void *grow(Arena *a, void *old, size_t oldn, size_t newn, size_t esz,
                  size_t align)
{
    void *p = arena_alloc(a, newn * esz, align);

    if (oldn)
        memcpy(p, old, oldn * esz);
    return p;
}

IrModule *ir_module_new(Arena *arena, DiagCtx *dc)
{
    IrModule *m = arena_alloc(arena, sizeof(IrModule), _Alignof(IrModule));

    memset(m, 0, sizeof(*m));
    m->arena = arena;
    m->dc = dc;
    return m;
}

u32 ir_sym(IrModule *m, const char *name)
{
    u32 i;

    /* Linear: the symbol table is small and INSERTION-ORDERED, which the
     * printer depends on. A map would demand order bookkeeping anyway. */
    for (i = 0; i < m->nsyms; i++)
        if (m->syms[i] == name || strcmp(m->syms[i], name) == 0)
            return i;
    if (m->nsyms == m->cap_syms) {
        u32 nc = m->cap_syms ? m->cap_syms * 2 : 8;

        m->syms = grow(m->arena, m->syms, m->nsyms, nc, sizeof(char *),
                       _Alignof(char *));
        m->cap_syms = nc;
    }
    m->syms[m->nsyms] = name;
    return m->nsyms++;
}

IrGlobal *ir_global_new(IrModule *m, const char *name)
{
    IrGlobal *g;

    if (m->nglobals == m->cap_globals) {
        u32 nc = m->cap_globals ? m->cap_globals * 2 : 8;

        m->globals = grow(m->arena, m->globals, m->nglobals, nc,
                          sizeof(IrGlobal), _Alignof(IrGlobal));
        m->cap_globals = nc;
    }
    g = &m->globals[m->nglobals++];
    memset(g, 0, sizeof(*g));
    g->name = name;
    g->align = 1;
    ir_sym(m, name); /* every global is referenceable */
    return g;
}

static ValueId new_value(IrModule *m, IrFunc *f, IrType t, IrValDef kind,
                         BlockId blk, u32 pos)
{
    ValueId v;

    if (f->nvals == f->cap_vals) {
        u32 nc = f->cap_vals ? f->cap_vals * 2 : 16;

        f->vals = grow(m->arena, f->vals, f->nvals, nc, sizeof(IrValInfo),
                       _Alignof(IrValInfo));
        f->cap_vals = nc;
    }
    f->vals[f->nvals].type = (u8)t;
    f->vals[f->nvals].def_kind = (u8)kind;
    f->vals[f->nvals].def_block = blk;
    f->vals[f->nvals].def_pos = pos;
    f->nvals++;
    v.v = f->nvals; /* ids are 1-based; 0 stays invalid */
    return v;
}

IrFunc *ir_func_new(IrModule *m, const char *name, IrType ret,
                    const IrType *params, u32 nparams)
{
    IrFunc *f;
    u32 i;

    if (m->nfuncs == m->cap_funcs) {
        u32 nc = m->cap_funcs ? m->cap_funcs * 2 : 8;

        m->funcs = grow(m->arena, m->funcs, m->nfuncs, nc, sizeof(IrFunc),
                        _Alignof(IrFunc));
        m->cap_funcs = nc;
    }
    f = &m->funcs[m->nfuncs++];
    memset(f, 0, sizeof(*f));
    f->name = name;
    f->ret = (u8)ret;
    f->linkage = IRLINK_EXTERNAL; /* ` internal` marker flips it */
    f->nparams = nparams;
    if (nparams) {
        f->param_types = arena_alloc(m->arena, nparams, 1);
        f->param_vals =
            arena_alloc(m->arena, nparams * sizeof(ValueId), _Alignof(ValueId));
        for (i = 0; i < nparams; i++) {
            f->param_types[i] = (u8)params[i];
            /* Function parameters are the ENTRY BLOCK's implicit defs:
             * the entry block itself declares no params (verifier check
             * 5), and these values are defined "at its head". */
            f->param_vals[i] =
                new_value(m, f, params[i], VDEF_FPARAM, (BlockId){1}, 0);
        }
    }
    ir_sym(m, name);
    return f;
}

BlockId ir_block_new(IrModule *m, IrFunc *f, const char *name)
{
    BlockId b;

    if (f->nblocks == f->cap_blocks) {
        u32 nc = f->cap_blocks ? f->cap_blocks * 2 : 8;

        f->blocks = grow(m->arena, f->blocks, f->nblocks, nc, sizeof(IrBlock),
                         _Alignof(IrBlock));
        f->cap_blocks = nc;
    }
    memset(&f->blocks[f->nblocks], 0, sizeof(IrBlock));
    f->blocks[f->nblocks].name = name;
    f->nblocks++;
    b.v = f->nblocks; /* 1-based */
    return b;
}

IrBlock *ir_block(IrFunc *f, BlockId b)
{
    if (b.v == 0 || b.v > f->nblocks)
        return NULL;
    return &f->blocks[b.v - 1];
}

ValueId ir_block_param(IrModule *m, IrFunc *f, BlockId b, IrType t)
{
    IrBlock *blk = ir_block(f, b);
    ValueId v = new_value(m, f, t, VDEF_BPARAM, b, 0);
    ValueId *np;

    np = grow(m->arena, blk->params, blk->nparams, blk->nparams + 1,
              sizeof(ValueId), _Alignof(ValueId));
    np[blk->nparams] = v;
    blk->params = np;
    blk->nparams++;
    return v;
}

IrType ir_value_type(const IrFunc *f, ValueId v)
{
    if (v.v == 0 || v.v > f->nvals)
        return IRT_VOID;
    return (IrType)f->vals[v.v - 1].type;
}

/* --- operands ------------------------------------------------------------ */

IrOperand ir_op_value(const IrFunc *f, ValueId v)
{
    IrOperand o;

    memset(&o, 0, sizeof(o));
    o.kind = IROP_VALUE;
    o.type = (u8)ir_value_type(f, v);
    o.a = v.v;
    return o;
}

IrOperand ir_op_iconst(IrType t, i64 v)
{
    IrOperand o;

    memset(&o, 0, sizeof(o));
    o.kind = IROP_ICONST;
    o.type = (u8)t;
    o.a = (u64)v;
    return o;
}

IrOperand ir_op_fconst(IrType t, u64 lo, u64 hi)
{
    IrOperand o;

    memset(&o, 0, sizeof(o));
    o.kind = IROP_FCONST;
    o.type = (u8)t;
    o.a = lo;
    o.b = hi;
    return o;
}

IrOperand ir_op_symbol(IrType t, u32 sym, i64 addend)
{
    IrOperand o;

    memset(&o, 0, sizeof(o));
    o.kind = IROP_SYMBOL;
    o.type = (u8)t;
    o.sym = sym;
    o.a = (u64)addend;
    return o;
}

IrOperand ir_op_undef(IrType t)
{
    IrOperand o;

    memset(&o, 0, sizeof(o));
    o.kind = IROP_UNDEF;
    o.type = (u8)t;
    return o;
}

/* --- unreachable-block cleanup ------------------------------------------- */

void ir_func_remove_unreachable(IrFunc *f)
{
    Arena scratch;
    bool *reach;
    u32 *work;
    u32 *remap;
    u32 nwork = 0;
    u32 i, j;
    u32 next = 0;
    bool any_dead = false;

    if (f->nblocks == 0)
        return;
    /* The IR has no semantic block-count ceiling.  Keep the work arrays
     * off the C stack, but size them to the actual function so cleanup
     * remains valid for every representable CFG. */
    arena_init(&scratch);
    reach = arena_alloc(&scratch, f->nblocks * sizeof(bool), _Alignof(bool));
    work = arena_alloc(&scratch, f->nblocks * sizeof(u32), _Alignof(u32));
    remap = arena_alloc(&scratch, f->nblocks * sizeof(u32), _Alignof(u32));
    memset(reach, 0, f->nblocks * sizeof(bool));
    reach[0] = true;
    work[nwork++] = 0;
    while (nwork) {
        u32 b = work[--nwork];
        const IrInst *in;

        for (in = f->blocks[b].first; in; in = in->next)
            for (i = 0; i < in->nedges; i++) {
                u32 t = in->edges[i].target.v;

                if (t >= 1 && t <= f->nblocks && !reach[t - 1]) {
                    reach[t - 1] = true;
                    work[nwork++] = t - 1;
                }
            }
    }
    for (i = 0; i < f->nblocks; i++) {
        if (reach[i])
            remap[i] = next++;
        else
            any_dead = true;
    }
    if (!any_dead) {
        arena_free_all(&scratch);
        return;
    }
    /* Values defined in dying blocks lose their def coordinates; their
     * ids stay allocated so every surviving operand id is untouched. */
    for (i = 0; i < f->nvals; i++) {
        u32 db = f->vals[i].def_block.v;

        if (db >= 1 && db <= f->nblocks) {
            if (!reach[db - 1]) {
                f->vals[i].def_block.v = 0;
                f->vals[i].def_kind = VDEF_NONE;
            } else {
                f->vals[i].def_block.v = remap[db - 1] + 1;
            }
        }
    }
    for (i = 0; i < f->nblocks; i++) {
        IrInst *in;

        if (!reach[i])
            continue;
        for (in = f->blocks[i].first; in; in = in->next)
            for (j = 0; j < in->nedges; j++) {
                u32 t = in->edges[j].target.v;

                if (t >= 1 && t <= f->nblocks)
                    in->edges[j].target.v = remap[t - 1] + 1;
            }
        f->blocks[remap[i]] = f->blocks[i];
    }
    f->nblocks = next;
    arena_free_all(&scratch);
}

/* --- canonical value renumbering ------------------------------------------ */

static void renumber_operand(IrOperand *o, const u32 *map, u32 nold)
{
    if (o->kind == IROP_VALUE) {
        u32 id = (u32)o->a;

        o->a = (id >= 1 && id <= nold) ? map[id] : 0;
    }
}

void ir_func_renumber(Arena *arena, IrFunc *f)
{
    u32 nold = f->nvals;
    u32 *map = arena_alloc(arena, (nold + 1) * sizeof(u32), _Alignof(u32));
    IrValInfo *nv;
    u32 next = 0;
    u32 bi, i;
    IrInst *in;

    memset(map, 0, (nold + 1) * sizeof(u32));
    /* Pass 1: assign new ids in document order. */
    for (i = 0; i < f->nparams; i++)
        if (f->param_vals[i].v)
            map[f->param_vals[i].v] = ++next;
    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];

        for (i = 0; i < blk->nparams; i++)
            if (blk->params[i].v)
                map[blk->params[i].v] = ++next;
        for (in = blk->first; in; in = in->next)
            if (in->result.v)
                map[in->result.v] = ++next;
    }
    /* Pass 2: rebuild the vals table with fresh def coordinates. */
    nv = arena_alloc(arena, (next ? next : 1) * sizeof(IrValInfo),
                     _Alignof(IrValInfo));
    memset(nv, 0, (next ? next : 1) * sizeof(IrValInfo));
    for (i = 0; i < f->nparams; i++) {
        u32 nid = map[f->param_vals[i].v];

        if (!nid)
            continue;
        nv[nid - 1].type = f->vals[f->param_vals[i].v - 1].type;
        nv[nid - 1].def_kind = VDEF_FPARAM;
        nv[nid - 1].def_block.v = 1;
        f->param_vals[i].v = nid;
    }
    for (i = 0; i < f->nlocal_slots; i++) {
        u32 old = f->local_slots[i].addr.v;

        f->local_slots[i].addr.v = old >= 1 && old <= nold ? map[old] : 0;
    }
    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];
        u32 pos = 0;

        for (i = 0; i < blk->nparams; i++) {
            u32 old = blk->params[i].v;
            u32 nid = old ? map[old] : 0;

            if (!nid)
                continue;
            nv[nid - 1].type = f->vals[old - 1].type;
            nv[nid - 1].def_kind = VDEF_BPARAM;
            nv[nid - 1].def_block.v = bi + 1;
            blk->params[i].v = nid;
        }
        for (in = blk->first; in; in = in->next, pos++) {
            if (in->result.v) {
                u32 old = in->result.v;
                u32 nid = map[old];

                if (nid) {
                    nv[nid - 1].type = f->vals[old - 1].type;
                    nv[nid - 1].def_kind = VDEF_INST;
                    nv[nid - 1].def_block.v = bi + 1;
                    nv[nid - 1].def_pos = pos;
                    in->result.v = nid;
                }
            }
        }
    }
    /* Pass 3: rewrite every use. */
    for (bi = 0; bi < f->nblocks; bi++)
        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 j;

            for (i = 0; i < in->nops; i++)
                renumber_operand(&in->ops[i], map, nold);
            for (i = 0; i < in->nedges; i++)
                for (j = 0; j < in->edges[i].nargs; j++)
                    renumber_operand(&in->edges[i].args[j], map, nold);
        }
    f->vals = nv;
    f->nvals = next;
    f->cap_vals = next;
}

/* --- structural equality ------------------------------------------------- */

static bool str_eq(const char *a, const char *b)
{
    if (a == b)
        return true;
    if (!a || !b)
        return false;
    return strcmp(a, b) == 0;
}

static bool operand_eq(const IrOperand *a, const IrOperand *b)
{
    return a->kind == b->kind && a->type == b->type && a->sym == b->sym &&
           a->a == b->a && a->b == b->b;
}

static bool inst_eq(const IrInst *a, const IrInst *b)
{
    u32 i, j;

    if (a->op != b->op || a->type != b->type || a->subop != b->subop ||
        a->flags != b->flags || a->align != b->align ||
        a->result.v != b->result.v || a->nops != b->nops ||
        a->nedges != b->nedges || a->callee != b->callee)
        return false;
    for (i = 0; i < a->nops; i++)
        if (!operand_eq(&a->ops[i], &b->ops[i]))
            return false;
    for (i = 0; i < a->nedges; i++) {
        if (a->edges[i].target.v != b->edges[i].target.v ||
            a->edges[i].nargs != b->edges[i].nargs ||
            a->edges[i].case_val != b->edges[i].case_val)
            return false;
        for (j = 0; j < a->edges[i].nargs; j++)
            if (!operand_eq(&a->edges[i].args[j], &b->edges[i].args[j]))
                return false;
    }
    return true;
}

bool ir_module_struct_eq(const IrModule *a, const IrModule *b)
{
    u32 i, j;

    if (a->nfuncs != b->nfuncs || a->nglobals != b->nglobals ||
        a->nsyms != b->nsyms)
        return false;
    for (i = 0; i < a->nsyms; i++)
        if (!str_eq(a->syms[i], b->syms[i]))
            return false;
    for (i = 0; i < a->nglobals; i++) {
        const IrGlobal *x = &a->globals[i];
        const IrGlobal *y = &b->globals[i];

        if (!str_eq(x->name, y->name) || x->size != y->size ||
            x->align != y->align || x->linkage != y->linkage ||
            x->is_tentative != y->is_tentative ||
            (x->init == NULL) != (y->init == NULL) || x->nrelocs != y->nrelocs)
            return false;
        if (x->init && x->size && memcmp(x->init, y->init, x->size) != 0)
            return false;
        for (j = 0; j < x->nrelocs; j++)
            if (x->relocs[j].offset != y->relocs[j].offset ||
                x->relocs[j].symbol != y->relocs[j].symbol ||
                x->relocs[j].addend != y->relocs[j].addend)
                return false;
    }
    for (i = 0; i < a->nfuncs; i++) {
        const IrFunc *x = &a->funcs[i];
        const IrFunc *y = &b->funcs[i];

        if (!str_eq(x->name, y->name) || x->ret != y->ret ||
            x->nparams != y->nparams || x->nblocks != y->nblocks ||
            x->nvals != y->nvals || x->variadic != y->variadic ||
            x->abi_ret != y->abi_ret || x->linkage != y->linkage ||
            x->calls_setjmp != y->calls_setjmp)
            return false;
        for (j = 0; j < x->nparams; j++) {
            u64 xa = x->param_annots ? x->param_annots[j] : 0;
            u64 ya = y->param_annots ? y->param_annots[j] : 0;

            if (x->param_types[j] != y->param_types[j] || xa != ya)
                return false;
        }
        for (j = 0; j < x->nblocks; j++) {
            const IrBlock *p = &x->blocks[j];
            const IrBlock *q = &y->blocks[j];
            const IrInst *ii;
            const IrInst *jj;
            u32 k;

            if (!str_eq(p->name, q->name) || p->nparams != q->nparams ||
                p->ninsts != q->ninsts)
                return false;
            for (k = 0; k < p->nparams; k++)
                if (p->params[k].v != q->params[k].v)
                    return false;
            for (ii = p->first, jj = q->first; ii && jj;
                 ii = ii->next, jj = jj->next)
                if (!inst_eq(ii, jj))
                    return false;
            if (ii || jj)
                return false;
        }
        for (j = 0; j < x->nvals; j++)
            if (x->vals[j].type != y->vals[j].type)
                return false;
    }
    return true;
}
