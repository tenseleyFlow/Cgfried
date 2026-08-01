#include "opt/opt.h"

#include <string.h>

#include "opt/alias.h"
#include "opt/dep.h"

const Pass OPT_PASS_VECTORIZE = {"vectorize", opt_vectorize, PASS_PINNED_EXACT};

typedef struct VecAccess {
    const IrInst *inst;
    DepAccess dep;
    bool write;
} VecAccess;

typedef struct Reduction {
    bool active;
    u32 param;
    IrOp op;
    IrOp reduce_op;
    ValueId update;
    IrOperand contribution;
} Reduction;

typedef struct VecPlan {
    const Loop *loop;
    TripInfo trip;
    BlockId header_id;
    BlockId body_id;
    BlockId preheader_id;
    IrInst *header_term;
    IrInst *body_term;
    IrEdge *preedge;
    IrEdge *backedge;
    IrType elem_type;
    IrType vector_type;
    u32 lanes;
    u32 peel;
    IrInst **body_insts;
    u32 nbody;
    bool *address_value;
    Reduction *reductions;
} VecPlan;

static IrInst *value_inst(IrFunc *f, ValueId value)
{
    IrValInfo *vi;
    IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    vi = &f->vals[value.v - 1];
    if (vi->def_kind != VDEF_INST || !vi->def_block.v)
        return NULL;
    for (in = f->blocks[vi->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

static IrEdge *edge_to(IrFunc *f, BlockId from, BlockId to)
{
    IrInst *term;
    u32 i;

    if (!from.v || from.v > f->nblocks)
        return NULL;
    term = f->blocks[from.v - 1].last;
    for (i = 0; term && i < term->nedges; i++)
        if (term->edges[i].target.v == to.v)
            return &term->edges[i];
    return NULL;
}

bool opt_func_has_vector_ir(const IrFunc *f)
{
    u32 bi;

    for (bi = 0; bi < f->nblocks; bi++) {
        const IrBlock *b = &f->blocks[bi];
        const IrInst *in;
        u32 pi;

        for (pi = 0; pi < b->nparams; pi++)
            if (ir_type_is_vector(ir_value_type(f, b->params[pi])))
                return true;
        for (in = b->first; in; in = in->next) {
            u32 oi;

            if (ir_type_is_vector((IrType)in->type) || in->op == IR_VSPLAT ||
                in->op == IR_VEXTRACT ||
                (in->op >= IR_VREDUCE_ADD && in->op <= IR_VREDUCE_XOR))
                return true;
            for (oi = 0; oi < in->nops; oi++)
                if (ir_type_is_vector((IrType)in->ops[oi].type))
                    return true;
        }
    }
    return false;
}

bool opt_module_has_vector_ir(const IrModule *m)
{
    u32 fi;

    for (fi = 0; fi < m->nfuncs; fi++)
        if (opt_func_has_vector_ir(&m->funcs[fi]))
            return true;
    return false;
}

static IrType vector_type(IrType elem)
{
    switch (elem) {
    case IRT_I8:
        return IRT_V16I8;
    case IRT_I16:
        return IRT_V8I16;
    case IRT_I32:
        return IRT_V4I32;
    case IRT_I64:
        return IRT_V2I64;
    case IRT_F32:
        return IRT_V4F32;
    case IRT_F64:
        return IRT_V2F64;
    default:
        return IRT_VOID;
    }
}

static bool same_value(IrOperand op, ValueId value)
{
    return op.kind == IROP_VALUE && op.a == value.v;
}

static bool value_defined_in(const IrFunc *f, IrOperand op, BlockId block)
{
    return op.kind == IROP_VALUE && op.a && op.a <= f->nvals &&
           f->vals[op.a - 1].def_block.v == block.v;
}

static bool supported_reduction(IrOp op, IrType type, const OptConfig *cfg,
                                IrOp *reduce)
{
    switch (op) {
    case IR_IADD:
        *reduce = IR_VREDUCE_ADD;
        return type >= IRT_I8 && type <= IRT_I64;
    case IR_AND:
        *reduce = IR_VREDUCE_AND;
        return type >= IRT_I8 && type <= IRT_I64;
    case IR_OR:
        *reduce = IR_VREDUCE_OR;
        return type >= IRT_I8 && type <= IRT_I64;
    case IR_XOR:
        *reduce = IR_VREDUCE_XOR;
        return type >= IRT_I8 && type <= IRT_I64;
    case IR_FADD:
        *reduce = IR_VREDUCE_ADD;
        return (type == IRT_F32 || type == IRT_F64) && cfg->fast_math.reassoc;
    case IR_FMUL:
        *reduce = IR_VREDUCE_MUL;
        return (type == IRT_F32 || type == IRT_F64) && cfg->fast_math.reassoc;
    default:
        return false;
    }
}

static bool mark_address_value(IrFunc *f, BlockId body, IrOperand op,
                               bool *marked, u32 depth)
{
    IrInst *in;
    u32 i;

    if (op.kind != IROP_VALUE || !value_defined_in(f, op, body))
        return true;
    if (depth > 24 || op.a > f->nvals)
        return false;
    if (marked[op.a])
        return true;
    in = value_inst(f, (ValueId){(u32)op.a});
    if (!in || (in->op != IR_PTRADD && in->op != IR_IADD && in->op != IR_ISUB &&
                in->op != IR_IMUL && in->op != IR_SHL))
        return false;
    marked[op.a] = true;
    for (i = 0; i < in->nops; i++)
        if (!mark_address_value(f, body, in->ops[i], marked, depth + 1))
            return false;
    return true;
}

static bool op_vectorizable(IrOp op, IrType vec)
{
    if (ir_type_is_vector_int(vec)) {
        if (op == IR_IADD || op == IR_ISUB || op == IR_AND || op == IR_OR ||
            op == IR_XOR)
            return true;
        return op == IR_IMUL && vec == IRT_V8I16;
    }
    return ir_type_is_vector_float(vec) &&
           (op == IR_FADD || op == IR_FSUB || op == IR_FMUL || op == IR_FDIV);
}

static const char *analyze_body(IrModule *m, IrFunc *f, VecPlan *p,
                                const OptConfig *cfg, Arena *scratch)
{
    IrBlock *body = &f->blocks[p->body_id.v - 1];
    VecAccess *accesses;
    u32 naccess = 0, i, j;
    AliasConfig acfg;
    AliasCtx *alias;

    if (!body->last || body->last->op != IR_BR || body->last->nedges != 1 ||
        body->last->edges[0].target.v != p->header_id.v)
        return "vec_control_flow";
    p->body_term = body->last;
    p->nbody = body->ninsts - 1;
    if (p->nbody > 24)
        return "vec_cost";
    p->body_insts =
        arena_alloc(scratch, (p->nbody ? p->nbody : 1) * sizeof(*p->body_insts),
                    _Alignof(IrInst *));
    p->address_value =
        arena_alloc(scratch, (f->nvals + 1) * sizeof(bool), _Alignof(bool));
    memset(p->address_value, 0, (f->nvals + 1) * sizeof(bool));
    accesses =
        arena_alloc(scratch, (p->nbody ? p->nbody : 1) * sizeof(*accesses),
                    _Alignof(VecAccess));
    for (i = 0; i < p->nbody; i++)
        p->body_insts[i] = i ? p->body_insts[i - 1]->next : body->first;

    for (i = 0; i < p->nbody; i++) {
        IrInst *in = p->body_insts[i];
        IrOperand ptr;
        IrType type;
        u64 size;
        const char *why = NULL;

        if ((in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) || in->nedges ||
            in->op == IR_CALL || in->op == IR_ATOMICRMW || in->op == IR_CMPXCHG)
            return "vec_body_op";
        if (in->op != IR_LOAD && in->op != IR_STORE)
            continue;
        type = in->op == IR_LOAD ? (IrType)in->type : (IrType)in->ops[0].type;
        if (p->elem_type == IRT_VOID) {
            p->elem_type = type;
            p->vector_type = vector_type(type);
            if (p->vector_type == IRT_VOID)
                return "vec_body_op";
            p->lanes = ir_vector_lanes(p->vector_type);
        } else if (type != p->elem_type) {
            return "vec_stride";
        }
        size = ir_type_size(type);
        ptr = in->ops[in->op == IR_STORE ? 1 : 0];
        if (!mark_address_value(f, p->body_id, ptr, p->address_value, 0) ||
            !dep_access_from_ptr(f, ptr, p->trip.induction.iv, size,
                                 (EffTypeId)in->subop, &accesses[naccess].dep,
                                 &why) ||
            accesses[naccess].dep.stride != (i64)size)
            return "vec_stride";
        accesses[naccess].inst = in;
        accesses[naccess].write = in->op == IR_STORE;
        naccess++;
    }
    if (p->elem_type == IRT_VOID)
        return "vec_body_op";
    if (p->trip.constant < 2u * p->lanes)
        return "vec_cost";

    acfg.func = f;
    acfg.no_strict_aliasing = cfg->no_strict_aliasing;
    alias = alias_build(m, &acfg);
    for (i = 0; i < naccess; i++)
        for (j = i + 1; j < naccess; j++) {
            DepResult dep;

            if (!accesses[i].write && !accesses[j].write)
                continue;
            dep = dep_query(alias, accesses[i].dep, accesses[j].dep);
            if (dep.kind == DEP_UNKNOWN) {
                alias_free(alias);
                return "vec_alias_unproven";
            }
            if (dep.kind == DEP_DISTANCE && dep.distance != 0) {
                alias_free(alias);
                return "vec_loop_carried";
            }
        }
    alias_free(alias);
    return NULL;
}

static const char *analyze_reductions(IrFunc *f, VecPlan *p,
                                      const OptConfig *cfg, Arena *scratch)
{
    IrBlock *header = &f->blocks[p->header_id.v - 1];
    u32 pi;

    p->reductions = arena_alloc(scratch,
                                (header->nparams ? header->nparams : 1) *
                                    sizeof(*p->reductions),
                                _Alignof(Reduction));
    memset(p->reductions, 0, header->nparams * sizeof(*p->reductions));
    for (pi = 0; pi < header->nparams; pi++) {
        IrOperand next;
        IrInst *update;
        IrOperand contribution;
        IrOp reduce;

        if (pi == p->trip.induction.param_index)
            continue;
        next = p->backedge->args[pi];
        if (next.kind != IROP_VALUE ||
            !(update = value_inst(f, (ValueId){(u32)next.a})) ||
            update->nops != 2 || update->result.v != next.a ||
            update->type != ir_value_type(f, header->params[pi]))
            return "vec_body_op";
        if (same_value(update->ops[0], header->params[pi]))
            contribution = update->ops[1];
        else if (same_value(update->ops[1], header->params[pi]))
            contribution = update->ops[0];
        else
            return "vec_body_op";
        if ((update->op == IR_FADD || update->op == IR_FMUL) &&
            !cfg->fast_math.reassoc)
            return "vec_fp_reduction_needs_ofast";
        if (!supported_reduction((IrOp)update->op, (IrType)update->type, cfg,
                                 &reduce) ||
            (IrType)update->type != p->elem_type ||
            !value_defined_in(f, contribution, p->body_id))
            return "vec_body_op";
        p->reductions[pi].active = true;
        p->reductions[pi].param = pi;
        p->reductions[pi].op = (IrOp)update->op;
        p->reductions[pi].reduce_op = reduce;
        p->reductions[pi].update = update->result;
        p->reductions[pi].contribution = contribution;
    }
    return NULL;
}

static bool result_is_reduction(const VecPlan *p, const IrBlock *header,
                                ValueId result, u32 *which)
{
    u32 i;

    for (i = 0; i < header->nparams; i++)
        if (p->reductions[i].active && p->reductions[i].update.v == result.v) {
            *which = i;
            return true;
        }
    return false;
}

static bool operand_uses(IrOperand op, ValueId value)
{
    return op.kind == IROP_VALUE && op.a == value.v;
}

static bool reduction_uses_are_closed(const IrFunc *f, const VecPlan *p)
{
    const IrBlock *header = &f->blocks[p->header_id.v - 1];
    u32 pi, bi;

    for (pi = 0; pi < header->nparams; pi++) {
        const Reduction *r = &p->reductions[pi];

        if (!r->active)
            continue;
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                u32 oi, ei, ai;

                for (oi = 0; oi < in->nops; oi++)
                    if (operand_uses(in->ops[oi], header->params[pi]) &&
                        in->result.v != r->update.v)
                        return false;
                for (oi = 0; oi < in->nops; oi++)
                    if (operand_uses(in->ops[oi], r->update))
                        return false;
                for (ei = 0; ei < in->nedges; ei++)
                    for (ai = 0; ai < in->edges[ei].nargs; ai++) {
                        if (operand_uses(in->edges[ei].args[ai],
                                         header->params[pi]) &&
                            !(bi + 1 == p->header_id.v && in == p->header_term))
                            return false;
                        if (operand_uses(in->edges[ei].args[ai], r->update) &&
                            !(in == p->body_term &&
                              in->edges[ei].target.v == p->header_id.v &&
                              ai == r->param))
                            return false;
                    }
            }
        }
    }
    return true;
}

static bool body_values_do_not_escape(const IrFunc *f, const VecPlan *p)
{
    u32 i, bi;

    for (i = 0; i < p->nbody; i++) {
        ValueId value = p->body_insts[i]->result;

        if (!value.v)
            continue;
        for (bi = 0; bi < f->nblocks; bi++) {
            const IrInst *in;

            if (bi + 1 == p->body_id.v)
                continue;
            for (in = f->blocks[bi].first; in; in = in->next) {
                u32 oi, ei, ai;

                for (oi = 0; oi < in->nops; oi++)
                    if (operand_uses(in->ops[oi], value))
                        return false;
                for (ei = 0; ei < in->nedges; ei++)
                    for (ai = 0; ai < in->edges[ei].nargs; ai++)
                        if (operand_uses(in->edges[ei].args[ai], value))
                            return false;
            }
        }
    }
    return true;
}

static const char *analyze_loop(IrModule *m, IrFunc *f, const LoopTree *tree,
                                const Loop *loop, const OptConfig *cfg,
                                Arena *scratch, VecPlan *p)
{
    IrBlock *header;
    u32 i;
    const char *reason = NULL;

    memset(p, 0, sizeof(*p));
    p->elem_type = IRT_VOID;
    p->loop = loop;
    p->header_id = loop_header(loop);
    p->preheader_id = loop_preheader(loop);
    if (loop_block_count(loop) != 2 || loop_latch_count(loop) != 1 ||
        loop_exit_count(loop) != 1 || !p->preheader_id.v)
        return "vec_control_flow";
    p->body_id = loop_latch(loop, 0);
    if (p->body_id.v == p->header_id.v)
        return "vec_control_flow";
    for (i = 0; i < loop_tree_count(tree); i++)
        if (loop_parent(loop_tree_at(tree, i)) == loop)
            return "vec_control_flow";
    if (!loop_trip_analyze(f, loop, cfg->fwrapv, &p->trip, &reason) ||
        p->trip.kind != LOOP_TRIP_CONSTANT || p->trip.induction.subtract_step ||
        p->trip.induction.step.kind != IROP_ICONST ||
        p->trip.induction.step.a != 1)
        return "vec_trip_unknown";
    header = &f->blocks[p->header_id.v - 1];
    p->header_term = header->last;
    if (header->ninsts != 2 || !p->header_term ||
        p->header_term->op != IR_CONDBR)
        return "vec_control_flow";
    p->preedge = edge_to(f, p->preheader_id, p->header_id);
    p->backedge = edge_to(f, p->body_id, p->header_id);
    if (!p->preedge || !p->backedge || p->preedge->nargs != header->nparams ||
        p->backedge->nargs != header->nparams)
        return "vec_control_flow";
    reason = analyze_body(m, f, p, cfg, scratch);
    if (reason)
        return reason;
    p->peel = (u32)(p->trip.constant % p->lanes);
    reason = analyze_reductions(f, p, cfg, scratch);
    if (reason)
        return reason;
    if (!reduction_uses_are_closed(f, p) || !body_values_do_not_escape(f, p))
        return "vec_body_op";
    return NULL;
}

static IrOperand remap(IrOperand op, const IrOperand *map, u32 nmap)
{
    if (op.kind == IROP_VALUE && op.a < nmap && map[op.a].kind != IROP_NONE)
        return map[op.a];
    return op;
}

static ValueId clone_scalar(IrBuilder *b, const IrInst *in, IrOperand *map,
                            u32 nmap)
{
    IrOperand x = in->nops ? remap(in->ops[0], map, nmap) : (IrOperand){0};
    IrOperand y = in->nops > 1 ? remap(in->ops[1], map, nmap) : (IrOperand){0};
    ValueId result = VALUE_INVALID;

    ir_builder_set_span(b, ir_inst_span(b->m, in));
    switch (in->op) {
    case IR_PTRADD:
        result = ir_build_ptradd(b, x, y);
        break;
    case IR_LOAD:
        result = ir_build_load_typed(b, (IrType)in->type, x, in->align,
                                     in->flags, (EffTypeId)in->subop);
        break;
    case IR_STORE:
        ir_build_store_typed(b, x, y, in->align, in->flags,
                             (EffTypeId)in->subop);
        break;
    default:
        result =
            ir_build2_flags(b, (IrOp)in->op, (IrType)in->type, x, y, in->flags);
        break;
    }
    if (in->result.v)
        map[in->result.v] = ir_op_value(b->f, result);
    return result;
}

static void detach_term(IrBlock *block, IrInst *term)
{
    IrInst *in, *prev = NULL;

    for (in = block->first; in && in != term; in = in->next)
        prev = in;
    if (in != term)
        CGF_ICE("vectorize: lost terminator");
    if (prev)
        prev->next = NULL;
    else
        block->first = NULL;
    block->last = prev;
    block->ninsts--;
    term->next = NULL;
}

static void append_old_term(IrBlock *block, IrInst *term)
{
    if (block->last)
        block->last->next = term;
    else
        block->first = term;
    block->last = term;
    block->ninsts++;
}

static void emit_prefix(IrModule *m, IrFunc *f, VecPlan *p, IrOperand *map,
                        u32 nmap)
{
    IrBlock *header = &f->blocks[p->header_id.v - 1];
    IrBlock *preheader = &f->blocks[p->preheader_id.v - 1];
    IrInst *term = preheader->last;
    IrBuilder b;
    IrOperand *state;
    u32 copy, i;

    state = arena_alloc(
        m->arena, (header->nparams ? header->nparams : 1) * sizeof(*state),
        _Alignof(IrOperand));
    memset(map, 0, nmap * sizeof(*map));
    for (i = 0; i < header->nparams; i++)
        map[header->params[i].v] = p->preedge->args[i];
    map[p->trip.induction.compare.v] = ir_op_iconst(IRT_I32, 1);
    detach_term(preheader, term);
    ir_builder_at(&b, m, f, p->preheader_id);
    for (copy = 0; copy < p->peel; copy++) {
        for (i = 0; i < p->nbody; i++)
            (void)clone_scalar(&b, p->body_insts[i], map, nmap);
        for (i = 0; i < header->nparams; i++)
            state[i] = remap(p->backedge->args[i], map, nmap);
        for (i = 0; i < header->nparams; i++)
            map[header->params[i].v] = state[i];
    }
    if (p->peel) {
        IrOperand *args = arena_alloc(m->arena, header->nparams * sizeof(*args),
                                      _Alignof(IrOperand));

        for (i = 0; i < header->nparams; i++)
            args[i] = map[header->params[i].v];
        term->edges[0].args = args;
    }
    append_old_term(preheader, term);
}

static bool vector_operand_available(const IrFunc *f, const VecPlan *p,
                                     IrOperand old)
{
    IrInst *def;
    u32 ri;

    if ((IrType)old.type != p->elem_type)
        return false;
    if (old.kind != IROP_VALUE)
        return true;
    if (!old.a || old.a > f->nvals)
        return false;
    if (!loop_contains(p->loop, f->vals[old.a - 1].def_block))
        return true;
    def = value_inst((IrFunc *)f, (ValueId){(u32)old.a});
    if (!def || def->result.v == p->trip.induction.update.v ||
        result_is_reduction(p, &f->blocks[p->header_id.v - 1], def->result,
                            &ri) ||
        p->address_value[def->result.v])
        return false;
    return def->op == IR_LOAD || op_vectorizable((IrOp)def->op, p->vector_type);
}

static IrOperand vector_operand(IrBuilder *b, const VecPlan *p, IrOperand old,
                                IrOperand *vmap, u32 nmap)
{
    ValueId splat;

    if (old.kind == IROP_VALUE && old.a < nmap && vmap[old.a].kind != IROP_NONE)
        return vmap[old.a];
    if (!vector_operand_available(b->f, p, old))
        return (IrOperand){0};
    splat = ir_build_vsplat(b, p->vector_type, old);
    return ir_op_value(b->f, splat);
}

static void emit_vector_body(IrModule *m, IrFunc *f, VecPlan *p,
                             IrOperand *smap, IrOperand *vmap, u32 nmap)
{
    IrBlock *header = &f->blocks[p->header_id.v - 1];
    IrBlock *body = &f->blocks[p->body_id.v - 1];
    IrBuilder b;
    u32 i;

    body->first = body->last = NULL;
    body->ninsts = 0;
    memset(smap, 0, nmap * sizeof(*smap));
    memset(vmap, 0, nmap * sizeof(*vmap));
    for (i = 0; i < header->nparams; i++)
        smap[header->params[i].v] = ir_op_value(f, header->params[i]);
    ir_builder_at(&b, m, f, p->body_id);
    for (i = 0; i < p->nbody; i++) {
        IrInst *in = p->body_insts[i];
        u32 ri;

        ir_builder_set_span(&b, ir_inst_span(m, in));
        if (in->result.v == p->trip.induction.update.v) {
            IrOperand iv = ir_op_value(f, p->trip.induction.iv);
            ValueId next = ir_build2_flags(
                &b, IR_IADD, p->trip.induction.type, iv,
                ir_op_iconst(p->trip.induction.type, p->lanes), in->flags);
            smap[in->result.v] = ir_op_value(f, next);
            continue;
        }
        if (result_is_reduction(p, header, in->result, &ri)) {
            Reduction *r = &p->reductions[ri];
            IrOperand contribution =
                vector_operand(&b, p, r->contribution, vmap, nmap);
            ValueId reduced, combined;

            if (contribution.kind == IROP_NONE)
                CGF_ICE("vectorize: planned reduction lost vector input");
            reduced = ir_build_vreduce(&b, r->reduce_op, contribution);
            combined = ir_build2_flags(&b, r->op, p->elem_type,
                                       ir_op_value(f, header->params[r->param]),
                                       ir_op_value(f, reduced), 0);
            smap[in->result.v] = ir_op_value(f, combined);
            continue;
        }
        if (in->op == IR_PTRADD ||
            (in->result.v && p->address_value[in->result.v])) {
            (void)clone_scalar(&b, in, smap, nmap);
            continue;
        }
        if (in->op == IR_LOAD) {
            IrOperand ptr = remap(in->ops[0], smap, nmap);
            ValueId value =
                ir_build_load_typed(&b, p->vector_type, ptr, in->align,
                                    in->flags, (EffTypeId)in->subop);
            vmap[in->result.v] = ir_op_value(f, value);
            continue;
        }
        if (in->op == IR_STORE) {
            IrOperand value = vector_operand(&b, p, in->ops[0], vmap, nmap);
            IrOperand ptr = remap(in->ops[1], smap, nmap);

            if (value.kind == IROP_NONE)
                CGF_ICE("vectorize: planned store lost vector input");
            ir_build_store_typed(&b, value, ptr, in->align, in->flags,
                                 (EffTypeId)in->subop);
            continue;
        }
        if (op_vectorizable((IrOp)in->op, p->vector_type)) {
            IrOperand x = vector_operand(&b, p, in->ops[0], vmap, nmap);
            IrOperand y = vector_operand(&b, p, in->ops[1], vmap, nmap);
            ValueId value;

            if (x.kind == IROP_NONE || y.kind == IROP_NONE)
                CGF_ICE("vectorize: planned arithmetic lost vector input");
            value = ir_build2_flags(&b, (IrOp)in->op, p->vector_type, x, y,
                                    in->flags & (u8)~IRF_NSW);
            vmap[in->result.v] = ir_op_value(f, value);
            continue;
        }
        CGF_ICE("vectorize: unsupported body opcode passed planning");
    }
    {
        IrOperand *args = arena_alloc(m->arena, header->nparams * sizeof(*args),
                                      _Alignof(IrOperand));

        for (i = 0; i < header->nparams; i++)
            args[i] = remap(p->backedge->args[i], smap, nmap);
        ir_build_br(&b, p->header_id, args, header->nparams);
    }
}

static bool validate_body_ops(IrFunc *f, VecPlan *p)
{
    IrBlock *header = &f->blocks[p->header_id.v - 1];
    u32 i;

    for (i = 0; i < p->nbody; i++) {
        IrInst *in = p->body_insts[i];
        u32 ri;

        if (in->result.v == p->trip.induction.update.v || in->op == IR_LOAD ||
            in->op == IR_PTRADD ||
            (in->result.v && p->address_value[in->result.v]))
            continue;
        if (in->op == IR_STORE) {
            if (!vector_operand_available(f, p, in->ops[0]))
                return false;
            continue;
        }
        if (result_is_reduction(p, header, in->result, &ri)) {
            if (!vector_operand_available(f, p, p->reductions[ri].contribution))
                return false;
            continue;
        }
        if (!op_vectorizable((IrOp)in->op, p->vector_type) ||
            (IrType)in->type != p->elem_type ||
            !vector_operand_available(f, p, in->ops[0]) ||
            !vector_operand_available(f, p, in->ops[1]))
            return false;
    }
    return true;
}

static void commit_plan(IrModule *m, IrFunc *f, VecPlan *p)
{
    u32 nmap = f->nvals + 1;
    IrOperand *smap =
        arena_alloc(m->arena, nmap * sizeof(*smap), _Alignof(IrOperand));
    IrOperand *vmap =
        arena_alloc(m->arena, nmap * sizeof(*vmap), _Alignof(IrOperand));

    emit_prefix(m, f, p, smap, nmap);
    emit_vector_body(m, f, p, smap, vmap, nmap);
    ir_func_renumber(m->arena, f);
}

static void log_reason(const OptConfig *cfg, const char *reason)
{
    if (strcmp(reason, "vec_trip_unknown") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_trip_unknown");
    else if (strcmp(reason, "vec_cost") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_cost");
    else if (strcmp(reason, "vec_stride") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_stride");
    else if (strcmp(reason, "vec_alias_unproven") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_alias_unproven");
    else if (strcmp(reason, "vec_loop_carried") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_loop_carried");
    else if (strcmp(reason, "vec_fp_reduction_needs_ofast") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_fp_reduction_needs_ofast");
    else if (strcmp(reason, "vec_control_flow") == 0)
        OPT_BAIL(cfg, "vectorize", "vec_control_flow");
    else
        OPT_BAIL(cfg, "vectorize", "vec_body_op");
}

static bool vectorize_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    IrDomTree *dom;
    LoopTree *tree;
    u32 i, max_depth = 0;
    bool changed = false;

    arena_init(&scratch);
    dom = ir_domtree_build(&scratch, f);
    tree = loop_tree_build(&scratch, f, dom);
    if (loop_tree_irreducible(tree)) {
        OPT_BAIL(cfg, "vectorize", "vec_control_flow");
        arena_free_all(&scratch);
        return false;
    }
    /* Source lowering leaves loop live-outs in ordinary SSA form.  Build
     * dedicated exits and LCSSA before reduction matching, just as the later
     * loop pipeline would; reparsing emitted IR must not be a prerequisite
     * for source-level vectorization. */
    if (loop_canonicalize(m, f, tree)) {
        changed = true;
        arena_free_all(&scratch);
        arena_init(&scratch);
        dom = ir_domtree_build(&scratch, f);
        tree = loop_tree_build(&scratch, f, dom);
    }
    for (i = 0; i < loop_tree_count(tree); i++)
        if (loop_depth(loop_tree_at(tree, i)) > max_depth)
            max_depth = loop_depth(loop_tree_at(tree, i));
    if (loop_tree_count(tree))
        for (;;) {
            for (i = 0; i < loop_tree_count(tree); i++) {
                const Loop *loop = loop_tree_at(tree, i);
                VecPlan plan;
                const char *reason;

                if (loop_depth(loop) != max_depth)
                    continue;
                reason = analyze_loop(m, f, tree, loop, cfg, &scratch, &plan);
                if (reason) {
                    log_reason(cfg, reason);
                    continue;
                }
                if (!validate_body_ops(f, &plan)) {
                    OPT_BAIL(cfg, "vectorize", "vec_body_op");
                    continue;
                }
                commit_plan(m, f, &plan);
                changed = true;
                break;
            }
            if (changed || max_depth == 0)
                break;
            max_depth--;
        }
    arena_free_all(&scratch);
    return changed;
}

bool opt_vectorize(IrModule *m, const OptConfig *cfg)
{
    u32 fi;
    bool changed = false;

    if (cfg->disable_vectorize ||
        (cfg->level != OPT_O3 && cfg->level != OPT_OFAST))
        return false;
    for (fi = 0; fi < m->nfuncs; fi++) {
        OptConfig fc = *cfg;
        bool func_changed = false;

        fc.current_func = m->funcs[fi].name;
        if (opt_func_has_vector_ir(&m->funcs[fi]))
            continue;
        while (vectorize_func(m, &m->funcs[fi], &fc))
            func_changed = true;
        changed |= func_changed;
    }
    return changed;
}
