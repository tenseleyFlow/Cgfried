#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"

typedef enum {
    BAIL_NONE,
    BAIL_ADDR_TAKEN,
    BAIL_VOLATILE_ACCESS,
    BAIL_NONSCALAR,
    BAIL_MIXED_ACCESS_TYPE,
} BailReason;

typedef struct {
    IrInst *inst;
    ValueId ptr;
    BlockId block;
    u32 ord;
    IrType type;
    u64 size;
    BailReason bail;
    bool promotable;
} AllocaInfo;

struct OptMem2RegInfo {
    UndefUse *uses;
    u32 nuses;
    u32 cap_uses;
};

const Pass OPT_PASS_MEM2REG = {"mem2reg", opt_mem2reg, PASS_PINNED_EXACT};

const UndefUse *opt_mem2reg_undef_log(const IrFunc *f, u32 *n)
{
    const struct OptMem2RegInfo *info = f ? f->opt_mem2reg_info : NULL;

    if (n)
        *n = info ? info->nuses : 0;
    return info ? info->uses : NULL;
}

static const IrLocalSlot *slot_for(const IrFunc *f, ValueId addr)
{
    u32 i;

    for (i = 0; i < f->nlocal_slots; i++)
        if (f->local_slots[i].addr.v == addr.v)
            return &f->local_slots[i];
    return NULL;
}

static void log_undef(IrModule *m, IrFunc *f, const AllocaInfo *alloca,
                      BlockId block, u32 loc, u8 classification,
                      Span decision_loc, u8 decision_kind,
                      u32 decision_predicate, bool self_init,
                      bool suppress_same_predicate, bool path_undecided)
{
    struct OptMem2RegInfo *info = f->opt_mem2reg_info;
    const IrLocalSlot *slot = slot_for(f, alloca->ptr);

    if (!info) {
        info = arena_alloc(m->arena, sizeof(*info),
                           _Alignof(struct OptMem2RegInfo));
        memset(info, 0, sizeof(*info));
        f->opt_mem2reg_info = info;
    }
    if (info->nuses == info->cap_uses) {
        u32 nc = info->cap_uses ? info->cap_uses * 2 : 8;
        UndefUse *nu =
            arena_alloc(m->arena, nc * sizeof(*nu), _Alignof(UndefUse));

        if (info->nuses)
            memcpy(nu, info->uses, info->nuses * sizeof(*nu));
        info->uses = nu;
        info->cap_uses = nc;
    }
    info->uses[info->nuses].alloca_ord = alloca->ord;
    info->uses[info->nuses].block = block;
    info->uses[info->nuses].loc = ir_debug_loc(m, loc);
    info->uses[info->nuses].name = slot ? slot->name : NULL;
    if (slot)
        info->uses[info->nuses].decl_loc = slot->decl_span;
    else
        memset(&info->uses[info->nuses].decl_loc, 0, sizeof(Span));
    info->uses[info->nuses].classification = classification;
    info->uses[info->nuses].decision_loc = decision_loc;
    info->uses[info->nuses].decision_kind = decision_kind;
    info->uses[info->nuses].decision_predicate = decision_predicate;
    info->uses[info->nuses].self_init = self_init;
    info->uses[info->nuses].suppress_same_predicate = suppress_same_predicate;
    info->uses[info->nuses].path_undecided = path_undecided;
    info->nuses++;
}

static bool operand_is_value(IrOperand op, ValueId v)
{
    return op.kind == IROP_VALUE && op.a == v.v;
}

static u64 scalar_size(IrType t)
{
    return ir_type_size(t);
}

static void note_access(AllocaInfo *a, IrType t, bool *have_type, bool *mixed)
{
    if (!*have_type) {
        a->type = t;
        *have_type = true;
    } else if (a->type != t) {
        *mixed = true;
    }
}

static BailReason analyze_alloca(const IrFunc *f, AllocaInfo *a)
{
    bool addr_taken = false;
    bool volatile_access = false;
    bool have_type = false;
    bool mixed = false;
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 oi, ei, ai;
            int direct_op = -1;

            if (in->op == IR_LOAD && in->nops >= 1 &&
                operand_is_value(in->ops[0], a->ptr)) {
                direct_op = 0;
                note_access(a, (IrType)in->type, &have_type, &mixed);
                if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                    volatile_access = true;
            } else if (in->op == IR_STORE && in->nops >= 2 &&
                       operand_is_value(in->ops[1], a->ptr)) {
                direct_op = 1;
                note_access(a, (IrType)in->ops[0].type, &have_type, &mixed);
                if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST))
                    volatile_access = true;
            } else if ((in->op == IR_ATOMICRMW || in->op == IR_CMPXCHG) &&
                       in->nops >= 1 && operand_is_value(in->ops[0], a->ptr)) {
                /* Atomic memory traffic is pinned by the same named
                 * non-ordinary-access bailout as volatile load/store. */
                direct_op = 0;
                volatile_access = true;
            }
            for (oi = 0; oi < in->nops; oi++)
                if ((int)oi != direct_op &&
                    operand_is_value(in->ops[oi], a->ptr))
                    addr_taken = true;
            for (ei = 0; ei < in->nedges; ei++)
                for (ai = 0; ai < in->edges[ei].nargs; ai++)
                    if (operand_is_value(in->edges[ei].args[ai], a->ptr))
                        addr_taken = true;
        }
    }
    if (addr_taken)
        return BAIL_ADDR_TAKEN;
    if (volatile_access)
        return BAIL_VOLATILE_ACCESS;
    if (mixed)
        return BAIL_MIXED_ACCESS_TYPE;
    if (a->block.v != 1 || a->inst->nops != 1 ||
        a->inst->ops[0].kind != IROP_ICONST || !have_type ||
        a->inst->ops[0].a == 0 || scalar_size(a->type) != a->inst->ops[0].a)
        return BAIL_NONSCALAR;
    a->size = a->inst->ops[0].a;
    return BAIL_NONE;
}

static void emit_bail(const OptConfig *cfg, BailReason reason)
{
    switch (reason) {
    case BAIL_ADDR_TAKEN:
        OPT_BAIL(cfg, "mem2reg", "addr_taken");
        break;
    case BAIL_VOLATILE_ACCESS:
        OPT_BAIL(cfg, "mem2reg", "volatile_access");
        break;
    case BAIL_NONSCALAR:
        OPT_BAIL(cfg, "mem2reg", "nonscalar");
        break;
    case BAIL_MIXED_ACCESS_TYPE:
        OPT_BAIL(cfg, "mem2reg", "mixed_access_type");
        break;
    case BAIL_NONE:
        break;
    }
}

static int alloca_for_ptr(const AllocaInfo *allocas, u32 nallocas, IrOperand op)
{
    u32 i;

    if (op.kind != IROP_VALUE)
        return -1;
    for (i = 0; i < nallocas; i++)
        if (allocas[i].promotable && allocas[i].ptr.v == op.a)
            return (int)i;
    return -1;
}

static IrOperand resolve_operand(IrOperand op, const IrOperand *replacement,
                                 u32 nold)
{
    u32 steps = 0;

    while (op.kind == IROP_VALUE && op.a >= 1 && op.a <= nold &&
           replacement[op.a].kind != IROP_NONE) {
        op = replacement[op.a];
        if (++steps > nold)
            CGF_ICE("mem2reg: cyclic value replacement");
    }
    return op;
}

static IrOperand resolve_inst_operand(IrOperand op,
                                      const IrOperand *replacement, u32 nold,
                                      bool call_operand)
{
    IrOperand old = op;

    op = resolve_operand(op, replacement, nold);
    if (call_operand)
        ir_arg_carry_provenance(&op, &old);
    return op;
}

enum { DS_UNDEF = 1u, DS_DEFINED = 2u };

typedef struct CfgWorkspace {
    struct CfgPred *preds;
    u32 *pred_offsets;
    bool *seen;
    bool *queued;
    u8 *state;
    u32 *work;
} CfgWorkspace;

typedef struct CfgPred {
    u32 block;
    u32 edge;
} CfgPred;

static bool block_has_noreturn_cut(const IrFunc *f, u32 block)
{
    const IrInst *in;

    for (in = f->blocks[block].first; in; in = in->next)
        if (in->op == IR_CALL && (in->flags & IRF_NORETURN))
            return true;
    return false;
}

static bool cfg_can_reach(const IrFunc *f, BlockId from, BlockId to,
                          CfgWorkspace *ws);

static bool find_decisive_branch(const IrModule *m, const IrFunc *f,
                                 BlockId undef_block, BlockId defined_block,
                                 CfgWorkspace *ws, Span *loc, u8 *kind,
                                 BlockId *decision_block, bool *undecided,
                                 u32 *predicates)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *term = f->blocks[bi].last;
        int undef_edge = -1;
        bool have_defined_edge = false;
        u32 ei;

        if (!term || (term->op != IR_CONDBR && term->op != IR_SWITCH))
            continue;
        if (++*predicates > 128) {
            *undecided = true;
            return false;
        }
        for (ei = 0; ei < term->nedges; ei++) {
            BlockId edge = term->edges[ei].target;
            bool reaches_undef;
            bool reaches_defined;

            if (!opt_cfg_edge_feasible(f, term, ei))
                continue;
            reaches_undef = cfg_can_reach(f, edge, undef_block, ws);
            reaches_defined = cfg_can_reach(f, edge, defined_block, ws);
            if (reaches_undef && !reaches_defined)
                undef_edge = (int)ei;
            if (reaches_defined && !reaches_undef)
                have_defined_edge = true;
        }
        if (undef_edge < 0 || !have_defined_edge)
            continue;
        *loc = ir_inst_span(m, term);
        if (term->op == IR_CONDBR)
            *kind = undef_edge == 0 ? 1u : 2u;
        else
            *kind = undef_edge == 0 ? 3u : 4u;
        decision_block->v = bi + 1;
        return true;
    }
    return false;
}

typedef struct PredicateKey {
    u32 kind; /* 1 function parameter, 2 source local, 3 parameter alloca */
    u32 id;
    bool negated;
} PredicateKey;

static bool predicate_key(const IrFunc *f, IrOperand op, PredicateKey *key,
                          u32 depth);

static bool predicate_is_single_entry_local(const IrFunc *f, PredicateKey key)
{
    u32 stores = 0;
    u32 bi;

    if (key.kind != 2)
        return false;
    for (bi = 0; bi < f->nblocks; bi++) {
        const IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == IR_STORE && in->nops >= 2 &&
                operand_is_value(in->ops[1], (ValueId){key.id})) {
                if (bi != 0 || ++stores > 1)
                    return false;
            }
    }
    return stores == 1;
}

static void witness_for_use(const IrModule *m, const IrFunc *f,
                            BlockId use_block, const u8 *in_state,
                            const u8 *out_state, CfgWorkspace *ws, Span *loc,
                            u8 *kind, u32 *predicate, BlockId *decision_block,
                            bool *undecided)
{
    u32 nwork = 0;
    u32 predicates = 0;

    memset(ws->seen, 0, f->nblocks * sizeof(*ws->seen));
    if (!use_block.v || use_block.v > f->nblocks)
        return;
    ws->seen[use_block.v - 1] = true;
    ws->work[nwork++] = use_block.v - 1;
    while (nwork) {
        u32 target = ws->work[--nwork];
        const IrInst *undef_term = NULL;
        u32 undef_edge = 0, undef_block = 0, defined_block = 0;
        bool have_undef = false, have_defined = false;
        u32 pi;

        for (pi = ws->pred_offsets[target]; pi < ws->pred_offsets[target + 1];
             pi++) {
            u32 bi = ws->preds[pi].block;
            const IrInst *term = f->blocks[bi].last;
            u32 ei = ws->preds[pi].edge;

            if (!term || block_has_noreturn_cut(f, bi))
                continue;
            if (term->op == IR_CONDBR && ++predicates > 128) {
                *undecided = true;
                return;
            }
            if (out_state[bi] == DS_UNDEF) {
                have_undef = true;
                undef_term = term;
                undef_edge = ei;
                undef_block = bi;
            } else if (out_state[bi] == DS_DEFINED) {
                have_defined = true;
                defined_block = bi;
            } else if (out_state[bi] == (DS_UNDEF | DS_DEFINED) &&
                       !ws->seen[bi]) {
                PredicateKey guard = {0};
                bool stable_guard = term->op == IR_CONDBR && term->nops == 1 &&
                                    predicate_key(f, term->ops[0], &guard, 0) &&
                                    (guard.kind == 1 || guard.kind == 3 ||
                                     predicate_is_single_entry_local(f, guard));

                /* Derived predicates and reassigned locals can carry path
                 * correlations this two-bit lattice cannot prove. A local
                 * copied once in entry is stable and remains eligible for
                 * the ordinary witness/same-predicate checks. */
                if (term->op == IR_CONDBR && !stable_guard)
                    *undecided = true;
                ws->seen[bi] = true;
                ws->work[nwork++] = bi;
            }
        }
        if (have_undef && have_defined && undef_term) {
            *loc = ir_inst_span(m, undef_term);
            if (undef_term->op == IR_CONDBR)
                *kind = undef_edge == 0 ? 1u : 2u;
            else if (undef_term->op == IR_SWITCH)
                *kind = undef_edge == 0 ? 3u : 4u;
            else
                (void)find_decisive_branch(m, f, (BlockId){undef_block + 1},
                                           (BlockId){defined_block + 1}, ws,
                                           loc, kind, decision_block, undecided,
                                           &predicates);
            if (undef_term->op == IR_CONDBR && undef_term->nops == 1 &&
                undef_term->ops[0].kind == IROP_VALUE &&
                undef_term->ops[0].a >= 1 &&
                f->vals[undef_term->ops[0].a - 1].def_kind == VDEF_FPARAM)
                *predicate = (u32)undef_term->ops[0].a;
            if (!decision_block->v && *kind)
                decision_block->v = undef_block + 1;
            return;
        }
        if (in_state[target] != (DS_UNDEF | DS_DEFINED))
            return;
    }
}

static const IrInst *inst_for_value(const IrFunc *f, u32 value)
{
    const IrValInfo *vi;
    const IrInst *in;

    if (!value || value > f->nvals)
        return NULL;
    vi = &f->vals[value - 1];
    if (vi->def_kind != VDEF_INST || !vi->def_block.v ||
        vi->def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[vi->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value)
            return in;
    return NULL;
}

static bool operand_zero(IrOperand op)
{
    return op.kind == IROP_ICONST && op.a == 0;
}

static bool predicate_key(const IrFunc *f, IrOperand op, PredicateKey *key,
                          u32 depth)
{
    const IrInst *def;

    if (op.kind != IROP_VALUE || op.a == 0 || op.a > f->nvals ||
        depth > f->nvals)
        return false;
    if (f->vals[op.a - 1].def_kind == VDEF_FPARAM) {
        key->kind = 1;
        key->id = (u32)op.a;
        return true;
    }
    def = inst_for_value(f, (u32)op.a);
    if (!def)
        return false;
    if (def->op == IR_LOAD && def->nops >= 1 &&
        def->ops[0].kind == IROP_VALUE) {
        const IrInst *addr = inst_for_value(f, (u32)def->ops[0].a);

        if (!addr || addr->op != IR_ALLOCA)
            return false;
        key->kind = slot_for(f, (ValueId){(u32)def->ops[0].a}) ? 2 : 3;
        key->id = (u32)def->ops[0].a;
        return true;
    }
    if (def->op == IR_ICMP && def->nops == 2 &&
        (def->subop == ICMP_EQ || def->subop == ICMP_NE)) {
        IrOperand base;

        if (operand_zero(def->ops[0]))
            base = def->ops[1];
        else if (operand_zero(def->ops[1]))
            base = def->ops[0];
        else
            return false;
        if (!predicate_key(f, base, key, depth + 1))
            return false;
        if (def->subop == ICMP_EQ)
            key->negated = !key->negated;
        return true;
    }
    if ((def->op == IR_TRUNC || def->op == IR_ZEXT || def->op == IR_SEXT) &&
        def->nops == 1)
        return predicate_key(f, def->ops[0], key, depth + 1);
    return false;
}

static bool cfg_can_reach(const IrFunc *f, BlockId from, BlockId to,
                          CfgWorkspace *ws)
{
    u32 nwork = 0;

    if (!from.v || from.v > f->nblocks || !to.v || to.v > f->nblocks)
        return false;
    memset(ws->seen, 0, f->nblocks * sizeof(*ws->seen));
    ws->seen[from.v - 1] = true;
    ws->work[nwork++] = from.v - 1;
    while (nwork) {
        u32 bi = ws->work[--nwork];
        const IrInst *term = f->blocks[bi].last;
        u32 ei;

        if (bi == to.v - 1)
            return true;
        if (!term || block_has_noreturn_cut(f, bi))
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;

            if (opt_cfg_edge_feasible(f, term, ei) && target &&
                target <= f->nblocks && !ws->seen[target - 1]) {
                ws->seen[target - 1] = true;
                ws->work[nwork++] = target - 1;
            }
        }
    }
    return false;
}

static bool predicate_reassigned(const IrFunc *f, PredicateKey key,
                                 BlockId decision, BlockId guard,
                                 CfgWorkspace *ws)
{
    const IrInst *term;
    u32 nwork = 0;
    u32 ei;

    if ((key.kind != 2 && key.kind != 3) || !decision.v ||
        decision.v > f->nblocks || !guard.v || guard.v > f->nblocks)
        return false;
    memset(ws->state, 0, f->nblocks * sizeof(*ws->state));
    memset(ws->queued, 0, f->nblocks * sizeof(*ws->queued));
    term = f->blocks[decision.v - 1].last;
    if (!term || block_has_noreturn_cut(f, decision.v - 1))
        return false;
    for (ei = 0; ei < term->nedges; ei++) {
        u32 target = term->edges[ei].target.v;

        if (!opt_cfg_edge_feasible(f, term, ei) || !target ||
            target > f->nblocks)
            continue;
        ws->state[target - 1] |= DS_UNDEF;
        if (!ws->queued[target - 1]) {
            ws->queued[target - 1] = true;
            ws->work[nwork++] = target - 1;
        }
    }
    while (nwork) {
        u32 bi = ws->work[--nwork];
        u8 current = ws->state[bi];
        const IrInst *in;

        ws->queued[bi] = false;
        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->op == IR_STORE && in->nops >= 2 &&
                operand_is_value(in->ops[1], (ValueId){key.id})) {
                current = DS_DEFINED;
                break;
            }
        if (bi + 1 == guard.v) {
            if (current & DS_DEFINED)
                return true;
            continue;
        }
        term = f->blocks[bi].last;
        if (!term || block_has_noreturn_cut(f, bi))
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;
            u8 incoming;

            if (!opt_cfg_edge_feasible(f, term, ei) || !target ||
                target > f->nblocks)
                continue;
            incoming = ws->state[target - 1] | current;
            if (incoming == ws->state[target - 1])
                continue;
            ws->state[target - 1] = incoming;
            if (!ws->queued[target - 1]) {
                ws->queued[target - 1] = true;
                ws->work[nwork++] = target - 1;
            }
        }
    }
    return false;
}

static bool suppress_same_predicate(const IrFunc *f, const IrDomTree *dom,
                                    BlockId decision, BlockId use,
                                    u8 decision_kind, CfgWorkspace *ws,
                                    bool *undecided)
{
    const IrInst *decision_term;
    PredicateKey original = {0};
    u32 bi;
    u32 predicates = 0;

    if (!decision.v || decision.v > f->nblocks || decision_kind < 1 ||
        decision_kind > 2)
        return false;
    decision_term = f->blocks[decision.v - 1].last;
    if (!decision_term || decision_term->op != IR_CONDBR ||
        decision_term->nops != 1 ||
        !predicate_key(f, decision_term->ops[0], &original, 0))
        return false;
    for (bi = 0; bi < f->nblocks; bi++) {
        BlockId guard = {bi + 1};
        const IrInst *term = f->blocks[bi].last;
        PredicateKey candidate = {0};
        bool true_reaches, false_reaches;
        u32 defined_edge, use_edge;

        if (term && term->op == IR_CONDBR && ++predicates > 128) {
            *undecided = true;
            return false;
        }
        if (guard.v == decision.v || !term || term->op != IR_CONDBR ||
            term->nedges != 2 || term->nops != 1 ||
            !ir_dominates(dom, decision, guard) ||
            !ir_dominates(dom, guard, use) ||
            !predicate_key(f, term->ops[0], &candidate, 0) ||
            candidate.kind != original.kind || candidate.id != original.id)
            continue;
        true_reaches = cfg_can_reach(f, term->edges[0].target, use, ws);
        false_reaches = cfg_can_reach(f, term->edges[1].target, use, ws);
        if (true_reaches == false_reaches)
            continue;
        use_edge = true_reaches ? 0u : 1u;
        defined_edge = ((u32)decision_kind - 1u) ^ 1u;
        if (candidate.negated != original.negated)
            defined_edge ^= 1u;
        if (use_edge == defined_edge &&
            !predicate_reassigned(f, original, decision, guard, ws))
            return true;
    }
    return false;
}

static void classify_alloca_uses(IrModule *m, IrFunc *f,
                                 const AllocaInfo *allocas, u32 nallocas,
                                 u32 nblocks, Arena *scratch)
{
    u8 *in_state = arena_alloc(scratch, nblocks ? nblocks : 1, 1);
    u8 *out_state = arena_alloc(scratch, nblocks ? nblocks : 1, 1);
    bool *queued = arena_alloc(scratch, nblocks ? nblocks : 1, 1);
    u32 *work = arena_alloc(scratch, (nblocks ? nblocks : 1) * sizeof(*work),
                            _Alignof(u32));
    IrDomTree *dom = ir_domtree_build(scratch, f);
    CfgWorkspace ws;
    u32 *pred_cursor;
    u32 npreds;
    u32 ai;

    ws.seen = arena_alloc(scratch, nblocks ? nblocks : 1, sizeof(*ws.seen));
    ws.queued = arena_alloc(scratch, nblocks ? nblocks : 1, sizeof(*ws.queued));
    ws.state = arena_alloc(scratch, nblocks ? nblocks : 1, sizeof(*ws.state));
    ws.work = arena_alloc(scratch, (nblocks ? nblocks : 1) * sizeof(*ws.work),
                          _Alignof(u32));
    ws.pred_offsets =
        arena_alloc(scratch, (nblocks + 1) * sizeof(u32), _Alignof(u32));
    memset(ws.pred_offsets, 0, (nblocks + 1) * sizeof(u32));
    for (ai = 0; ai < nblocks; ai++) {
        const IrInst *term = f->blocks[ai].last;
        u32 ei;

        if (!term || block_has_noreturn_cut(f, ai))
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;

            if (opt_cfg_edge_feasible(f, term, ei) && target &&
                target <= nblocks)
                ws.pred_offsets[target]++;
        }
    }
    for (ai = 1; ai <= nblocks; ai++)
        ws.pred_offsets[ai] += ws.pred_offsets[ai - 1];
    npreds = ws.pred_offsets[nblocks];
    ws.preds = arena_alloc(scratch, (npreds ? npreds : 1) * sizeof(*ws.preds),
                           _Alignof(CfgPred));
    pred_cursor = arena_alloc(scratch, (nblocks ? nblocks : 1) * sizeof(u32),
                              _Alignof(u32));
    if (nblocks)
        memcpy(pred_cursor, ws.pred_offsets, nblocks * sizeof(u32));
    for (ai = 0; ai < nblocks; ai++) {
        const IrInst *term = f->blocks[ai].last;
        u32 ei;

        if (!term || block_has_noreturn_cut(f, ai))
            continue;
        for (ei = 0; ei < term->nedges; ei++) {
            u32 target = term->edges[ei].target.v;
            u32 at;

            if (!opt_cfg_edge_feasible(f, term, ei) || !target ||
                target > nblocks)
                continue;
            at = pred_cursor[target - 1]++;
            ws.preds[at].block = ai;
            ws.preds[at].edge = ei;
        }
    }

    for (ai = 0; ai < nallocas; ai++) {
        u32 nwork = 0;
        u32 bi;

        if (!allocas[ai].promotable)
            continue;
        memset(in_state, 0, nblocks);
        memset(out_state, 0, nblocks);
        memset(queued, 0, nblocks);
        in_state[0] = DS_UNDEF;
        queued[0] = true;
        work[nwork++] = 0;
        while (nwork) {
            const IrInst *in;
            const IrInst *term;
            u8 current;
            u32 ei;

            bi = work[--nwork];
            queued[bi] = false;
            current = in_state[bi];
            for (in = f->blocks[bi].first; in; in = in->next)
                if (in->op == IR_CALL && (in->flags & IRF_NORETURN))
                    break;
                else if (in->op == IR_STORE && in->nops >= 2 &&
                         operand_is_value(in->ops[1], allocas[ai].ptr))
                    current = DS_DEFINED;
            if (current == out_state[bi])
                continue;
            out_state[bi] = current;
            term = f->blocks[bi].last;
            if (!term || block_has_noreturn_cut(f, bi))
                continue;
            for (ei = 0; ei < term->nedges; ei++) {
                u32 to = term->edges[ei].target.v;
                u8 incoming;

                if (!opt_cfg_edge_feasible(f, term, ei) || !to || to > nblocks)
                    continue;
                incoming = in_state[to - 1] | current;
                if (incoming == in_state[to - 1])
                    continue;
                in_state[to - 1] = incoming;
                if (!queued[to - 1]) {
                    queued[to - 1] = true;
                    work[nwork++] = to - 1;
                }
            }
        }

        for (bi = 0; bi < nblocks; bi++) {
            u8 current = in_state[bi];
            const IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                if (in->op == IR_CALL && (in->flags & IRF_NORETURN))
                    break;
                if (in->op == IR_LOAD && in->nops >= 1 &&
                    operand_is_value(in->ops[0], allocas[ai].ptr) &&
                    (current & DS_UNDEF)) {
                    Span decision = {0};
                    u8 decision_kind = 0;
                    u32 predicate = 0;
                    BlockId decision_block = BLOCK_INVALID;
                    bool same_predicate = false;
                    bool path_undecided = false;

                    if (current & DS_DEFINED) {
                        witness_for_use(m, f, (BlockId){bi + 1}, in_state,
                                        out_state, &ws, &decision,
                                        &decision_kind, &predicate,
                                        &decision_block, &path_undecided);
                        same_predicate = suppress_same_predicate(
                            f, dom, decision_block, (BlockId){bi + 1},
                            decision_kind, &ws, &path_undecided);
                    }
                    log_undef(m, f, &allocas[ai], (BlockId){bi + 1}, in->loc,
                              (current & DS_DEFINED) ? UNDEF_USE_MAYBE
                                                     : UNDEF_USE_DEFINITE,
                              decision, decision_kind, predicate,
                              (in->flags & IRF_SELF_INIT) != 0, same_predicate,
                              path_undecided);
                }
                if (in->op == IR_STORE && in->nops >= 2 &&
                    operand_is_value(in->ops[1], allocas[ai].ptr))
                    current = DS_DEFINED;
            }
        }
    }
}

static void append_phi_args(IrModule *m, IrEdge *edge,
                            const AllocaInfo *allocas, u32 nallocas,
                            const bool *phis, u32 nblocks,
                            const IrOperand *current)
{
    u32 target = edge->target.v - 1;
    u32 add = 0, i, at;
    IrOperand *args;

    for (i = 0; i < nallocas; i++)
        if (allocas[i].promotable && phis[i * nblocks + target])
            add++;
    if (!add)
        return;
    args = arena_alloc(m->arena, (edge->nargs + add) * sizeof(*args),
                       _Alignof(IrOperand));
    if (edge->nargs)
        memcpy(args, edge->args, edge->nargs * sizeof(*args));
    at = edge->nargs;
    for (i = 0; i < nallocas; i++) {
        IrOperand value;

        if (!allocas[i].promotable || !phis[i * nblocks + target])
            continue;
        value = current[i];
        if (value.kind == IROP_NONE)
            value = ir_op_undef(allocas[i].type);
        args[at++] = value;
    }
    edge->args = args;
    edge->nargs += add;
}

static bool promote_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    OptConfig fc = *cfg;
    AllocaInfo *allocas;
    u32 nallocas = 0, ai = 0, bi, i, j;
    u32 nblocks = f->nblocks;
    bool *pred, *df, *defs, *phis, *use_before_def, *live_in;
    u32 *pred_count, *idom, *order, *stack, *work;
    ValueId *phi_vals;
    IrOperand *replacement, *out_values;
    IrDomTree *dom;
    u32 old_nvals;
    bool changed = false;

    fc.current_func = f->name;
    if (f->calls_setjmp) {
        OPT_BAIL(&fc, "mem2reg", "setjmp_caller");
        return false;
    }
    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            if (in->op == IR_ALLOCA)
                nallocas++;
        }
    }
    if (!nallocas || !nblocks)
        return false;
    arena_init(&scratch);
    allocas = arena_alloc(&scratch, nallocas * sizeof(*allocas),
                          _Alignof(AllocaInfo));
    memset(allocas, 0, nallocas * sizeof(*allocas));
    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            if (in->op != IR_ALLOCA)
                continue;
            allocas[ai].inst = in;
            allocas[ai].ptr = in->result;
            allocas[ai].block.v = bi + 1;
            allocas[ai].ord = ai;
            ai++;
        }
    }
    for (i = 0; i < nallocas; i++) {
        allocas[i].bail = analyze_alloca(f, &allocas[i]);
        allocas[i].promotable = allocas[i].bail == BAIL_NONE;
        if (allocas[i].promotable)
            changed = true;
        else
            emit_bail(&fc, allocas[i].bail);
    }
    if (!changed) {
        arena_free_all(&scratch);
        return false;
    }

    if ((size_t)nblocks > SIZE_MAX / (size_t)nblocks ||
        (size_t)nallocas > SIZE_MAX / (size_t)nblocks)
        CGF_ICE("mem2reg: CFG too large");
    pred = arena_alloc(&scratch, (size_t)nblocks * nblocks, 1);
    df = arena_alloc(&scratch, (size_t)nblocks * nblocks, 1);
    defs = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    phis = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    use_before_def = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    live_in = arena_alloc(&scratch, (size_t)nallocas * nblocks, 1);
    phi_vals =
        arena_alloc(&scratch, (size_t)nallocas * nblocks * sizeof(*phi_vals),
                    _Alignof(ValueId));
    pred_count =
        arena_alloc(&scratch, nblocks * sizeof(*pred_count), _Alignof(u32));
    idom = arena_alloc(&scratch, nblocks * sizeof(*idom), _Alignof(u32));
    order = arena_alloc(&scratch, nblocks * sizeof(*order), _Alignof(u32));
    stack = arena_alloc(&scratch, nblocks * sizeof(*stack), _Alignof(u32));
    work = arena_alloc(&scratch, nblocks * sizeof(*work), _Alignof(u32));
    memset(pred, 0, (size_t)nblocks * nblocks);
    memset(df, 0, (size_t)nblocks * nblocks);
    memset(defs, 0, (size_t)nallocas * nblocks);
    memset(phis, 0, (size_t)nallocas * nblocks);
    memset(use_before_def, 0, (size_t)nallocas * nblocks);
    memset(live_in, 0, (size_t)nallocas * nblocks);
    memset(phi_vals, 0, (size_t)nallocas * nblocks * sizeof(*phi_vals));
    memset(pred_count, 0, nblocks * sizeof(*pred_count));

    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 ei;

            if (in->op == IR_STORE && in->nops >= 2) {
                int aidx = alloca_for_ptr(allocas, nallocas, in->ops[1]);

                if (aidx >= 0)
                    defs[(u32)aidx * nblocks + bi] = true;
            }
            for (ei = 0; ei < in->nedges; ei++) {
                u32 to = in->edges[ei].target.v - 1;

                if (!pred[bi * nblocks + to]) {
                    pred[bi * nblocks + to] = true;
                    pred_count[to]++;
                }
            }
        }
    }

    /* Classify reads while LOAD/STORE events and their original source
     * locations still exist.  A store initializes its destination regardless
     * of the value stored; following undefined SSA values would diagnose the
     * assignee instead of the read that produced the indeterminate value. */
    classify_alloca_uses(m, f, allocas, nallocas, nblocks, &scratch);

    /* Pruned SSA placement: a frontier gets a parameter only if the slot is
     * live on entry. Loads before the first local definition generate use;
     * any definition kills successor liveness. */
    for (ai = 0; ai < nallocas; ai++) {
        if (!allocas[ai].promotable)
            continue;
        for (bi = 0; bi < nblocks; bi++) {
            IrInst *in;
            bool seen_def = false;

            for (in = f->blocks[bi].first; in; in = in->next) {
                if (in->op == IR_STORE && in->nops >= 2 &&
                    operand_is_value(in->ops[1], allocas[ai].ptr))
                    seen_def = true;
                else if (in->op == IR_LOAD && in->nops >= 1 && !seen_def &&
                         operand_is_value(in->ops[0], allocas[ai].ptr))
                    use_before_def[(size_t)ai * nblocks + bi] = true;
            }
        }
    }
    {
        bool moved;

        do {
            moved = false;
            for (ai = 0; ai < nallocas; ai++) {
                if (!allocas[ai].promotable)
                    continue;
                for (bi = nblocks; bi-- > 0;) {
                    size_t at = (size_t)ai * nblocks + bi;
                    bool out = false;
                    bool next;
                    u32 succ;

                    for (succ = 0; succ < nblocks; succ++)
                        if (pred[bi * nblocks + succ] &&
                            live_in[(size_t)ai * nblocks + succ]) {
                            out = true;
                            break;
                        }
                    next = use_before_def[at] || (!defs[at] && out);
                    if (next != live_in[at]) {
                        live_in[at] = next;
                        moved = true;
                    }
                }
            }
        } while (moved);
    }

    dom = ir_domtree_build(&scratch, f);
    idom[0] = UINT32_MAX;
    for (bi = 1; bi < nblocks; bi++) {
        BlockId b = {(u32)(bi + 1)};
        BlockId p = ir_idom(dom, b);

        idom[bi] = p.v ? p.v - 1 : UINT32_MAX;
    }
    for (bi = 0; bi < nblocks; bi++) {
        u32 stop;

        if (pred_count[bi] < 2)
            continue;
        stop = idom[bi];
        for (i = 0; i < nblocks; i++) {
            u32 runner;

            if (!pred[i * nblocks + bi])
                continue;
            runner = i;
            while (runner != stop && runner != UINT32_MAX) {
                df[runner * nblocks + bi] = true;
                runner = idom[runner];
            }
        }
    }

    old_nvals = f->nvals;
    for (ai = 0; ai < nallocas; ai++) {
        bool *queued;
        u32 head = 0, tail = 0;

        if (!allocas[ai].promotable)
            continue;
        queued = arena_alloc(&scratch, nblocks, 1);
        memset(queued, 0, nblocks);
        for (bi = 0; bi < nblocks; bi++)
            if (defs[ai * nblocks + bi]) {
                work[tail++] = bi;
                queued[bi] = true;
            }
        while (head < tail) {
            u32 x = work[head++];

            for (bi = 0; bi < nblocks; bi++) {
                size_t at = (size_t)ai * nblocks + bi;

                if (!df[x * nblocks + bi] || !live_in[at] || phis[at])
                    continue;
                phis[at] = true;
                phi_vals[at] =
                    ir_block_param(m, f, (BlockId){bi + 1}, allocas[ai].type);
                if (!defs[at] && !queued[bi]) {
                    work[tail++] = bi;
                    queued[bi] = true;
                }
            }
        }
    }

    replacement = arena_alloc(&scratch, (old_nvals + 1) * sizeof(*replacement),
                              _Alignof(IrOperand));
    out_values =
        arena_alloc(&scratch, (size_t)nblocks * nallocas * sizeof(*out_values),
                    _Alignof(IrOperand));
    memset(replacement, 0, (old_nvals + 1) * sizeof(*replacement));
    memset(out_values, 0, (size_t)nblocks * nallocas * sizeof(*out_values));

    /* Deterministic iterative preorder: push children in reverse block order.
     */
    {
        u32 sp = 0, norder = 0;

        stack[sp++] = 0;
        while (sp) {
            u32 b = stack[--sp];

            order[norder++] = b;
            for (i = nblocks; i-- > 1;)
                if (idom[i] == b)
                    stack[sp++] = i;
        }
        if (norder != nblocks)
            CGF_ICE("mem2reg: dominator preorder missed a block");
    }

    for (i = 0; i < nblocks; i++) {
        u32 bidx = order[i];
        IrOperand *current = &out_values[(size_t)bidx * nallocas];
        IrBlock *blk = &f->blocks[bidx];
        IrInst *in = blk->first;
        IrInst *prev = NULL;

        if (idom[bidx] != UINT32_MAX)
            memcpy(current, &out_values[(size_t)idom[bidx] * nallocas],
                   nallocas * sizeof(*current));
        for (ai = 0; ai < nallocas; ai++)
            if (allocas[ai].promotable && phis[ai * nblocks + bidx])
                current[ai] =
                    ir_op_value(f, phi_vals[(size_t)ai * nblocks + bidx]);

        while (in) {
            IrInst *next = in->next;
            bool remove = false;
            int aidx = -1;
            u32 oi, ei, xi;

            for (oi = 0; oi < in->nops; oi++)
                in->ops[oi] = resolve_inst_operand(
                    in->ops[oi], replacement, old_nvals, in->op == IR_CALL);
            for (ei = 0; ei < in->nedges; ei++)
                for (xi = 0; xi < in->edges[ei].nargs; xi++)
                    in->edges[ei].args[xi] = resolve_operand(
                        in->edges[ei].args[xi], replacement, old_nvals);

            if (in->op == IR_ALLOCA) {
                for (ai = 0; ai < nallocas; ai++)
                    if (allocas[ai].promotable && allocas[ai].inst == in) {
                        remove = true;
                        break;
                    }
            } else if (in->op == IR_LOAD && in->nops >= 1) {
                aidx = alloca_for_ptr(allocas, nallocas, in->ops[0]);
                if (aidx >= 0) {
                    IrOperand value = current[aidx];

                    if (value.kind == IROP_NONE) {
                        value = ir_op_undef(allocas[aidx].type);
                    }
                    replacement[in->result.v] = value;
                    remove = true;
                }
            } else if (in->op == IR_STORE && in->nops >= 2) {
                aidx = alloca_for_ptr(allocas, nallocas, in->ops[1]);
                if (aidx >= 0) {
                    current[aidx] =
                        resolve_operand(in->ops[0], replacement, old_nvals);
                    remove = true;
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
        for (in = blk->first; in; in = in->next)
            for (j = 0; j < in->nedges; j++)
                append_phi_args(m, &in->edges[j], allocas, nallocas, phis,
                                nblocks, current);
    }

    /* Loads can feed uses in blocks visited later; one final transitive
     * rewrite covers every surviving operand before canonical renumbering. */
    for (bi = 0; bi < nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next) {
            u32 ei, xi;

            for (i = 0; i < in->nops; i++)
                in->ops[i] = resolve_inst_operand(in->ops[i], replacement,
                                                  old_nvals, in->op == IR_CALL);
            for (ei = 0; ei < in->nedges; ei++)
                for (xi = 0; xi < in->edges[ei].nargs; xi++)
                    in->edges[ei].args[xi] = resolve_operand(
                        in->edges[ei].args[xi], replacement, old_nvals);
        }
    }

    ir_func_renumber(m->arena, f);
    arena_free_all(&scratch);
    return true;
}

bool opt_mem2reg(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= promote_func(m, &m->funcs[i], cfg);
    return changed;
}
