#include "opt/opt.h"

#include <stdio.h>
#include <string.h>

static bool value_is_param(const IrBlock *block, IrOperand op, u32 *ordinal)
{
    u32 i;

    if (op.kind != IROP_VALUE)
        return false;
    for (i = 0; i < block->nparams; i++)
        if (block->params[i].v == op.a) {
            *ordinal = i;
            return true;
        }
    return false;
}

static const IrInst *value_inst(const IrFunc *f, ValueId value)
{
    const IrValInfo *info;
    const IrInst *in;

    if (!value.v || value.v > f->nvals)
        return NULL;
    info = &f->vals[value.v - 1];
    if (info->def_kind != VDEF_INST || !info->def_block.v ||
        info->def_block.v > f->nblocks)
        return NULL;
    for (in = f->blocks[info->def_block.v - 1].first; in; in = in->next)
        if (in->result.v == value.v)
            return in;
    return NULL;
}

bool loop_operand_invariant(const IrFunc *f, const Loop *loop, IrOperand op)
{
    const IrValInfo *info;

    if (op.kind != IROP_VALUE)
        return op.kind != IROP_NONE && op.kind != IROP_UNDEF;
    if (!op.a || op.a > f->nvals)
        return false;
    info = &f->vals[op.a - 1];
    if (info->def_kind == VDEF_FPARAM)
        return true;
    if (info->def_kind == VDEF_NONE || !info->def_block.v)
        return false;
    return !loop_contains(loop, info->def_block);
}

static IrIcmp swap_pred(IrIcmp pred)
{
    static const IrIcmp swapped[] = {
        ICMP_EQ,  ICMP_NE,  ICMP_SGT, ICMP_SGE, ICMP_SLT,
        ICMP_SLE, ICMP_UGT, ICMP_UGE, ICMP_ULT, ICMP_ULE,
    };

    return swapped[pred];
}

static IrIcmp invert_pred(IrIcmp pred)
{
    static const IrIcmp inverted[] = {
        ICMP_NE,  ICMP_EQ,  ICMP_SGE, ICMP_SGT, ICMP_SLE,
        ICMP_SLT, ICMP_UGE, ICMP_UGT, ICMP_ULE, ICMP_ULT,
    };

    return inverted[pred];
}

static const IrEdge *edge_to(const IrFunc *f, BlockId source, BlockId target)
{
    const IrBlock *block;
    const IrInst *in;
    const IrEdge *found = NULL;
    u32 ei;

    if (!source.v || source.v > f->nblocks)
        return NULL;
    block = &f->blocks[source.v - 1];
    for (in = block->first; in; in = in->next)
        for (ei = 0; ei < in->nedges; ei++)
            if (in->edges[ei].target.v == target.v) {
                if (found)
                    return NULL;
                found = &in->edges[ei];
            }
    return found;
}

static bool recurrence(const IrFunc *f, const IrBlock *header, IrOperand next,
                       u32 ordinal, const IrInst **update, IrOperand *step,
                       bool *subtract)
{
    const IrInst *in;
    u32 base_ordinal;

    if (next.kind != IROP_VALUE)
        return false;
    in = value_inst(f, (ValueId){(u32)next.a});
    if (!in || (in->op != IR_IADD && in->op != IR_ISUB) || in->nops != 2)
        return false;
    if (value_is_param(header, in->ops[0], &base_ordinal) &&
        base_ordinal == ordinal && in->ops[1].kind == IROP_ICONST) {
        *step = in->ops[1];
    } else if (in->op == IR_IADD &&
               value_is_param(header, in->ops[1], &base_ordinal) &&
               base_ordinal == ordinal && in->ops[0].kind == IROP_ICONST) {
        *step = in->ops[0];
    } else {
        return false;
    }
    *update = in;
    *subtract = in->op == IR_ISUB;
    return true;
}

bool loop_trip_analyze(const IrFunc *f, const Loop *loop, bool fwrapv,
                       TripInfo *out, const char **reason)
{
    BlockId header_id, preheader_id, latch_id;
    const IrBlock *header;
    const IrInst *term, *compare, *update;
    const IrEdge *preedge, *backedge;
    IrOperand iv, bound, step;
    IrIcmp pred;
    u32 ordinal, i, inside = UINT32_MAX;
    bool subtract;
    bool direct_iv;
    u64 effective_step;

    if (reason)
        *reason = NULL;
    if (!out)
        return false;
    memset(out, 0, sizeof(*out));
    out->kind = LOOP_TRIP_UNKNOWN;
    if (!f || !loop) {
        if (reason)
            *reason = "trip_shape";
        return false;
    }
    header_id = loop_header(loop);
    preheader_id = loop_preheader(loop);
    if (!preheader_id.v) {
        if (reason)
            *reason = "trip_no_preheader";
        return false;
    }
    if (loop_latch_count(loop) != 1) {
        if (reason)
            *reason = "trip_multi_latch";
        return false;
    }
    latch_id = loop_latch(loop, 0);
    if (!header_id.v || header_id.v > f->nblocks ||
        preheader_id.v > f->nblocks || latch_id.v > f->nblocks) {
        if (reason)
            *reason = "trip_shape";
        return false;
    }
    header = &f->blocks[header_id.v - 1];
    preedge = edge_to(f, preheader_id, header_id);
    backedge = edge_to(f, latch_id, header_id);
    term = header->last;
    if (!preedge || !backedge || preedge->nargs != header->nparams ||
        backedge->nargs != header->nparams || !term || term->op != IR_CONDBR ||
        term->nops != 1 || term->nedges != 2 ||
        term->ops[0].kind != IROP_VALUE) {
        if (reason)
            *reason = "trip_shape";
        return false;
    }
    for (i = 0; i < 2; i++)
        if (loop_contains(loop, term->edges[i].target)) {
            if (inside != UINT32_MAX) {
                if (reason)
                    *reason = "trip_shape";
                return false;
            }
            inside = i;
        }
    if (inside == UINT32_MAX) {
        if (reason)
            *reason = "trip_shape";
        return false;
    }
    compare = value_inst(f, (ValueId){(u32)term->ops[0].a});
    if (!compare || compare->op != IR_ICMP || compare->nops != 2) {
        if (reason)
            *reason = "trip_no_induction";
        return false;
    }
    pred = (IrIcmp)compare->subop;
    iv = compare->ops[0];
    bound = compare->ops[1];
    direct_iv = value_is_param(header, iv, &ordinal);
    if (!direct_iv) {
        iv = compare->ops[1];
        bound = compare->ops[0];
        pred = swap_pred(pred);
        direct_iv = value_is_param(header, iv, &ordinal);
        if (!direct_iv) {
            if (reason)
                *reason = "trip_no_induction";
            return false;
        }
    }
    if (inside == 1)
        pred = invert_pred(pred);
    if (ordinal >= preedge->nargs || ordinal >= backedge->nargs ||
        !recurrence(f, header, backedge->args[ordinal], ordinal, &update, &step,
                    &subtract)) {
        if (reason)
            *reason = "trip_no_induction";
        return false;
    }
    if (!loop_operand_invariant(f, loop, preedge->args[ordinal]) ||
        !loop_operand_invariant(f, loop, bound)) {
        if (reason)
            *reason = "trip_noninvariant";
        return false;
    }

    out->induction.header = header_id;
    out->induction.preheader = preheader_id;
    out->induction.latch = latch_id;
    out->induction.iv = header->params[ordinal];
    out->induction.update = update->result;
    out->induction.compare = compare->result;
    out->induction.param_index = ordinal;
    out->induction.type = ir_value_type(f, header->params[ordinal]);
    out->induction.pred = pred;
    out->induction.start = preedge->args[ordinal];
    out->induction.step = step;
    out->induction.bound = bound;
    out->induction.continue_edge = (u8)inside;
    out->induction.subtract_step = subtract;
    out->induction.signed_no_wrap = !fwrapv && (update->flags & IRF_NSW);
    out->induction.modular = pred == ICMP_EQ || pred == ICMP_NE ||
                             pred >= ICMP_ULT || !out->induction.signed_no_wrap;

    if (out->induction.start.kind != IROP_ICONST ||
        out->induction.bound.kind != IROP_ICONST) {
        out->kind = LOOP_TRIP_RUNTIME;
        return true;
    }
    effective_step = step.a;
    if (subtract)
        effective_step = ~effective_step + 1;
    if (!opt_unroll_trip_count(
            out->induction.type, pred, out->induction.start.a, effective_step,
            out->induction.bound.a, out->induction.modular, &out->constant)) {
        if (reason)
            *reason = "trip_wrap";
        return false;
    }
    out->kind = LOOP_TRIP_CONSTANT;
    return true;
}

static bool block_name_exists(const IrFunc *f, const char *name)
{
    u32 i;

    for (i = 0; i < f->nblocks; i++)
        if (f->blocks[i].name && strcmp(f->blocks[i].name, name) == 0)
            return true;
    return false;
}

static const char *clone_name(IrModule *m, const IrFunc *f, const char *prefix,
                              u32 ordinal)
{
    size_t cap = strlen(prefix) + 48;
    char *name = arena_alloc(m->arena, cap, 1);
    u32 serial = ordinal;

    for (;;) {
        int n = snprintf(name, cap, "%s.%u", prefix, serial);

        if (n < 0 || (size_t)n >= cap)
            CGF_ICE("loop_clone: label formatting overflow");
        if (!block_name_exists(f, name))
            return name;
        serial++;
        if (!serial)
            CGF_ICE("loop_clone: exhausted label namespace");
    }
}

static ValueId new_inst_value(IrModule *m, IrFunc *f, IrType type,
                              BlockId block, u32 pos)
{
    IrValInfo *values;
    ValueId result;

    if (f->nvals == f->cap_vals) {
        u32 cap = f->cap_vals ? f->cap_vals * 2 : 16;

        values = arena_alloc(m->arena, (size_t)cap * sizeof(*values),
                             _Alignof(IrValInfo));
        if (f->nvals)
            memcpy(values, f->vals, (size_t)f->nvals * sizeof(*values));
        f->vals = values;
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

BlockId loop_clone_block(const LoopCloneMap *map, BlockId old)
{
    if (!map || !old.v || old.v >= map->nblocks)
        return BLOCK_INVALID;
    return map->blocks[old.v];
}

IrOperand loop_clone_operand(const LoopCloneMap *map, IrOperand old)
{
    IrOperand replacement;

    if (!map || old.kind != IROP_VALUE || !old.a || old.a >= map->nvalues ||
        map->values[old.a].kind == IROP_NONE)
        return old;
    replacement = map->values[old.a];
    /* The map names a replacement VALUE; `old` is still the use site.  In
     * particular, a variadic call argument keeps its anonymous/narrowing ABI
     * provenance when a loop clone remaps a value defined in the region. */
    ir_arg_carry_provenance(&replacement, &old);
    return replacement;
}

static IrOperand *clone_operands(IrModule *m, const LoopCloneMap *map,
                                 const IrOperand *old, u32 n)
{
    IrOperand *copy;
    u32 i;

    if (!n)
        return NULL;
    copy =
        arena_alloc(m->arena, (size_t)n * sizeof(*copy), _Alignof(IrOperand));
    for (i = 0; i < n; i++)
        copy[i] = loop_clone_operand(map, old[i]);
    return copy;
}

static IrEdge *clone_edges(IrModule *m, const LoopCloneMap *map,
                           const IrEdge *old, u32 n)
{
    IrEdge *copy;
    u32 i;

    if (!n)
        return NULL;
    copy = arena_alloc(m->arena, (size_t)n * sizeof(*copy), _Alignof(IrEdge));
    for (i = 0; i < n; i++) {
        copy[i] = old[i];
        if (old[i].target.v < map->nblocks && map->blocks[old[i].target.v].v)
            copy[i].target = map->blocks[old[i].target.v];
        copy[i].args = clone_operands(m, map, old[i].args, old[i].nargs);
    }
    return copy;
}

static void append_inst(IrBlock *block, IrInst *in)
{
    in->next = NULL;
    if (block->last)
        block->last->next = in;
    else
        block->first = in;
    block->last = in;
    block->ninsts++;
}

bool loop_clone_region(IrModule *m, IrFunc *f, const BlockId *region,
                       u32 nregion, BlockId entry,
                       LoopClonePinnedPolicy pinned_policy,
                       const char *name_prefix, LoopCloneMap *out,
                       const char **reason)
{
    bool *member;
    bool cloned_pinned = false;
    u32 old_nblocks, old_nvals;
    u32 i, j;

    if (reason)
        *reason = NULL;
    if (out)
        memset(out, 0, sizeof(*out));
    if (!m || !f || !region || !nregion || !out || !entry.v ||
        (pinned_policy != LOOP_CLONE_REJECT_PINNED &&
         pinned_policy != LOOP_CLONE_PATH_EXCLUSIVE)) {
        if (reason)
            *reason = "clone_invalid_region";
        return false;
    }
    old_nblocks = f->nblocks;
    old_nvals = f->nvals;
    member = arena_alloc(m->arena, (size_t)(old_nblocks + 1) * sizeof(*member),
                         _Alignof(bool));
    memset(member, 0, (size_t)(old_nblocks + 1) * sizeof(*member));
    for (i = 0; i < nregion; i++) {
        const IrBlock *block;
        const IrInst *in;

        if (!region[i].v || region[i].v > old_nblocks) {
            if (reason)
                *reason = "clone_invalid_region";
            return false;
        }
        if (member[region[i].v]) {
            if (reason)
                *reason = "clone_duplicate_block";
            return false;
        }
        member[region[i].v] = true;
        block = &f->blocks[region[i].v - 1];
        for (in = block->first; in; in = in->next)
            if (in->flags & (IRF_VOLATILE | IRF_SEQ_CST)) {
                if (pinned_policy == LOOP_CLONE_REJECT_PINNED) {
                    if (reason)
                        *reason = "clone_pinned";
                    return false;
                }
                cloned_pinned = true;
            }
    }
    if (entry.v > old_nblocks || !member[entry.v]) {
        if (reason)
            *reason = "clone_entry_outside";
        return false;
    }

    out->nblocks = old_nblocks + 1;
    out->blocks =
        arena_alloc(m->arena, (size_t)out->nblocks * sizeof(*out->blocks),
                    _Alignof(BlockId));
    memset(out->blocks, 0, (size_t)out->nblocks * sizeof(*out->blocks));
    out->nvalues = old_nvals + 1;
    out->values =
        arena_alloc(m->arena, (size_t)out->nvalues * sizeof(*out->values),
                    _Alignof(IrOperand));
    memset(out->values, 0, (size_t)out->nvalues * sizeof(*out->values));
    if (!name_prefix || !*name_prefix)
        name_prefix = "loop.clone";

    /* Allocate the complete block set first.  ir_block_new may relocate the
     * function's block array; no source block pointer crosses this loop. */
    for (i = 0; i < nregion; i++)
        out->blocks[region[i].v] =
            ir_block_new(m, f, clone_name(m, f, name_prefix, i));
    out->entry = out->blocks[entry.v];
    out->cloned_pinned = cloned_pinned;

    /* Preallocate every definition before copying any use.  This handles
     * forward cross-block uses and also deliberately drives value-array
     * growth without retaining an IrValInfo pointer. */
    for (i = 0; i < nregion; i++) {
        BlockId source_id = region[i];
        BlockId dest_id = out->blocks[source_id.v];
        const IrBlock *source = &f->blocks[source_id.v - 1];
        const IrInst *in;
        u32 pos = 0;

        for (j = 0; j < source->nparams; j++) {
            ValueId old = source->params[j];
            ValueId fresh =
                ir_block_param(m, f, dest_id, ir_value_type(f, old));

            out->values[old.v] = ir_op_value(f, fresh);
        }
        source = &f->blocks[source_id.v - 1];
        for (in = source->first; in; in = in->next, pos++)
            if (in->result.v) {
                ValueId fresh =
                    new_inst_value(m, f, (IrType)in->type, dest_id, pos);

                out->values[in->result.v] = ir_op_value(f, fresh);
            }
    }

    for (i = 0; i < nregion; i++) {
        BlockId source_id = region[i];
        BlockId dest_id = out->blocks[source_id.v];
        const IrInst *old;

        /* Reacquire both blocks by ID after every allocation phase. */
        old = f->blocks[source_id.v - 1].first;
        for (; old; old = old->next) {
            IrInst *copy =
                arena_alloc(m->arena, sizeof(*copy), _Alignof(IrInst));
            IrBlock *dest;

            memcpy(copy, old, sizeof(*copy));
            copy->ops = clone_operands(m, out, old->ops, old->nops);
            copy->edges = clone_edges(m, out, old->edges, old->nedges);
            copy->next = NULL;
            if (old->result.v)
                copy->result = (ValueId){(u32)out->values[old->result.v].a};
            dest = &f->blocks[dest_id.v - 1];
            append_inst(dest, copy);
        }
    }
    return true;
}
