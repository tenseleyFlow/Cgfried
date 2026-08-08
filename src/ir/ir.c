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

static void *clone_array(Arena *arena, const void *source, size_t count,
                         size_t size, size_t align)
{
    void *copy;

    if (!source || !count)
        return NULL;
    copy = arena_alloc(arena, count * size, align);
    memcpy(copy, source, count * size);
    return copy;
}

static IrInst *clone_inst_list(Arena *arena, const IrInst *source,
                               IrInst **last)
{
    IrInst *head = NULL;
    IrInst *tail = NULL;

    for (; source; source = source->next) {
        IrInst *copy = arena_alloc(arena, sizeof(*copy), _Alignof(IrInst));
        u32 i;

        *copy = *source;
        copy->next = NULL;
        copy->ops = clone_array(arena, source->ops, source->nops,
                                sizeof(*source->ops), _Alignof(IrOperand));
        copy->edges = clone_array(arena, source->edges, source->nedges,
                                  sizeof(*source->edges), _Alignof(IrEdge));
        for (i = 0; i < copy->nedges; i++)
            copy->edges[i].args = clone_array(
                arena, source->edges[i].args, source->edges[i].nargs,
                sizeof(*source->edges[i].args), _Alignof(IrOperand));
        if (tail)
            tail->next = copy;
        else
            head = copy;
        tail = copy;
    }
    *last = tail;
    return head;
}

IrModule *ir_module_clone(Arena *arena, const IrModule *source)
{
    IrModule *copy;
    u32 i, j;

    if (!arena || !source)
        return NULL;
    copy = ir_module_new(arena, source->dc);
    copy->nsyms = copy->cap_syms = source->nsyms;
    copy->syms = clone_array(arena, source->syms, source->nsyms,
                             sizeof(*source->syms), _Alignof(char *));
    copy->sym_cgf_attrs =
        clone_array(arena, source->sym_cgf_attrs, source->nsyms,
                    sizeof(*source->sym_cgf_attrs), _Alignof(CgfAttr *));
    copy->nlocs = copy->cap_locs = source->nlocs;
    copy->locs = clone_array(arena, source->locs, source->nlocs,
                             sizeof(*source->locs), _Alignof(Span));
    copy->nmem_layouts = copy->cap_mem_layouts = source->nmem_layouts;
    copy->mem_layouts =
        clone_array(arena, source->mem_layouts, source->nmem_layouts,
                    sizeof(*source->mem_layouts), _Alignof(IrMemLayout));
    for (i = 0; i < copy->nmem_layouts; i++)
        copy->mem_layouts[i].ranges =
            clone_array(arena, source->mem_layouts[i].ranges,
                        source->mem_layouts[i].nranges, sizeof(IrByteRange),
                        _Alignof(IrByteRange));
    copy->nglobals = copy->cap_globals = source->nglobals;
    copy->globals = clone_array(arena, source->globals, source->nglobals,
                                sizeof(*source->globals), _Alignof(IrGlobal));
    for (i = 0; i < copy->nglobals; i++) {
        const IrGlobal *sg = &source->globals[i];
        IrGlobal *dg = &copy->globals[i];

        dg->init = clone_array(arena, sg->init, sg->init ? sg->size : 0, 1, 1);
        dg->relocs = clone_array(arena, sg->relocs, sg->nrelocs,
                                 sizeof(*sg->relocs), _Alignof(IrReloc));
    }
    copy->nfuncs = copy->cap_funcs = source->nfuncs;
    copy->funcs = clone_array(arena, source->funcs, source->nfuncs,
                              sizeof(*source->funcs), _Alignof(IrFunc));
    for (i = 0; i < copy->nfuncs; i++) {
        const IrFunc *sf = &source->funcs[i];
        IrFunc *df = &copy->funcs[i];

        df->module = copy;
        df->param_types = clone_array(arena, sf->param_types, sf->nparams,
                                      sizeof(*sf->param_types), 1);
        df->param_annots =
            clone_array(arena, sf->param_annots, sf->nparams,
                        sizeof(*sf->param_annots), _Alignof(u64));
        df->param_vals =
            clone_array(arena, sf->param_vals, sf->nparams,
                        sizeof(*sf->param_vals), _Alignof(ValueId));
        df->vals = clone_array(arena, sf->vals, sf->nvals, sizeof(*sf->vals),
                               _Alignof(IrValInfo));
        df->cap_vals = df->nvals;
        df->local_slots =
            clone_array(arena, sf->local_slots, sf->nlocal_slots,
                        sizeof(*sf->local_slots), _Alignof(IrLocalSlot));
        df->cap_local_slots = df->nlocal_slots;
        df->cfg_removed =
            clone_array(arena, sf->cfg_removed, sf->ncfg_removed,
                        sizeof(*sf->cfg_removed), _Alignof(IrCfgRemoved));
        df->cap_cfg_removed = df->ncfg_removed;
        df->opt_mem2reg_info = NULL;
        df->blocks = clone_array(arena, sf->blocks, sf->nblocks,
                                 sizeof(*sf->blocks), _Alignof(IrBlock));
        df->cap_blocks = df->nblocks;
        for (j = 0; j < df->nblocks; j++) {
            const IrBlock *sb = &sf->blocks[j];
            IrBlock *db = &df->blocks[j];

            db->params = clone_array(arena, sb->params, sb->nparams,
                                     sizeof(*sb->params), _Alignof(ValueId));
            db->first = clone_inst_list(arena, sb->first, &db->last);
        }
    }
    return copy;
}

void ir_mem_layout_register(IrModule *m, Span span, u64 size,
                            const IrByteRange *ranges, u32 nranges,
                            bool suppress_uninit)
{
    u32 loc = ir_intern_span(m, span);
    u32 i;

    if (!loc)
        return;
    for (i = 0; i < m->nmem_layouts; i++) {
        IrMemLayout *old = &m->mem_layouts[i];

        if (old->loc != loc || old->size != size)
            continue;
        if (old->suppress_uninit != suppress_uninit ||
            old->nranges != nranges ||
            (nranges &&
             memcmp(old->ranges, ranges, nranges * sizeof(*ranges)) != 0)) {
            /* One spelling produced incompatible aggregate shapes.  This is
             * possible through aggressive macro provenance coalescing; a
             * diagnostic must not guess which shape an instruction meant. */
            old->suppress_uninit = true;
            old->nranges = 0;
            old->ranges = NULL;
        }
        return;
    }
    if (m->nmem_layouts == m->cap_mem_layouts) {
        u32 nc = m->cap_mem_layouts ? m->cap_mem_layouts * 2 : 16;

        m->mem_layouts = grow(m->arena, m->mem_layouts, m->nmem_layouts, nc,
                              sizeof(*m->mem_layouts), _Alignof(IrMemLayout));
        m->cap_mem_layouts = nc;
    }
    m->mem_layouts[m->nmem_layouts] = (IrMemLayout){
        .loc = loc,
        .nranges = nranges,
        .size = size,
        .ranges = clone_array(m->arena, ranges, nranges, sizeof(*ranges),
                              _Alignof(IrByteRange)),
        .suppress_uninit = suppress_uninit,
    };
    m->nmem_layouts++;
}

const IrMemLayout *ir_mem_layout_find(const IrModule *m, const IrInst *in,
                                      u64 size)
{
    u32 i;

    if (!m || !in || !in->loc)
        return NULL;
    for (i = 0; i < m->nmem_layouts; i++)
        if (m->mem_layouts[i].loc == in->loc && m->mem_layouts[i].size == size)
            return &m->mem_layouts[i];
    return NULL;
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
        m->sym_cgf_attrs = grow(m->arena, m->sym_cgf_attrs, m->nsyms, nc,
                                sizeof(*m->sym_cgf_attrs), _Alignof(CgfAttr *));
        m->cap_syms = nc;
    }
    m->syms[m->nsyms] = name;
    m->sym_cgf_attrs[m->nsyms] = NULL;
    return m->nsyms++;
}

IrAlias *ir_alias_new(IrModule *m, const char *name, const char *target)
{
    IrAlias *a;

    if (m->naliases == m->cap_aliases) {
        u32 nc = m->cap_aliases ? m->cap_aliases * 2 : 4;

        m->aliases = grow(m->arena, m->aliases, m->naliases, nc,
                          sizeof(IrAlias), _Alignof(IrAlias));
        m->cap_aliases = nc;
    }
    a = &m->aliases[m->naliases++];
    memset(a, 0, sizeof(*a));
    a->name = name;
    a->target = target;
    ir_sym(m, name); /* referenceable, exactly like a global */
    return a;
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
    f->module = m;
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

bool ir_type_is_vector(IrType t)
{
    return t >= IRT_V16I8 && t <= IRT_V2F64;
}

bool ir_type_is_vector_int(IrType t)
{
    return t >= IRT_V16I8 && t <= IRT_V2I64;
}

bool ir_type_is_vector_float(IrType t)
{
    return t == IRT_V4F32 || t == IRT_V2F64;
}

IrType ir_vector_elem_type(IrType t)
{
    static const u8 elems[] = {IRT_I8,  IRT_I16, IRT_I32,
                               IRT_I64, IRT_F32, IRT_F64};

    return ir_type_is_vector(t) ? (IrType)elems[t - IRT_V16I8] : IRT_VOID;
}

u32 ir_vector_lanes(IrType t)
{
    static const u8 lanes[] = {16, 8, 4, 2, 4, 2};

    return ir_type_is_vector(t) ? lanes[t - IRT_V16I8] : 0;
}

u32 ir_type_size(IrType t)
{
    static const u8 sizes[] = {1, 2, 4, 8, 4, 8, 16, 16, 8};

    if (ir_type_is_vector(t))
        return 16;
    return t <= IRT_PTR ? sizes[t] : 0;
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

static void append_removed_block(IrFunc *f, u32 index, Span loc, u32 region,
                                 u8 flags)
{
    IrModule *m = f->module;
    u32 i;

    if (!m || index >= f->nblocks)
        return;
    if (!loc.file_id)
        loc = ir_debug_loc(m, f->loc);
    if (!loc.file_id)
        return;
    for (i = 0; i < f->ncfg_removed; i++)
        if (f->cfg_removed[i].loc.file_id == loc.file_id &&
            f->cfg_removed[i].loc.line == loc.line &&
            f->cfg_removed[i].loc.col == loc.col &&
            f->cfg_removed[i].loc.len == loc.len &&
            f->cfg_removed[i].loc.seq == loc.seq &&
            f->cfg_removed[i].loc.origin == loc.origin) {
            f->cfg_removed[i].flags |= flags;
            if (!f->cfg_removed[i].region)
                f->cfg_removed[i].region = region;
            return;
        }
    if (f->ncfg_removed == f->cap_cfg_removed) {
        u32 nc = f->cap_cfg_removed ? f->cap_cfg_removed * 2 : 8;

        f->cfg_removed = grow(m->arena, f->cfg_removed, f->ncfg_removed, nc,
                              sizeof(*f->cfg_removed), _Alignof(IrCfgRemoved));
        f->cap_cfg_removed = nc;
    }
    f->cfg_removed[f->ncfg_removed].block.v = index + 1;
    f->cfg_removed[f->ncfg_removed].loc = loc;
    f->cfg_removed[f->ncfg_removed].block_name = f->blocks[index].name;
    f->cfg_removed[f->ncfg_removed].region = region;
    f->cfg_removed[f->ncfg_removed].flags = flags;
    f->ncfg_removed++;
}

static void log_removed_block(IrFunc *f, u32 index, u8 flags)
{
    IrModule *m = f->module;
    const IrInst *in;
    Span loc = {0};

    if (!m || index >= f->nblocks)
        return;
    for (in = f->blocks[index].first; in; in = in->next)
        if (in->loc) {
            loc = ir_inst_span(m, in);
            if (in->op == IR_BR && (in->flags & IRF_FLOW_PROVENANCE))
                flags |= IR_CFG_REMOVED_DEFENSIVE_BREAK;
            break;
        }
    append_removed_block(f, index, loc, 0, flags);
}

void ir_func_record_removed(IrFunc *f, BlockId block, u8 flags)
{
    if (f && block.v >= 1 && block.v <= f->nblocks)
        log_removed_block(f, block.v - 1, flags);
}

void ir_func_record_removed_span(IrFunc *f, BlockId block, Span loc, u8 flags)
{
    if (f && block.v >= 1 && block.v <= f->nblocks)
        append_removed_block(f, block.v - 1, loc, 0, flags);
}

void ir_func_record_removed_region(IrFunc *f, BlockId block, Span loc,
                                   u32 region, u8 flags)
{
    if (f && block.v >= 1 && block.v <= f->nblocks)
        append_removed_block(f, block.v - 1, loc, region, flags);
}

static void remove_unreachable(IrFunc *f, bool retain_provenance)
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
    if (retain_provenance) {
        bool logged = false;

        /* Report the root of each disconnected dead region, not every
         * descendant block. A rootless dead SCC still gets one record. */
        for (i = 0; i < f->nblocks; i++) {
            bool dead_pred = false;
            u32 from;

            if (reach[i])
                continue;
            for (from = 0; from < f->nblocks && !dead_pred; from++) {
                const IrInst *term;

                if (reach[from])
                    continue;
                term = f->blocks[from].last;
                if (!term)
                    continue;
                for (j = 0; j < term->nedges; j++)
                    if (term->edges[j].target.v == i + 1) {
                        dead_pred = true;
                        break;
                    }
            }
            if (!dead_pred) {
                log_removed_block(f, i, 0);
                logged = true;
            }
        }
        if (!logged)
            for (i = 0; i < f->nblocks; i++)
                if (!reach[i]) {
                    log_removed_block(f, i, 0);
                    break;
                }
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

void ir_func_remove_unreachable(IrFunc *f)
{
    remove_unreachable(f, false);
}

void ir_func_remove_unreachable_with_log(IrFunc *f)
{
    remove_unreachable(f, true);
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

/* Is this symbol a thread-local OBJECT? Asked by every backend at the point
 * it would otherwise materialize an ordinary address, because a thread-local
 * has no ordinary address -- it has an offset from the thread pointer. */
bool ir_sym_is_tls(const IrModule *m, u32 sym_index)
{
    const char *name;
    u32 i;

    if (!m || !sym_index || sym_index > m->nsyms)
        return false;
    name = m->syms[sym_index - 1];
    for (i = 0; i < m->nglobals; i++)
        if (strcmp(m->globals[i].name, name) == 0)
            return m->globals[i].is_tls;
    return false;
}

IrSymBinding ir_sym_binding(const IrModule *m, u32 sym_index)
{
    IrSymBinding b = {false, true};
    const char *name;
    u32 i;

    if (!m || !sym_index || sym_index > m->nsyms)
        return b;
    name = m->syms[sym_index - 1];
    for (i = 0; i < m->nglobals; i++) {
        if (strcmp(m->globals[i].name, name) == 0) {
            b.defined_here = true;
            b.external = m->globals[i].linkage != IRLINK_INTERNAL;
            return b;
        }
    }
    for (i = 0; i < m->nfuncs; i++) {
        if (strcmp(m->funcs[i].name, name) == 0) {
            b.defined_here = true;
            b.external = m->funcs[i].linkage != IRLINK_INTERNAL;
            return b;
        }
    }
    /* Not defined here: undefined symbols are external by definition. */
    return b;
}

void ir_arg_carry_provenance(IrOperand *fresh, const IrOperand *old)
{
    if (fresh->kind != IROP_FCONST)
        fresh->b = old->b;
    fresh->argflags = old->argflags;
}

static bool operand_eq(const IrOperand *a, const IrOperand *b)
{
    return a->kind == b->kind && a->type == b->type &&
           a->argflags == b->argflags && a->sym == b->sym && a->a == b->a &&
           a->b == b->b;
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

IrAlias *ir_alias_find(IrModule *m, const char *name)
{
    u32 i;

    for (i = 0; i < m->naliases; i++)
        if (str_eq(m->aliases[i].name, name))
            return &m->aliases[i];
    return NULL;
}

/* Both NULL, or both present and equal. A section name is optional, so a
 * plain str_eq would deref a NULL on the common case. */
static bool str_eq_opt(const char *a, const char *b)
{
    if (!a || !b)
        return a == b;
    return str_eq(a, b);
}

bool ir_module_struct_eq(const IrModule *a, const IrModule *b)
{
    u32 i, j;

    if (a->nfuncs != b->nfuncs || a->nglobals != b->nglobals ||
        a->nsyms != b->nsyms || a->naliases != b->naliases)
        return false;
    for (i = 0; i < a->nsyms; i++)
        if (!str_eq(a->syms[i], b->syms[i]))
            return false;
    for (i = 0; i < a->naliases; i++) {
        const IrAlias *x = &a->aliases[i];
        const IrAlias *y = &b->aliases[i];

        if (!str_eq(x->name, y->name) || !str_eq(x->target, y->target) ||
            x->linkage != y->linkage || x->is_weak != y->is_weak ||
            x->visibility != y->visibility)
            return false;
    }
    for (i = 0; i < a->nglobals; i++) {
        const IrGlobal *x = &a->globals[i];
        const IrGlobal *y = &b->globals[i];

        if (!str_eq(x->name, y->name) || x->size != y->size ||
            x->align != y->align || x->linkage != y->linkage ||
            x->is_tentative != y->is_tentative || x->is_tls != y->is_tls ||
            x->is_weak != y->is_weak || x->visibility != y->visibility ||
            x->is_used != y->is_used || !str_eq_opt(x->section, y->section) ||
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
            x->unprototyped != y->unprototyped || x->abi_ret != y->abi_ret ||
            x->abi_ret_n != y->abi_ret_n || x->linkage != y->linkage ||
            x->calls_setjmp != y->calls_setjmp || x->is_weak != y->is_weak ||
            x->visibility != y->visibility ||
            x->fp_contract != y->fp_contract || x->align != y->align ||
            x->is_used != y->is_used || !str_eq_opt(x->section, y->section))
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
