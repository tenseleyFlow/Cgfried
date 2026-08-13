#include "ir/ir.h"

#include <string.h>

/* The builder. Instructions append to the current block as an intrusive
 * list; operand and edge arrays are copied into the arena at build time,
 * so a caller may pass stack temporaries. Terminators seal the block —
 * appending past one is a builder bug and ICEs immediately rather than
 * producing IR the verifier would reject later with less context. */

static bool span_eq(Span a, Span b)
{
    bool same_path = a.presumed_path == b.presumed_path;

    if (!same_path && a.presumed_path && b.presumed_path)
        same_path = strcmp(a.presumed_path, b.presumed_path) == 0;
    return a.file_id == b.file_id && a.line == b.line && a.col == b.col &&
           a.len == b.len && a.presumed_line == b.presumed_line &&
           a.debug_loc == b.debug_loc && a.seq == b.seq &&
           a.origin == b.origin && same_path;
}

static u64 hash_bytes(u64 h, const void *data, size_t len)
{
    const u8 *p = data;
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= UINT64_C(1099511628211);
    }
    return h;
}

static u64 span_hash(Span span)
{
    u64 h = UINT64_C(1469598103934665603);

#define HASH_FIELD(field) h = hash_bytes(h, &span.field, sizeof(span.field))
    HASH_FIELD(file_id);
    HASH_FIELD(line);
    HASH_FIELD(col);
    HASH_FIELD(len);
    HASH_FIELD(presumed_line);
    HASH_FIELD(debug_loc);
    HASH_FIELD(seq);
    HASH_FIELD(origin);
#undef HASH_FIELD
    if (span.presumed_path)
        h = hash_bytes(h, span.presumed_path, strlen(span.presumed_path) + 1);
    return h;
}

static void loc_index_insert(IrModule *m, u32 id)
{
    u32 mask = m->cap_loc_slots - 1;
    u32 at = (u32)span_hash(m->locs[id - 1]) & mask;

    while (m->loc_slots[at]) {
        u32 old_id = m->loc_slots[at];

        if (span_eq(m->locs[old_id - 1], m->locs[id - 1]))
            return; /* preserve the first insertion id */
        at = (at + 1) & mask;
    }
    m->loc_slots[at] = id;
}

static void loc_index_rebuild(IrModule *m, u32 minimum_entries)
{
    u32 cap = 32;
    u32 i;

    if (minimum_entries > UINT32_MAX / 4)
        CGF_ICE("IR source-location index capacity overflow");
    while (cap < minimum_entries * 2)
        cap *= 2;
    m->loc_slots = arena_alloc(m->arena, cap * sizeof(u32), _Alignof(u32));
    memset(m->loc_slots, 0, cap * sizeof(u32));
    m->cap_loc_slots = cap;
    for (i = 0; i < m->nlocs; i++)
        loc_index_insert(m, i + 1);
    m->indexed_locs = m->nlocs;
}

u32 ir_intern_span(IrModule *m, Span span)
{
    u32 mask, at;

    if (!span.file_id)
        return 0;
    if (m->nlocs == UINT32_MAX)
        CGF_ICE("too many IR source locations");
    if (!m->cap_loc_slots || m->indexed_locs != m->nlocs ||
        m->nlocs >= m->cap_loc_slots - m->cap_loc_slots / 4)
        loc_index_rebuild(m, m->nlocs + 1);

    mask = m->cap_loc_slots - 1;
    at = (u32)span_hash(span) & mask;
    while (m->loc_slots[at]) {
        u32 id = m->loc_slots[at];

        if (span_eq(m->locs[id - 1], span))
            return id;
        at = (at + 1) & mask;
    }
    if (m->nlocs == m->cap_locs) {
        u32 nc = m->cap_locs ? m->cap_locs * 2 : 16;
        Span *nl = arena_alloc(m->arena, nc * sizeof(Span), _Alignof(Span));

        if (m->nlocs)
            memcpy(nl, m->locs, m->nlocs * sizeof(Span));
        m->locs = nl;
        m->cap_locs = nc;
    }
    m->locs[m->nlocs] = span;
    m->nlocs++;
    m->loc_slots[at] = m->nlocs;
    m->indexed_locs = m->nlocs;
    return m->nlocs;
}

static IrInst *append(IrBuilder *b, IrOp op, IrType t, bool defines)
{
    IrBlock *blk = ir_block(b->f, b->block);
    IrInst *in;

    if (!blk)
        CGF_ICE("ir builder: no current block");
    if (blk->last && blk->last->op >= IR_RET && blk->last->op <= IR_UNREACHABLE)
        CGF_ICE("ir builder: appending '%d' after the terminator of "
                "block '%s'",
                (int)op, blk->name ? blk->name : "?");
    in = arena_alloc(b->m->arena, sizeof(IrInst), _Alignof(IrInst));
    memset(in, 0, sizeof(*in));
    in->op = (u8)op;
    in->type = (u8)t;
    in->loc = ir_intern_span(b->m, b->loc);
    if (defines) {
        ValueId v;
        /* new_value lives in ir.c; recreate the minimal path here via the
         * public helpers: block params use ir_block_param, instruction
         * results need their own def record. */
        IrFunc *f = b->f;

        if (f->nvals == f->cap_vals) {
            u32 nc = f->cap_vals ? f->cap_vals * 2 : 16;
            IrValInfo *nv = arena_alloc(b->m->arena, nc * sizeof(IrValInfo),
                                        _Alignof(IrValInfo));

            if (f->nvals)
                memcpy(nv, f->vals, f->nvals * sizeof(IrValInfo));
            f->vals = nv;
            f->cap_vals = nc;
        }
        f->vals[f->nvals].type = (u8)t;
        f->vals[f->nvals].def_kind = VDEF_INST;
        f->vals[f->nvals].def_block = b->block;
        f->vals[f->nvals].def_pos = blk->ninsts;
        f->nvals++;
        v.v = f->nvals;
        in->result = v;
    }
    if (blk->last)
        blk->last->next = in;
    else
        blk->first = in;
    blk->last = in;
    blk->ninsts++;
    return in;
}

static IrOperand *copy_ops(IrModule *m, const IrOperand *src, u32 n)
{
    IrOperand *dst;

    if (n == 0)
        return NULL;
    dst = arena_alloc(m->arena, n * sizeof(IrOperand), _Alignof(IrOperand));
    memcpy(dst, src, n * sizeof(IrOperand));
    return dst;
}

void ir_builder_at(IrBuilder *b, IrModule *m, IrFunc *f, BlockId blk)
{
    b->m = m;
    b->f = f;
    b->block = blk;
    memset(&b->loc, 0, sizeof(b->loc));
}

void ir_builder_set_span(IrBuilder *b, Span span)
{
    b->loc = span;
}

Span ir_builder_span(const IrBuilder *b)
{
    return b->loc;
}

Span ir_debug_loc(const IrModule *m, u32 loc)
{
    Span none = {0};

    if (!m || loc == 0 || loc > m->nlocs)
        return none;
    return m->locs[loc - 1];
}

Span ir_inst_span(const IrModule *m, const IrInst *in)
{
    return ir_debug_loc(m, in ? in->loc : 0);
}

ValueId ir_build2(IrBuilder *b, IrOp op, IrType t, IrOperand x, IrOperand y)
{
    return ir_build2_flags(b, op, t, x, y, 0);
}

ValueId ir_build2_flags(IrBuilder *b, IrOp op, IrType t, IrOperand x,
                        IrOperand y, u8 flags)
{
    IrInst *in;
    IrOperand ops[2];

    if (op >= IR_VA_ARG) {
        ir_build_reserved(b, op);
        return VALUE_INVALID;
    }
    ops[0] = x;
    ops[1] = y;
    in = append(b, op, t, true);
    in->flags = flags;
    in->ops = copy_ops(b->m, ops, 2);
    in->nops = 2;
    return in->result;
}

ValueId ir_build1(IrBuilder *b, IrOp op, IrType t, IrOperand x)
{
    IrInst *in;

    if (op >= IR_VA_ARG) {
        ir_build_reserved(b, op);
        return VALUE_INVALID;
    }
    in = append(b, op, t, true);
    in->ops = copy_ops(b->m, &x, 1);
    in->nops = 1;
    return in->result;
}

ValueId ir_build_icmp(IrBuilder *b, IrIcmp p, IrOperand x, IrOperand y)
{
    IrInst *in;
    IrOperand ops[2];

    ops[0] = x;
    ops[1] = y;
    in = append(b, IR_ICMP, IRT_I32, true); /* comparisons yield i32 0/1 */
    in->subop = (u8)p;
    in->ops = copy_ops(b->m, ops, 2);
    in->nops = 2;
    return in->result;
}

ValueId ir_build_fcmp(IrBuilder *b, IrFcmp p, IrOperand x, IrOperand y)
{
    IrInst *in;
    IrOperand ops[2];

    ops[0] = x;
    ops[1] = y;
    in = append(b, IR_FCMP, IRT_I32, true);
    in->subop = (u8)p;
    in->ops = copy_ops(b->m, ops, 2);
    in->nops = 2;
    return in->result;
}

ValueId ir_build_vsplat(IrBuilder *b, IrType vector_type, IrOperand scalar)
{
    return ir_build1(b, IR_VSPLAT, vector_type, scalar);
}

ValueId ir_build_vextract(IrBuilder *b, IrOperand vector, u32 lane)
{
    u32 lanes = ir_vector_lanes((IrType)vector.type);
    IrInst *in;

    if (!lanes || lane >= lanes || lane > UINT8_MAX)
        CGF_ICE("ir_build_vextract: lane %u is out of range for %s", lane,
                ir_type_name((IrType)vector.type));
    in = append(b, IR_VEXTRACT, ir_vector_elem_type((IrType)vector.type), true);
    in->ops = copy_ops(b->m, &vector, 1);
    in->nops = 1;
    in->subop = (u8)lane;
    return in->result;
}

ValueId ir_build_vreduce(IrBuilder *b, IrOp op, IrOperand vector)
{
    IrInst *in = append(b, op, ir_vector_elem_type((IrType)vector.type), true);

    in->ops = copy_ops(b->m, &vector, 1);
    in->nops = 1;
    return in->result;
}

ValueId ir_build_alloca(IrBuilder *b, IrOperand size, u32 align)
{
    return ir_build_alloca_typed(b, size, align, ETYPE_UNKNOWN);
}

ValueId ir_build_alloca_typed(IrBuilder *b, IrOperand size, u32 align,
                              EffTypeId etype)
{
    IrInst *in = append(b, IR_ALLOCA, IRT_PTR, true);

    in->ops = copy_ops(b->m, &size, 1);
    in->nops = 1;
    in->align = align;
    in->subop = (u8)etype;
    return in->result;
}

ValueId ir_build_load(IrBuilder *b, IrType t, IrOperand ptr, u32 align,
                      u8 flags)
{
    return ir_build_load_typed(b, t, ptr, align, flags, ETYPE_UNKNOWN);
}

ValueId ir_build_load_typed(IrBuilder *b, IrType t, IrOperand ptr, u32 align,
                            u8 flags, EffTypeId etype)
{
    IrInst *in = append(b, IR_LOAD, t, true);

    in->ops = copy_ops(b->m, &ptr, 1);
    in->nops = 1;
    in->align = align;
    in->flags = flags;
    in->subop = (u8)etype;
    return in->result;
}

void ir_build_store(IrBuilder *b, IrOperand val, IrOperand ptr, u32 align,
                    u8 flags)
{
    ir_build_store_typed(b, val, ptr, align, flags, ETYPE_UNKNOWN);
}

void ir_build_store_typed(IrBuilder *b, IrOperand val, IrOperand ptr, u32 align,
                          u8 flags, EffTypeId etype)
{
    IrOperand ops[2];
    IrInst *in;

    ops[0] = val;
    ops[1] = ptr;
    in = append(b, IR_STORE, IRT_VOID, false);
    in->ops = copy_ops(b->m, ops, 2);
    in->nops = 2;
    in->align = align;
    in->flags = flags;
    in->subop = (u8)etype;
}

ValueId ir_build_ptradd(IrBuilder *b, IrOperand ptr, IrOperand off)
{
    return ir_build2(b, IR_PTRADD, IRT_PTR, ptr, off);
}

void ir_build_memcpy(IrBuilder *b, IrOperand dst, IrOperand src, IrOperand size,
                     u32 align, u8 flags)
{
    IrOperand ops[3];
    IrInst *in;

    ops[0] = dst;
    ops[1] = src;
    ops[2] = size;
    in = append(b, IR_MEMCPY, IRT_VOID, false);
    in->ops = copy_ops(b->m, ops, 3);
    in->nops = 3;
    in->align = align;
    in->flags = flags;
    in->subop = ETYPE_CHAR;
}

void ir_build_memset(IrBuilder *b, IrOperand dst, IrOperand byte,
                     IrOperand size, u32 align, u8 flags)
{
    IrOperand ops[3];
    IrInst *in;

    ops[0] = dst;
    ops[1] = byte;
    ops[2] = size;
    in = append(b, IR_MEMSET, IRT_VOID, false);
    in->ops = copy_ops(b->m, ops, 3);
    in->nops = 3;
    in->align = align;
    in->flags = flags;
    in->subop = ETYPE_CHAR;
}

ValueId ir_build_select(IrBuilder *b, IrOperand c, IrOperand x, IrOperand y)
{
    IrOperand ops[3];
    IrInst *in;

    ops[0] = c;
    ops[1] = x;
    ops[2] = y;
    in = append(b, IR_SELECT, (IrType)x.type, true);
    in->ops = copy_ops(b->m, ops, 3);
    in->nops = 3;
    return in->result;
}

ValueId ir_build_call(IrBuilder *b, IrType ret, IrFuncRefKind kind, u32 callee,
                      const IrOperand *args, u32 nargs)
{
    IrInst *in = append(b, IR_CALL, ret, ret != IRT_VOID);

    in->subop = (u8)kind;
    in->callee = callee;
    in->ops = copy_ops(b->m, args, nargs);
    in->nops = nargs;
    return in->result;
}

/* Mark the just-built call as targeting a VARIADIC C type (Sprint 23:
 * the AL protocol). Must follow an ir_build_call* on the same block. */
void ir_call_mark_variadic(IrBuilder *b)
{
    IrBlock *blk = &b->f->blocks[b->block.v - 1];

    if (!blk->last || blk->last->op != IR_CALL)
        CGF_ICE("ir_call_mark_variadic: last instruction is not a call");
    blk->last->flags |= IRF_CALL_VARIADIC;
}

void ir_call_mark_noreturn(IrBuilder *b)
{
    IrBlock *blk = &b->f->blocks[b->block.v - 1];

    if (!blk->last || blk->last->op != IR_CALL)
        CGF_ICE("ir_call_mark_noreturn: last instruction is not a call");
    blk->last->flags |= IRF_NORETURN;
}

void ir_load_mark_self_init(IrBuilder *b)
{
    IrBlock *blk = &b->f->blocks[b->block.v - 1];

    if (!blk->last || blk->last->op != IR_LOAD)
        CGF_ICE("ir_load_mark_self_init: last instruction is not a load");
    blk->last->flags |= IRF_SELF_INIT;
}

void ir_branch_mark_flow_provenance(IrBuilder *b)
{
    IrBlock *blk = &b->f->blocks[b->block.v - 1];

    if (!blk->last || (blk->last->op != IR_BR && blk->last->op != IR_CONDBR &&
                       blk->last->op != IR_SWITCH))
        CGF_ICE("ir_branch_mark_flow_provenance: last instruction is not a "
                "branch");
    blk->last->flags |= IRF_FLOW_PROVENANCE;
}

void ir_ret_mark_implicit(IrBuilder *b)
{
    IrBlock *blk = &b->f->blocks[b->block.v - 1];

    if (!blk->last || blk->last->op != IR_RET)
        CGF_ICE("ir_ret_mark_implicit: last instruction is not a return");
    blk->last->flags |= IRF_FLOW_PROVENANCE;
}

ValueId ir_build_call_indirect(IrBuilder *b, IrType ret, IrOperand fp,
                               const IrOperand *args, u32 nargs)
{
    IrInst *in = append(b, IR_CALL, ret, ret != IRT_VOID);
    IrOperand *ops = arena_alloc(b->m->arena, (nargs + 1) * sizeof(IrOperand),
                                 _Alignof(IrOperand));

    /* The function pointer rides as ops[0]; real arguments follow. */
    ops[0] = fp;
    if (nargs)
        memcpy(ops + 1, args, nargs * sizeof(IrOperand));
    in->subop = FUNCREF_INDIRECT;
    in->ops = ops;
    in->nops = nargs + 1;
    return in->result;
}

static IrEdge *make_edges(IrModule *m, u32 n)
{
    IrEdge *e = arena_alloc(m->arena, n * sizeof(IrEdge), _Alignof(IrEdge));

    memset(e, 0, n * sizeof(IrEdge));
    return e;
}

void ir_build_ret(IrBuilder *b, const IrOperand *val)
{
    IrInst *in = append(b, IR_RET, IRT_VOID, false);

    if (val) {
        in->ops = copy_ops(b->m, val, 1);
        in->nops = 1;
    }
}

void ir_build_br(IrBuilder *b, BlockId target, const IrOperand *args, u32 nargs)
{
    IrInst *in = append(b, IR_BR, IRT_VOID, false);

    in->edges = make_edges(b->m, 1);
    in->nedges = 1;
    in->edges[0].target = target;
    in->edges[0].args = copy_ops(b->m, args, nargs);
    in->edges[0].nargs = nargs;
}

void ir_build_condbr(IrBuilder *b, IrOperand c, BlockId t,
                     const IrOperand *targs, u32 ntargs, BlockId e,
                     const IrOperand *eargs, u32 neargs)
{
    IrInst *in = append(b, IR_CONDBR, IRT_VOID, false);

    in->ops = copy_ops(b->m, &c, 1);
    in->nops = 1;
    in->edges = make_edges(b->m, 2);
    in->nedges = 2;
    in->edges[0].target = t;
    in->edges[0].args = copy_ops(b->m, targs, ntargs);
    in->edges[0].nargs = ntargs;
    in->edges[1].target = e;
    in->edges[1].args = copy_ops(b->m, eargs, neargs);
    in->edges[1].nargs = neargs;
}

void ir_build_switch(IrBuilder *b, IrOperand x, BlockId defblk,
                     const i64 *case_vals, const BlockId *case_blks, u32 n)
{
    IrInst *in = append(b, IR_SWITCH, IRT_VOID, false);
    u32 i;

    in->ops = copy_ops(b->m, &x, 1);
    in->nops = 1;
    /* Edge 0 is ALWAYS the default; cases follow in declaration order. */
    in->edges = make_edges(b->m, n + 1);
    in->nedges = n + 1;
    in->edges[0].target = defblk;
    for (i = 0; i < n; i++) {
        in->edges[i + 1].target = case_blks[i];
        in->edges[i + 1].case_val = case_vals[i];
    }
}

void ir_build_unreachable(IrBuilder *b)
{
    append(b, IR_UNREACHABLE, IRT_VOID, false);
}

void ir_build_va_start(IrBuilder *b, IrOperand ap)
{
    IrInst *in = append(b, IR_VA_START, IRT_VOID, false);

    in->ops = copy_ops(b->m, &ap, 1);
    in->nops = 1;
}

ValueId ir_build_stacksave(IrBuilder *b)
{
    IrInst *in = append(b, IR_STACKSAVE, IRT_PTR, true);

    return in->result;
}

void ir_build_stackrestore(IrBuilder *b, IrOperand tok)
{
    IrInst *in = append(b, IR_STACKRESTORE, IRT_VOID, false);

    in->ops = copy_ops(b->m, &tok, 1);
    in->nops = 1;
}

/* Inline asm. `asm_index` is the 1-based IrModule.asms index; the operands
 * are in the template's %0 order, outputs (as ADDRESSES) first. */
void ir_build_asm(IrBuilder *b, u32 asm_index, const IrOperand *ops, u32 nops)
{
    IrInst *in = append(b, IR_ASM, IRT_VOID, false);

    in->callee = asm_index;
    if (nops) {
        in->ops = copy_ops(b->m, ops, nops);
        in->nops = nops;
    }
}

ValueId ir_build_atomicrmw(IrBuilder *b, IrAtomicRmw op, IrType t,
                           IrOperand ptr, IrOperand val)
{
    IrOperand ops[2];
    IrInst *in;

    if (t > IRT_I64)
        CGF_ICE("atomicrmw on a non-integer type %d (belt and suspenders: "
                "sema already rejected oversized/_Atomic-float RMW here)",
                (int)t);
    ops[0] = ptr;
    ops[1] = val;
    in = append(b, IR_ATOMICRMW, t, true);
    in->subop = (u8)op;
    in->flags = IRF_SEQ_CST;
    in->ops = copy_ops(b->m, ops, 2);
    in->nops = 2;
    return in->result;
}

ValueId ir_build_cmpxchg(IrBuilder *b, IrType t, IrOperand ptr,
                         IrOperand expected, IrOperand desired)
{
    IrOperand ops[3];
    IrInst *in;

    if (t > IRT_I64)
        CGF_ICE("cmpxchg on a non-integer type %d", (int)t);
    ops[0] = ptr;
    ops[1] = expected;
    ops[2] = desired;
    in = append(b, IR_CMPXCHG, t, true);
    in->flags = IRF_SEQ_CST;
    in->ops = copy_ops(b->m, ops, 3);
    in->nops = 3;
    return in->result;
}

void ir_build_reserved(IrBuilder *b, IrOp op)
{
    static const struct {
        IrOp op;
        const char *name;
        int sprint;
    } table[] = {
        {IR_STACKSAVE, "stacksave", 20},
        {IR_STACKRESTORE, "stackrestore", 20},
        {IR_ATOMICRMW, "atomicrmw", 20},
        {IR_CMPXCHG, "cmpxchg", 20},
        /* IR_VA_ARG/VA_END/VA_COPY never become instructions (Sprint 19
         * expands them at lowering); reaching one here is a plain ICE
         * via the fallthrough below. */
    };
    u32 i;

    (void)b;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (table[i].op == op)
            CGF_ICE("ir_build_%s: reserved opcode — lands in Sprint %d",
                    table[i].name, table[i].sprint);
    CGF_ICE("ir_build_reserved: opcode %d is not reserved", (int)op);
}
