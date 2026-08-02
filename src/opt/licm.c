#include "opt/opt.h"

#include <limits.h>
#include <string.h>

#include "opt/alias.h"
#include "util/arena.h"

typedef struct {
    bool div_not_nonzero;
    bool load_not_guaranteed;
    bool volatile_op;
    bool call;
    bool load_clobbered;
    bool sink_unsafe;
    bool irreducible;
} LicmBails;

typedef struct {
    IrInst *inst;
    BlockId from;
    BlockId to;
    bool sink;
} LicmMove;

bool opt_licm(IrModule *m, const OptConfig *cfg);
const Pass OPT_PASS_LICM = {"licm", opt_licm, PASS_PINNED_EXACT};

#define BAIL_ONCE(cfg, seen, reason)                                           \
    do {                                                                       \
        if (!*(seen)) {                                                        \
            OPT_BAIL((cfg), "licm", reason);                                   \
            *(seen) = true;                                                    \
        }                                                                      \
    } while (0)

static u64 scalar_size(IrType type)
{
    u64 size = ir_type_size(type);

    if (!size)
        CGF_ICE("licm: memory access has void type");
    return size;
}

static IrInst *value_inst(IrFunc *f, ValueId value)
{
    IrValInfo *vi;
    IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    vi = &f->vals[value.v - 1];
    if (vi->def_kind != VDEF_INST || !vi->def_block.v ||
        vi->def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[vi->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static u64 type_mask(IrType type);

static bool operand_is_zero(IrOperand op)
{
    return op.kind == IROP_ICONST && (op.a & type_mask((IrType)op.type)) == 0;
}

static bool checked_nonzero(IrFunc *f, const IrDomTree *dom, BlockId before,
                            IrOperand wanted)
{
    u32 bi;

    if (wanted.kind == IROP_ICONST)
        return (wanted.a & type_mask((IrType)wanted.type)) != 0;
    for (bi = 0; bi < f->nblocks; bi++) {
        BlockId check_block = {bi + 1};
        IrInst *term = f->blocks[bi].last;
        IrInst *cmp;
        bool compares_wanted_to_zero;
        u32 safe_edge;

        if (!ir_dominates(dom, check_block, before) || !term ||
            term->op != IR_CONDBR || term->nops != 1 || term->nedges != 2 ||
            term->ops[0].kind != IROP_VALUE)
            continue;
        cmp = value_inst(f, (ValueId){(u32)term->ops[0].a});
        if (!cmp || cmp->op != IR_ICMP || cmp->nops != 2)
            continue;
        compares_wanted_to_zero = (operand_equal(cmp->ops[0], wanted) &&
                                   operand_is_zero(cmp->ops[1])) ||
                                  (operand_equal(cmp->ops[1], wanted) &&
                                   operand_is_zero(cmp->ops[0]));
        if (!compares_wanted_to_zero)
            continue;
        if (cmp->subop == ICMP_NE)
            safe_edge = 0;
        else if (cmp->subop == ICMP_EQ)
            safe_edge = 1;
        else
            continue;
        if (ir_dominates(dom, term->edges[safe_edge].target, before))
            return true;
    }
    return false;
}

static u64 type_min_bits(IrType type)
{
    switch (type) {
    case IRT_I8:
        return UINT64_C(0x80);
    case IRT_I16:
        return UINT64_C(0x8000);
    case IRT_I32:
        return UINT64_C(0x80000000);
    case IRT_I64:
        return UINT64_C(0x8000000000000000);
    default:
        return 0;
    }
}

static u64 type_mask(IrType type)
{
    switch (type) {
    case IRT_I8:
        return UINT64_C(0xff);
    case IRT_I16:
        return UINT64_C(0xffff);
    case IRT_I32:
        return UINT64_C(0xffffffff);
    case IRT_I64:
        return UINT64_MAX;
    default:
        return 0;
    }
}

static bool signed_div_safe(const IrInst *in)
{
    u64 mask = type_mask((IrType)in->type);

    if (!mask)
        return false;
    if (in->ops[1].kind == IROP_ICONST && (in->ops[1].a & mask) != mask)
        return true; /* divisor cannot be -1 */
    return in->ops[0].kind == IROP_ICONST &&
           (in->ops[0].a & mask) != type_min_bits((IrType)in->type);
}

static bool pure_candidate(IrOp op)
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
    case IR_PTRADD:
    case IR_SELECT:
        return true;
    default:
        return false;
    }
}

static bool operand_invariant(const IrFunc *f, const Loop *loop, IrOperand op)
{
    IrValInfo vi;

    if (op.kind == IROP_UNDEF)
        return false;
    if (op.kind != IROP_VALUE)
        return true;
    if (!op.a || op.a > f->nvals)
        return false;
    vi = f->vals[op.a - 1];
    return !vi.def_block.v || !loop_contains(loop, vi.def_block);
}

static bool inst_operands_invariant(const IrFunc *f, const Loop *loop,
                                    const IrInst *in)
{
    u32 oi;

    for (oi = 0; oi < in->nops; oi++)
        if (!operand_invariant(f, loop, in->ops[oi]))
            return false;
    return true;
}

static bool global_dereferenceable(const IrModule *m, IrOperand ptr, u64 need)
{
    u32 gi;
    u64 off;

    if (ptr.kind != IROP_SYMBOL || ptr.type != IRT_PTR || ptr.sym >= m->nsyms ||
        (i64)ptr.a < 0)
        return false;
    off = ptr.a;
    for (gi = 0; gi < m->nglobals; gi++)
        if (strcmp(m->globals[gi].name, m->syms[ptr.sym]) == 0)
            return off <= m->globals[gi].size &&
                   need <= m->globals[gi].size - off;
    return false;
}

static bool pointer_dereferenceable(IrModule *m, IrFunc *f, IrOperand ptr,
                                    u64 need, u32 depth)
{
    IrInst *def;

    if (depth > f->nvals)
        return false;
    if (global_dereferenceable(m, ptr, need))
        return true;
    if (ptr.kind != IROP_VALUE)
        return false;
    def = value_inst(f, (ValueId){(u32)ptr.a});
    if (!def)
        return false;
    if (def->op == IR_ALLOCA && def->nops == 1 &&
        def->ops[0].kind == IROP_ICONST)
        return def->ops[0].a >= need;
    if (def->op == IR_PTRADD && def->nops == 2 &&
        def->ops[1].kind == IROP_ICONST && (i64)def->ops[1].a >= 0 &&
        def->ops[1].a <= UINT64_MAX - need)
        return pointer_dereferenceable(m, f, def->ops[0], need + def->ops[1].a,
                                       depth + 1);
    return false;
}

static bool load_executes_before_every_progress(const IrFunc *f,
                                                const IrDomTree *dom,
                                                const Loop *loop, BlockId block)
{
    u32 bi, ei, n = loop_exit_count(loop);
    bool saw_backedge = false;

    if (!n)
        return false;
    for (ei = 0; ei < n; ei++)
        if (!ir_dominates(dom, block, loop_exit_source(loop, ei)))
            return false;
    /* Exit dominance alone misses an infinite bypass: a path may keep
     * taking another backedge and never execute the load or exit.  Cover
     * every natural cycle inside this loop, including nested loops, by
     * requiring the load to dominate each backedge source. */
    for (bi = 0; bi < loop_block_count(loop); bi++) {
        BlockId source = loop_block(loop, bi);
        const IrInst *in;

        for (in = f->blocks[source.v - 1].first; in; in = in->next)
            for (ei = 0; ei < in->nedges; ei++) {
                BlockId target = in->edges[ei].target;

                if (!loop_contains(loop, target) ||
                    !ir_dominates(dom, target, source))
                    continue;
                saw_backedge = true;
                if (!ir_dominates(dom, block, source))
                    return false;
            }
    }
    return saw_backedge;
}

static bool write_loc(AliasCtx *alias, const IrInst *in, MemLoc *out)
{
    if (in->op == IR_STORE && in->nops == 2) {
        *out = alias_memloc(alias, in->ops[1],
                            scalar_size((IrType)in->ops[0].type),
                            (EffTypeId)in->subop);
        return true;
    }
    if ((in->op == IR_MEMCPY || in->op == IR_MEMSET) && in->nops == 3 &&
        in->ops[2].kind == IROP_ICONST) {
        *out = alias_memloc(alias, in->ops[0], in->ops[2].a, ETYPE_CHAR);
        return true;
    }
    return false;
}

static bool read_loc(AliasCtx *alias, const IrInst *in, MemLoc *out)
{
    if (in->op == IR_LOAD && in->nops == 1) {
        *out = alias_memloc(alias, in->ops[0], scalar_size((IrType)in->type),
                            (EffTypeId)in->subop);
        return true;
    }
    if (in->op == IR_MEMCPY && in->nops == 3 &&
        in->ops[2].kind == IROP_ICONST) {
        *out = alias_memloc(alias, in->ops[1], in->ops[2].a, ETYPE_CHAR);
        return true;
    }
    return false;
}

static bool memory_state_barrier(const IrInst *in)
{
    return in->op == IR_CALL || in->op == IR_ATOMICRMW ||
           in->op == IR_CMPXCHG || in->op == IR_VA_START ||
           in->op == IR_STACKSAVE || in->op == IR_STACKRESTORE;
}

static bool load_unclobbered(AliasCtx *alias, const IrFunc *f, const Loop *loop,
                             const IrInst *load)
{
    MemLoc wanted =
        alias_memloc(alias, load->ops[0], scalar_size((IrType)load->type),
                     (EffTypeId)load->subop);
    u32 bi;

    for (bi = 0; bi < loop_block_count(loop); bi++) {
        BlockId b = loop_block(loop, bi);
        const IrInst *in;

        for (in = f->blocks[b.v - 1].first; in; in = in->next) {
            MemLoc written;

            if (in == load)
                continue;
            if (memory_state_barrier(in))
                return false;
            if (in->op != IR_STORE && in->op != IR_MEMCPY &&
                in->op != IR_MEMSET)
                continue;
            if (!write_loc(alias, in, &written) ||
                alias_query(alias, wanted, written) != ALIAS_NO)
                return false;
        }
    }
    return true;
}

static bool store_sink_safe(AliasCtx *alias, const IrFunc *f,
                            const IrDomTree *dom, const Loop *loop,
                            BlockId block, const IrInst *store)
{
    MemLoc wanted;
    u32 bi;

    if (loop_exit_count(loop) != 1 ||
        !ir_dominates(dom, block, loop_exit_source(loop, 0)) ||
        !inst_operands_invariant(f, loop, store) ||
        !write_loc(alias, store, &wanted))
        return false;
    for (bi = 0; bi < loop_block_count(loop); bi++) {
        BlockId b = loop_block(loop, bi);
        const IrInst *in;

        for (in = f->blocks[b.v - 1].first; in; in = in->next) {
            MemLoc touched;

            if (in == store)
                continue;
            if (memory_state_barrier(in) ||
                (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)))
                return false;
            if (write_loc(alias, in, &touched) ||
                read_loc(alias, in, &touched)) {
                if (alias_query(alias, wanted, touched) != ALIAS_NO)
                    return false;
            } else if (in->op == IR_MEMCPY || in->op == IR_MEMSET) {
                return false; /* dynamic-size access: no finite MemLoc */
            }
        }
    }
    return true;
}

static u32 integer_width(IrType type)
{
    switch (type) {
    case IRT_I8:
        return 8;
    case IRT_I16:
        return 16;
    case IRT_I32:
        return 32;
    case IRT_I64:
        return 64;
    default:
        return 0;
    }
}

static bool eligible(IrModule *m, IrFunc *f, const IrDomTree *dom,
                     const Loop *loop, AliasCtx *alias, BlockId block,
                     IrInst *in, const OptConfig *cfg, LicmBails *bails,
                     BlockId *target, bool *sink)
{
    BlockId preheader = loop_preheader(loop);

    *target = preheader;
    *sink = false;

    if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) {
        BAIL_ONCE(cfg, &bails->volatile_op, "licm_volatile");
        return false;
    }
    if (in->op == IR_CALL) {
        BAIL_ONCE(cfg, &bails->call, "licm_call");
        return false;
    }
    if (in->op == IR_STORE) {
        if (store_sink_safe(alias, f, dom, loop, block, in)) {
            *target = loop_exit_target(loop, 0);
            *sink = true;
            return true;
        }
        BAIL_ONCE(cfg, &bails->sink_unsafe, "licm_sink_unsafe");
        return false;
    }
    if (in->op == IR_LOAD) {
        if (!inst_operands_invariant(f, loop, in))
            return false;
        if (!load_executes_before_every_progress(f, dom, loop, block) &&
            !pointer_dereferenceable(m, f, in->ops[0],
                                     scalar_size((IrType)in->type), 0)) {
            BAIL_ONCE(cfg, &bails->load_not_guaranteed,
                      "licm_load_not_guaranteed");
            return false;
        }
        if (!load_unclobbered(alias, f, loop, in)) {
            BAIL_ONCE(cfg, &bails->load_clobbered, "licm_load_clobbered");
            return false;
        }
        return true;
    }
    if (!pure_candidate((IrOp)in->op) || !in->result.v ||
        !inst_operands_invariant(f, loop, in))
        return false;
    if ((in->op == IR_SHL || in->op == IR_LSHR || in->op == IR_ASHR) &&
        (in->nops != 2 || in->ops[1].kind != IROP_ICONST ||
         in->ops[1].a >= integer_width((IrType)in->type)))
        return false;
    if (in->op == IR_SDIV || in->op == IR_UDIV || in->op == IR_SREM ||
        in->op == IR_UREM) {
        if (!checked_nonzero(f, dom, preheader, in->ops[1]) ||
            ((in->op == IR_SDIV || in->op == IR_SREM) &&
             !signed_div_safe(in))) {
            BAIL_ONCE(cfg, &bails->div_not_nonzero, "licm_div_not_nonzero");
            return false;
        }
    }
    return true;
}

static void move_before_terminator(IrFunc *f, BlockId from, BlockId to,
                                   IrInst *wanted)
{
    IrBlock *src = &f->blocks[from.v - 1];
    IrBlock *dst = &f->blocks[to.v - 1];
    IrInst *in = src->first, *prev = NULL;
    IrInst *term = dst->last, *term_prev = NULL;

    while (in && in != wanted) {
        prev = in;
        in = in->next;
    }
    if (!in)
        CGF_ICE("licm: instruction disappeared during hoist");
    if (prev)
        prev->next = in->next;
    else
        src->first = in->next;
    if (src->last == in)
        src->last = prev;
    src->ninsts--;

    if (!term || term->op < IR_RET || term->op > IR_UNREACHABLE)
        CGF_ICE("licm: preheader is not terminated");
    for (term_prev = dst->first; term_prev && term_prev->next != term;
         term_prev = term_prev->next)
        ;
    if (term_prev) {
        term_prev->next = in;
    } else {
        if (dst->first != term)
            CGF_ICE("licm: malformed preheader instruction list");
        dst->first = in;
    }
    in->next = term;
    dst->ninsts++;
}

static void move_to_block_front(IrFunc *f, BlockId from, BlockId to,
                                IrInst *wanted)
{
    IrBlock *src = &f->blocks[from.v - 1];
    IrBlock *dst = &f->blocks[to.v - 1];
    IrInst *in = src->first, *prev = NULL;

    while (in && in != wanted) {
        prev = in;
        in = in->next;
    }
    if (!in)
        CGF_ICE("licm: store disappeared during sink");
    if (prev)
        prev->next = in->next;
    else
        src->first = in->next;
    if (src->last == in)
        src->last = prev;
    src->ninsts--;
    in->next = dst->first;
    dst->first = in;
    if (!dst->last)
        dst->last = in;
    dst->ninsts++;
}

static bool run_loop(IrModule *m, IrFunc *f, const IrDomTree *dom,
                     const Loop *loop, const OptConfig *cfg, LicmBails *bails,
                     Arena *scratch)
{
    bool changed = false;
    u32 max_moves = 0, bi;

    for (bi = 0; bi < loop_block_count(loop); bi++)
        max_moves += f->blocks[loop_block(loop, bi).v - 1].ninsts;
    for (;;) {
        AliasConfig acfg = {
            .func = f,
            .no_strict_aliasing = cfg->no_strict_aliasing,
        };
        AliasCtx *alias = alias_build(m, &acfg);
        LicmMove *moves =
            arena_alloc(scratch, (max_moves ? max_moves : 1) * sizeof(*moves),
                        _Alignof(LicmMove));
        u32 nmoves = 0;

        for (bi = 0; bi < loop_block_count(loop); bi++) {
            BlockId b = loop_block(loop, bi);
            IrInst *in;

            for (in = f->blocks[b.v - 1].first; in; in = in->next) {
                BlockId target;
                bool sink;

                if (eligible(m, f, dom, loop, alias, b, in, cfg, bails, &target,
                             &sink))
                    moves[nmoves++] = (LicmMove){in, b, target, sink};
            }
        }
        alias_free(alias);
        if (!nmoves)
            break;
        /* Hoists append before the preheader terminator.  Sinks prepend to
         * the exit in reverse collection order, retaining deterministic
         * source order (safe candidates are pairwise NoAlias). */
        for (bi = 0; bi < nmoves; bi++)
            if (!moves[bi].sink)
                move_before_terminator(f, moves[bi].from, moves[bi].to,
                                       moves[bi].inst);
        for (bi = nmoves; bi-- > 0;)
            if (moves[bi].sink)
                move_to_block_front(f, moves[bi].from, moves[bi].to,
                                    moves[bi].inst);
        ir_func_renumber(m->arena, f);
        changed = true;
    }
    return changed;
}

static bool run_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;
    LicmBails bails = {0};
    bool changed = false;
    u32 count, depth, i, max_depth = 0;

    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    if (loop_tree_irreducible(tree)) {
        BAIL_ONCE(cfg, &bails.irreducible, "loop_irreducible");
        arena_free_all(&scratch);
        return false;
    }
    if (loop_canonicalize(m, f, tree)) {
        changed = true;
        ir_func_renumber(m->arena, f);
        arena_free_all(&scratch);
        arena_init(&scratch);
        dom = ir_domtree_build(&scratch, f);
        tree = loop_tree_build(&scratch, f, dom);
    }
    count = loop_tree_count(tree);
    for (i = 0; i < count; i++)
        if (loop_depth(loop_tree_at(tree, i)) > max_depth)
            max_depth = loop_depth(loop_tree_at(tree, i));
    for (depth = max_depth + 1; depth-- > 0;)
        for (i = 0; i < count; i++) {
            const Loop *loop = loop_tree_at(tree, i);

            if (loop_depth(loop) == depth)
                changed |= run_loop(m, f, dom, loop, cfg, &bails, &scratch);
        }
    if (cfg->verify_after_each) {
        char why[256];

        arena_free_all(&scratch);
        arena_init(&scratch);
        dom = ir_domtree_build(&scratch, f);
        tree = loop_tree_build(&scratch, f, dom);
        if (!loop_tree_verify_canonical(tree, f, why, sizeof(why)))
            CGF_ICE("licm: canonical loop verification failed: %s", why);
    }
    arena_free_all(&scratch);
    return changed;
}

bool opt_licm(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 fi;

    for (fi = 0; fi < m->nfuncs; fi++) {
        OptConfig fc = *cfg;

        if (opt_func_has_vector_ir(&m->funcs[fi]))
            continue;
        fc.current_func = m->funcs[fi].name;
        changed |= run_func(m, &m->funcs[fi], &fc);
    }
    return changed;
}
