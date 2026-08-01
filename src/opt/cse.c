#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

typedef struct {
    u8 op;
    u8 type;
    u8 subop;
    u8 flags;
    u32 nops;
    IrOperand ops[3];
    IrOperand value;
} CseEntry;

typedef struct {
    u64 hash;
    bool used;
    CseEntry entry;
} CseSlot;

const Pass OPT_PASS_CSE = {"cse", opt_cse, PASS_PINNED_EXACT};

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

static bool is_candidate(IrOp op)
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

static bool operand_less(IrOperand a, IrOperand b)
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

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
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

static IrOperand resolve_operand(IrOperand op, const IrOperand *replacement,
                                 u32 nold)
{
    u32 steps = 0;

    while (op.kind == IROP_VALUE && op.a >= 1 && op.a <= nold &&
           replacement[op.a].kind != IROP_NONE) {
        op = replacement[op.a];
        if (++steps > nold)
            CGF_ICE("cse: cyclic value replacement");
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

static void make_key(CseEntry *key, const IrInst *in)
{
    u32 i;

    memset(key, 0, sizeof(*key));
    key->op = in->op;
    key->type = in->type;
    key->subop = in->subop;
    key->flags = in->flags;
    key->nops = in->nops;
    for (i = 0; i < in->nops; i++)
        key->ops[i] = in->ops[i];
    if (key->nops == 2 && commutative_key(in) &&
        operand_less(key->ops[1], key->ops[0])) {
        IrOperand tmp = key->ops[0];

        key->ops[0] = key->ops[1];
        key->ops[1] = tmp;
    }
}

static bool key_equal(const CseEntry *a, const CseEntry *b)
{
    u32 i;

    if (a->op != b->op || a->type != b->type || a->subop != b->subop ||
        a->flags != b->flags || a->nops != b->nops)
        return false;
    for (i = 0; i < a->nops; i++)
        if (!operand_equal(a->ops[i], b->ops[i]))
            return false;
    return true;
}

static u64 hash_mix(u64 hash, u64 value)
{
    hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
    return hash;
}

static u64 operand_hash(u64 hash, IrOperand op)
{
    hash = hash_mix(hash, op.kind);
    hash = hash_mix(hash, op.type);
    hash = hash_mix(hash, op.sym);
    hash = hash_mix(hash, op.a);
    return hash_mix(hash, op.b);
}

static u64 key_hash(const CseEntry *key)
{
    u64 hash = 0xcbf29ce484222325ull;
    u32 i;

    hash = hash_mix(hash, key->op);
    hash = hash_mix(hash, key->type);
    hash = hash_mix(hash, key->subop);
    hash = hash_mix(hash, key->flags);
    hash = hash_mix(hash, key->nops);
    for (i = 0; i < key->nops; i++)
        hash = operand_hash(hash, key->ops[i]);
    return hash;
}

static bool cse_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    OptConfig fc = *cfg;
    IrOperand *replacement;
    bool *may_undef;
    u32 nold = f->nvals;
    u32 bi;
    bool changed = false;
    bool load_bailed = false;

    fc.current_func = f->name;
    arena_init(&scratch);
    replacement = arena_alloc(&scratch, (nold + 1) * sizeof(*replacement),
                              _Alignof(IrOperand));
    may_undef = arena_alloc(&scratch, (nold + 1) * sizeof(*may_undef), 1);
    memset(replacement, 0, (nold + 1) * sizeof(*replacement));
    memset(may_undef, 0, (nold + 1) * sizeof(*may_undef));
    find_may_undef(f, may_undef);

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *blk = &f->blocks[bi];
        size_t table_cap = 8;
        CseSlot *table;
        IrInst *in = blk->first;
        IrInst *prev = NULL;

        while (table_cap < (size_t)blk->ninsts * 2) {
            if (table_cap > SIZE_MAX / 2)
                CGF_ICE("cse: expression table capacity overflow");
            table_cap *= 2;
        }
        table = arena_alloc(&scratch, table_cap * sizeof(*table),
                            _Alignof(CseSlot));
        memset(table, 0, table_cap * sizeof(*table));

        while (in) {
            IrInst *next = in->next;
            bool remove = false;
            u32 oi;

            for (oi = 0; oi < in->nops; oi++)
                in->ops[oi] = resolve_inst_operand(
                    in->ops[oi], replacement, nold,
                    in->op == IR_CALL && in->ops[oi].b != 0);

            if (in->op == IR_LOAD && !load_bailed) {
                OPT_BAIL(&fc, "cse", "load_requires_alias");
                load_bailed = true;
            }
            if (is_candidate((IrOp)in->op)) {
                bool has_undef = may_undef[in->result.v];
                CseEntry key;

                if (!in->result.v || in->nops > 3)
                    CGF_ICE("cse: malformed candidate instruction");
                for (oi = 0; oi < in->nops; oi++)
                    if (operand_may_undef(in->ops[oi], may_undef, nold)) {
                        has_undef = true;
                        break;
                    }
                if (!has_undef) {
                    u64 hash;
                    size_t slot;

                    make_key(&key, in);
                    hash = key_hash(&key);
                    slot = (size_t)hash & (table_cap - 1);
                    while (table[slot].used) {
                        if (table[slot].hash == hash &&
                            key_equal(&table[slot].entry, &key)) {
                            replacement[in->result.v] = table[slot].entry.value;
                            remove = true;
                            changed = true;
                            break;
                        }
                        slot = (slot + 1) & (table_cap - 1);
                    }
                    if (!remove) {
                        key.value = ir_op_value(f, in->result);
                        table[slot].used = true;
                        table[slot].hash = hash;
                        table[slot].entry = key;
                    }
                }
            }

            if (remove) {
                if (prev)
                    prev->next = next;
                else
                    blk->first = next;
                if (blk->last == in)
                    blk->last = prev;
                blk->ninsts--;
            } else {
                prev = in;
            }
            in = next;
        }
    }

    if (changed) {
        for (bi = 0; bi < f->nblocks; bi++) {
            IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                u32 oi, ei, ai;

                for (oi = 0; oi < in->nops; oi++)
                    in->ops[oi] = resolve_inst_operand(
                        in->ops[oi], replacement, nold,
                        in->op == IR_CALL && in->ops[oi].b != 0);
                for (ei = 0; ei < in->nedges; ei++)
                    for (ai = 0; ai < in->edges[ei].nargs; ai++)
                        in->edges[ei].args[ai] = resolve_operand(
                            in->edges[ei].args[ai], replacement, nold);
            }
        }
        ir_func_renumber(m->arena, f);
    }
    arena_free_all(&scratch);
    return changed;
}

bool opt_cse(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= cse_func(m, &m->funcs[i], cfg);
    return changed;
}
