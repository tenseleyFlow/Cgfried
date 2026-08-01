#include "opt/opt.h"

#include <string.h>

#include "opt/alias.h"
#include "util/arena.h"

typedef struct {
    u8 kind;
    u8 type;
    u32 sym;
    u64 a;
    u64 b;
} GvnOperand;

typedef struct {
    u8 op;
    u8 type;
    u8 subop;
    u8 flags;
    u32 nops;
    GvnOperand ops[3];
} GvnKey;

typedef struct {
    u64 hash;
    bool used;
    GvnKey key;
    IrOperand leader;
    BlockId block;
} GvnSlot;

typedef struct {
    IrInst **insts;
    u32 ninsts;
} BlockInsts;

typedef enum { MEM_NONE, MEM_FOUND, MEM_BLOCKED } MemResult;

bool opt_gvn(IrModule *m, const OptConfig *cfg);
const Pass OPT_PASS_GVN = {"gvn", opt_gvn, PASS_PINNED_EXACT};

static IrOperand resolve_operand(IrOperand op, const IrOperand *replacement,
                                 u32 nold)
{
    u32 steps = 0;

    while (op.kind == IROP_VALUE && op.a >= 1 && op.a <= nold &&
           replacement[op.a].kind != IROP_NONE) {
        op = replacement[op.a];
        if (++steps > nold)
            CGF_ICE("gvn: cyclic value replacement");
    }
    return op;
}

static IrOperand resolve_inst_operand(IrOperand op,
                                      const IrOperand *replacement, u32 nold,
                                      bool preserve_annot)
{
    u64 annot = op.b;

    op = resolve_operand(op, replacement, nold);
    if (preserve_annot)
        op.b = annot;
    return op;
}

static bool is_pure_candidate(IrOp op)
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
    case IR_FDIV:
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

static bool operand_may_undef(IrOperand op, const bool *may_undef, u32 nvals)
{
    if (op.kind == IROP_UNDEF)
        return true;
    return op.kind == IROP_VALUE && op.a >= 1 && op.a <= nvals &&
           may_undef[op.a];
}

static bool inst_may_produce_undef(const IrInst *in)
{
    switch ((IrOp)in->op) {
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
    case IR_FPTOSI:
    case IR_FPTOUI:
        return true;
    default:
        return false;
    }
}

static void find_may_undef(const IrFunc *f, bool *may_undef)
{
    bool moved;

    do {
        u32 bi;

        moved = false;
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrBlock *blk = &f->blocks[bi];
            const IrInst *in;

            for (in = blk->first; in; in = in->next) {
                u32 ei;

                for (ei = 0; ei < in->nedges; ei++) {
                    const IrEdge *edge = &in->edges[ei];
                    const IrBlock *target = &f->blocks[edge->target.v - 1];
                    u32 ai;

                    for (ai = 0; ai < edge->nargs; ai++) {
                        ValueId param = target->params[ai];

                        if (!may_undef[param.v] &&
                            operand_may_undef(edge->args[ai], may_undef,
                                              f->nvals)) {
                            may_undef[param.v] = true;
                            moved = true;
                        }
                    }
                }
            }
            for (in = blk->first; in; in = in->next) {
                u32 oi;

                if (!in->result.v || may_undef[in->result.v])
                    continue;
                if (inst_may_produce_undef(in)) {
                    may_undef[in->result.v] = true;
                    moved = true;
                    continue;
                }
                for (oi = 0; oi < in->nops; oi++)
                    if (operand_may_undef(in->ops[oi], may_undef, f->nvals)) {
                        may_undef[in->result.v] = true;
                        moved = true;
                        break;
                    }
            }
        }
    } while (moved);
}

static bool store_value_safe_to_forward(IrOperand value, const bool *may_undef,
                                        u32 nvals)
{
    return !operand_may_undef(value, may_undef, nvals);
}

static bool has_undef_operand(const IrInst *in, const bool *may_undef,
                              u32 nvals)
{
    u32 i;

    for (i = 0; i < in->nops; i++)
        if (operand_may_undef(in->ops[i], may_undef, nvals))
            return true;
    return false;
}

static bool commutative_key(const IrInst *in)
{
    switch ((IrOp)in->op) {
    case IR_IADD:
    case IR_IMUL:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
        return true;
    case IR_ICMP:
        return in->subop == ICMP_EQ || in->subop == ICMP_NE;
    case IR_FCMP:
        return in->subop == FCMP_OEQ || in->subop == FCMP_ONE ||
               in->subop == FCMP_UEQ || in->subop == FCMP_UNE;
    default:
        return false;
    }
}

static bool goperand_less(GvnOperand a, GvnOperand b)
{
    if (a.kind != b.kind)
        return a.kind < b.kind;
    if (a.type != b.type)
        return a.type < b.type;
    if (a.sym != b.sym)
        return a.sym < b.sym;
    if (a.a != b.a)
        return a.a < b.a;
    return a.b < b.b;
}

static GvnOperand class_operand(IrOperand op, const u32 *classes, u32 nold)
{
    GvnOperand out;

    out.kind = op.kind;
    out.type = op.type;
    out.sym = op.sym;
    out.a = op.a;
    out.b = op.b;
    if (op.kind == IROP_VALUE && op.a >= 1 && op.a <= nold) {
        if (!classes[op.a])
            CGF_ICE("gvn: operand class used before its definition");
        out.a = classes[op.a];
        out.b = 0;
    }
    return out;
}

static void make_key(GvnKey *key, const IrInst *in, const u32 *classes,
                     u32 nold)
{
    u32 i;

    memset(key, 0, sizeof(*key));
    key->op = in->op;
    key->type = in->type;
    key->subop = in->subop;
    key->flags = in->flags;
    key->nops = in->nops;
    for (i = 0; i < in->nops; i++)
        key->ops[i] = class_operand(in->ops[i], classes, nold);
    if (key->nops == 2 && commutative_key(in) &&
        goperand_less(key->ops[1], key->ops[0])) {
        GvnOperand tmp = key->ops[0];

        key->ops[0] = key->ops[1];
        key->ops[1] = tmp;
    }
}

static bool key_equal(const GvnKey *a, const GvnKey *b)
{
    return a->op == b->op && a->type == b->type && a->subop == b->subop &&
           a->flags == b->flags && a->nops == b->nops &&
           memcmp(a->ops, b->ops, a->nops * sizeof(a->ops[0])) == 0;
}

static u64 hash_mix(u64 hash, u64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

static u64 key_hash(const GvnKey *key)
{
    u64 hash = 0xcbf29ce484222325ull;
    u32 i;

    hash = hash_mix(hash, key->op);
    hash = hash_mix(hash, key->type);
    hash = hash_mix(hash, key->subop);
    hash = hash_mix(hash, key->flags);
    hash = hash_mix(hash, key->nops);
    for (i = 0; i < key->nops; i++) {
        hash = hash_mix(hash, key->ops[i].kind);
        hash = hash_mix(hash, key->ops[i].type);
        hash = hash_mix(hash, key->ops[i].sym);
        hash = hash_mix(hash, key->ops[i].a);
        hash = hash_mix(hash, key->ops[i].b);
    }
    return hash;
}

static u64 scalar_size(IrType type)
{
    u64 size = ir_type_size(type);

    if (!size)
        CGF_ICE("gvn: memory access has void type");
    return size;
}

static bool is_barrier(const IrInst *in)
{
    return in->op == IR_CALL || in->op == IR_ATOMICRMW ||
           in->op == IR_CMPXCHG ||
           (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) != 0;
}

static bool write_loc(AliasCtx *alias, const IrInst *in, MemLoc *out)
{
    u64 size;

    if (in->op == IR_STORE && in->nops == 2) {
        *out = alias_memloc(alias, in->ops[1],
                            scalar_size((IrType)in->ops[0].type),
                            (EffTypeId)in->subop);
        return true;
    }
    if ((in->op == IR_MEMCPY || in->op == IR_MEMSET) && in->nops == 3) {
        if (in->ops[2].kind != IROP_ICONST)
            return false;
        size = in->ops[2].a;
        *out = alias_memloc(alias, in->ops[0], size, ETYPE_CHAR);
        return true;
    }
    return false;
}

static MemResult
scan_memory_range(AliasCtx *alias, IrFunc *f, IrInst *const *insts, u32 end,
                  const IrInst *load, const IrOperand *replacement, u32 nold,
                  const bool *may_undef, IrOperand *found, bool *barrier)
{
    MemLoc wanted =
        alias_memloc(alias, load->ops[0], scalar_size((IrType)load->type),
                     (EffTypeId)load->subop);

    while (end) {
        const IrInst *in = insts[--end];

        if (is_barrier(in)) {
            *barrier = true;
            return MEM_BLOCKED;
        }
        if (in->op == IR_LOAD && in->nops == 1) {
            MemLoc prior;

            if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) {
                *barrier = true;
                return MEM_BLOCKED;
            }
            if (in->type != load->type)
                continue;
            prior =
                alias_memloc(alias, in->ops[0], scalar_size((IrType)in->type),
                             (EffTypeId)in->subop);
            if (alias_query(alias, wanted, prior) == ALIAS_MUST) {
                *found = resolve_operand(ir_op_value(f, in->result),
                                         replacement, nold);
                return MEM_FOUND;
            }
            continue;
        }
        if (in->op == IR_STORE || in->op == IR_MEMCPY || in->op == IR_MEMSET) {
            MemLoc written;
            AliasResult relation;

            if (!write_loc(alias, in, &written))
                return MEM_BLOCKED;
            relation = alias_query(alias, wanted, written);
            if (relation == ALIAS_NO)
                continue;
            if (in->op == IR_STORE && relation == ALIAS_MUST &&
                in->ops[0].type == load->type &&
                store_value_safe_to_forward(in->ops[0], may_undef, nold)) {
                *found = resolve_operand(in->ops[0], replacement, nold);
                return MEM_FOUND;
            }
            return MEM_BLOCKED;
        }
    }
    return MEM_NONE;
}

static bool block_has_edge_to(const IrBlock *from, BlockId to)
{
    const IrInst *in;
    u32 ei;

    for (in = from->first; in; in = in->next)
        for (ei = 0; ei < in->nedges; ei++)
            if (in->edges[ei].target.v == to.v)
                return true;
    return false;
}

static bool ancestor_has_matching_memory(AliasCtx *alias, const IrFunc *f,
                                         const IrDomTree *dom, BlockId from,
                                         const IrInst *load, bool include_idom)
{
    MemLoc wanted =
        alias_memloc(alias, load->ops[0], scalar_size((IrType)load->type),
                     (EffTypeId)load->subop);
    BlockId b = ir_idom(dom, from);

    if (b.v && !include_idom)
        b = ir_idom(dom, b);
    while (b.v) {
        const IrInst *in;

        for (in = f->blocks[b.v - 1].first; in; in = in->next) {
            MemLoc prior;

            if (in->op == IR_LOAD && in->type == load->type &&
                !(in->flags & (IRF_VOLATILE | IRF_SEQ_CST))) {
                prior = alias_memloc(alias, in->ops[0],
                                     scalar_size((IrType)in->type),
                                     (EffTypeId)in->subop);
                if (alias_query(alias, wanted, prior) == ALIAS_MUST)
                    return true;
            } else if (in->op == IR_STORE && in->ops[0].type == load->type) {
                prior = alias_memloc(alias, in->ops[1],
                                     scalar_size((IrType)in->ops[0].type),
                                     (EffTypeId)in->subop);
                if (alias_query(alias, wanted, prior) == ALIAS_MUST)
                    return true;
            }
        }
        b = ir_idom(dom, b);
    }
    return false;
}

static MemResult find_load_value(AliasCtx *alias, IrFunc *f,
                                 const IrDomTree *dom, const BlockInsts *blocks,
                                 const u32 *preds, u32 bi, u32 pos,
                                 const IrInst *load,
                                 const IrOperand *replacement, u32 nold,
                                 const bool *may_undef, IrOperand *found,
                                 bool *barrier, bool *long_path)
{
    MemResult result =
        scan_memory_range(alias, f, blocks[bi].insts, pos, load, replacement,
                          nold, may_undef, found, barrier);
    BlockId block = {bi + 1};
    BlockId idom;

    if (result != MEM_NONE)
        return result;
    idom = ir_idom(dom, block);
    if (!idom.v)
        return MEM_NONE;
    if (preds[bi] != 1 || !block_has_edge_to(&f->blocks[idom.v - 1], block)) {
        if (ancestor_has_matching_memory(alias, f, dom, block, load, true))
            *long_path = true;
        return MEM_NONE;
    }
    result = scan_memory_range(alias, f, blocks[idom.v - 1].insts,
                               blocks[idom.v - 1].ninsts, load, replacement,
                               nold, may_undef, found, barrier);
    if (result == MEM_NONE &&
        ancestor_has_matching_memory(alias, f, dom, block, load, false))
        *long_path = true;
    return result;
}

static void build_block_insts(Arena *scratch, const IrFunc *f,
                              BlockInsts *blocks, u32 *preds)
{
    u32 bi;

    memset(preds, 0, f->nblocks * sizeof(*preds));
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrBlock *block = &f->blocks[bi];
        const IrInst *in;
        u32 pos = 0;

        blocks[bi].ninsts = block->ninsts;
        blocks[bi].insts = arena_alloc(
            scratch, (block->ninsts ? block->ninsts : 1) * sizeof(IrInst *),
            _Alignof(IrInst *));
        for (in = block->first; in; in = in->next) {
            u32 ei;

            blocks[bi].insts[pos++] = (IrInst *)in;
            for (ei = 0; ei < in->nedges; ei++)
                if (in->edges[ei].target.v &&
                    in->edges[ei].target.v <= f->nblocks)
                    preds[in->edges[ei].target.v - 1]++;
        }
        if (pos != block->ninsts)
            CGF_ICE("gvn: stale instruction count in block %u", bi + 1);
    }
}

static void build_dom_preorder(Arena *scratch, const IrFunc *f,
                               const IrDomTree *dom, u32 *order)
{
    u32 *stack =
        arena_alloc(scratch, f->nblocks * sizeof(*stack), _Alignof(u32));
    u32 sp = 0, norder = 0, i;

    stack[sp++] = 0;
    while (sp) {
        u32 b = stack[--sp];

        order[norder++] = b;
        for (i = f->nblocks; i-- > 1;) {
            BlockId p = ir_idom(dom, (BlockId){i + 1});

            if (p.v == b + 1)
                stack[sp++] = i;
        }
    }
    if (norder != f->nblocks)
        CGF_ICE("gvn: dominator preorder missed a block");
}

static bool gvn_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    AliasConfig acfg;
    AliasCtx *alias;
    IrDomTree *dom;
    BlockInsts *blocks;
    IrOperand *replacement;
    u32 *classes, *order, *preds;
    bool *may_undef;
    GvnSlot *table;
    size_t table_cap = 8;
    u32 nold = f->nvals, next_class = 1;
    u32 total_insts = 0, oi;
    bool changed = false;
    bool logged_store = false, logged_barrier = false;
    OptConfig fc = *cfg;

    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    blocks = arena_alloc(&scratch, f->nblocks * sizeof(*blocks),
                         _Alignof(BlockInsts));
    preds = arena_alloc(&scratch, f->nblocks * sizeof(*preds), _Alignof(u32));
    order = arena_alloc(&scratch, f->nblocks * sizeof(*order), _Alignof(u32));
    replacement = arena_alloc(&scratch, (nold + 1) * sizeof(*replacement),
                              _Alignof(IrOperand));
    classes =
        arena_alloc(&scratch, (nold + 1) * sizeof(*classes), _Alignof(u32));
    may_undef = arena_alloc(&scratch, (nold + 1) * sizeof(*may_undef), 1);
    memset(replacement, 0, (nold + 1) * sizeof(*replacement));
    memset(classes, 0, (nold + 1) * sizeof(*classes));
    memset(may_undef, 0, (nold + 1) * sizeof(*may_undef));
    find_may_undef(f, may_undef);
    build_block_insts(&scratch, f, blocks, preds);
    build_dom_preorder(&scratch, f, dom, order);
    for (oi = 0; oi < f->nblocks; oi++)
        total_insts += blocks[oi].ninsts;
    while (table_cap < (size_t)total_insts * 2) {
        if (table_cap > SIZE_MAX / 2)
            CGF_ICE("gvn: expression table capacity overflow");
        table_cap *= 2;
    }
    table =
        arena_alloc(&scratch, table_cap * sizeof(*table), _Alignof(GvnSlot));
    memset(table, 0, table_cap * sizeof(*table));
    for (oi = 0; oi < f->nparams; oi++)
        classes[f->param_vals[oi].v] = next_class++;
    for (oi = 0; oi < f->nblocks; oi++) {
        u32 pi;

        for (pi = 0; pi < f->blocks[oi].nparams; pi++)
            classes[f->blocks[oi].params[pi].v] = next_class++;
    }

    acfg.func = f;
    acfg.no_strict_aliasing = cfg->no_strict_aliasing;
    alias = alias_build(m, &acfg);
    fc.current_func = f->name;

    for (oi = 0; oi < f->nblocks; oi++) {
        u32 bi = order[oi], pos;

        for (pos = 0; pos < blocks[bi].ninsts; pos++) {
            IrInst *in = blocks[bi].insts[pos];

            if (!in->result.v)
                continue;
            if (is_pure_candidate((IrOp)in->op) && in->nops <= 3 &&
                !may_undef[in->result.v] &&
                !has_undef_operand(in, may_undef, nold)) {
                GvnKey key;
                u64 hash;
                size_t slot, insert;
                bool eliminated = false;

                make_key(&key, in, classes, nold);
                hash = key_hash(&key);
                slot = (size_t)hash & (table_cap - 1);
                insert = slot;
                while (table[slot].used) {
                    if (table[slot].hash == hash &&
                        key_equal(&table[slot].key, &key) &&
                        ir_dominates(dom, table[slot].block,
                                     (BlockId){bi + 1})) {
                        replacement[in->result.v] = table[slot].leader;
                        classes[in->result.v] = classes[table[slot].leader.a];
                        eliminated = true;
                        changed = true;
                        break;
                    }
                    slot = (slot + 1) & (table_cap - 1);
                }
                if (!eliminated) {
                    insert = slot;
                    classes[in->result.v] = next_class++;
                    table[insert].used = true;
                    table[insert].hash = hash;
                    table[insert].key = key;
                    table[insert].leader = ir_op_value(f, in->result);
                    table[insert].block.v = bi + 1;
                }
            } else if (in->op == IR_LOAD && in->nops == 1 &&
                       !(in->flags & (IRF_VOLATILE | IRF_SEQ_CST))) {
                IrOperand found = {0};
                bool barrier = false, long_path = false;
                MemResult mr = find_load_value(
                    alias, f, dom, blocks, preds, bi, pos, in, replacement,
                    nold, may_undef, &found, &barrier, &long_path);

                if (mr == MEM_FOUND) {
                    replacement[in->result.v] = found;
                    if (found.kind == IROP_VALUE)
                        classes[in->result.v] = classes[found.a];
                    else
                        classes[in->result.v] = next_class++;
                    changed = true;
                } else {
                    classes[in->result.v] = next_class++;
                    if (barrier && !logged_barrier) {
                        OPT_BAIL(&fc, "gvn", "gvn_barrier");
                        logged_barrier = true;
                    } else if ((mr == MEM_BLOCKED || long_path) &&
                               !logged_store) {
                        OPT_BAIL(&fc, "gvn", "gvn_load_intervening_may_store");
                        logged_store = true;
                    }
                }
            } else {
                classes[in->result.v] = next_class++;
            }
        }
    }
    alias_free(alias);

    if (changed) {
        u32 bi;

        for (bi = 0; bi < f->nblocks; bi++) {
            IrBlock *block = &f->blocks[bi];
            IrInst *in = block->first;
            IrInst *prev = NULL;

            while (in) {
                IrInst *next = in->next;
                bool remove =
                    in->result.v && replacement[in->result.v].kind != IROP_NONE;
                u32 i, ei, ai;

                if (!remove) {
                    for (i = 0; i < in->nops; i++)
                        in->ops[i] = resolve_inst_operand(
                            in->ops[i], replacement, nold,
                            in->op == IR_CALL && in->ops[i].b != 0);
                    for (ei = 0; ei < in->nedges; ei++)
                        for (ai = 0; ai < in->edges[ei].nargs; ai++)
                            in->edges[ei].args[ai] = resolve_operand(
                                in->edges[ei].args[ai], replacement, nold);
                    prev = in;
                } else {
                    if (prev)
                        prev->next = next;
                    else
                        block->first = next;
                    if (block->last == in)
                        block->last = prev;
                    block->ninsts--;
                }
                in = next;
            }
        }
        ir_func_renumber(m->arena, f);
    }
    arena_free_all(&scratch);
    return changed;
}

bool opt_gvn(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= gvn_func(m, &m->funcs[i], cfg);
    return changed;
}
