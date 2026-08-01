#include "opt/opt.h"

#include <string.h>

#include "util/arena.h"
#include "util/softfp.h"

const Pass OPT_PASS_SIMPLIFY = {"simplify", opt_simplify, PASS_PINNED_EXACT};

/* Integer IR values are bit patterns, never host signed arithmetic.  All
 * operations below use u64 modulo arithmetic and explicitly trim to the IR
 * width.  This is the integer half of Sprint 15's target-semantics law; the
 * FP half goes exclusively through softfp below. */
static u32 int_width(IrType t)
{
    if (t <= IRT_I64)
        return 8u << (u32)t;
    return 0;
}

static u64 width_mask(u32 width)
{
    return width == 64 ? UINT64_MAX : (1ull << width) - 1;
}

static bool is_int_const(IrOperand op)
{
    return op.kind == IROP_ICONST && int_width((IrType)op.type) != 0;
}

static IrOperand iconst_bits(IrType type, u64 bits)
{
    IrOperand out = ir_op_iconst(type, 0);

    out.a = bits & width_mask(int_width(type));
    return out;
}

static bool operand_equal(IrOperand a, IrOperand b)
{
    return a.kind == b.kind && a.type == b.type && a.sym == b.sym &&
           a.a == b.a && a.b == b.b;
}

static bool power_of_two(u64 value, u32 width, u32 *shift)
{
    u32 n = 0;

    value &= width_mask(width);
    if (value == 0 || (value & (value - 1)) != 0)
        return false;
    while ((value >>= 1) != 0)
        n++;
    *shift = n;
    return true;
}

static const SfFormat *format_of(IrType t)
{
    switch (t) {
    case IRT_F32:
        return &SF_BINARY32;
    case IRT_F64:
        return &SF_BINARY64;
    case IRT_F80:
        return &SF_X87_80;
    case IRT_F128:
        return &SF_BINARY128;
    default:
        return NULL;
    }
}

static Sf sf_of_operand(IrOperand op)
{
    const SfFormat *format = format_of((IrType)op.type);
    u8 bytes[16];
    u32 i;

    memset(bytes, 0, sizeof(bytes));
    for (i = 0; i < 8; i++) {
        bytes[i] = (u8)(op.a >> (i * 8));
        bytes[i + 8] = (u8)(op.b >> (i * 8));
    }
    return sf_from_bits(bytes, *format);
}

static IrOperand operand_of_sf(IrType type, Sf value)
{
    const SfFormat *format = format_of(type);
    u8 bytes[16];
    u64 lo = 0, hi = 0;
    u32 i;

    sf_to_bits(value, *format, bytes);
    for (i = 0; i < 8; i++) {
        lo |= (u64)bytes[i] << (i * 8);
        hi |= (u64)bytes[i + 8] << (i * 8);
    }
    return ir_op_fconst(type, lo, hi);
}

static bool fold_undef(const IrInst *in, IrOperand *out)
{
    IrOperand other;
    u32 width;
    u64 mask;

    if (in->nops != 2 ||
        (in->ops[0].kind != IROP_UNDEF && in->ops[1].kind != IROP_UNDEF))
        return false;
    width = int_width((IrType)in->type);
    if (!width)
        return false;
    mask = width_mask(width);
    other = in->ops[in->ops[0].kind == IROP_UNDEF ? 1 : 0];
    if (other.kind != IROP_ICONST)
        return false;
    switch ((IrOp)in->op) {
    case IR_AND:
    case IR_IMUL:
        if ((other.a & mask) == 0) {
            *out = iconst_bits((IrType)in->type, 0);
            return true;
        }
        break;
    case IR_OR:
        if ((other.a & mask) == mask) {
            *out = iconst_bits((IrType)in->type, mask);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

static bool fold_integer(const IrInst *in, IrOperand *out)
{
    IrType type = (IrType)in->type;
    u32 width = int_width(type);
    u64 mask, x, y, value = 0;
    bool sx, sy;

    if (!width || in->nops != 2 || !is_int_const(in->ops[0]) ||
        !is_int_const(in->ops[1]))
        return false;
    mask = width_mask(width);
    x = in->ops[0].a & mask;
    y = in->ops[1].a & mask;
    sx = (x & (1ull << (width - 1))) != 0;
    sy = (y & (1ull << (width - 1))) != 0;

    switch ((IrOp)in->op) {
    case IR_IADD:
        value = x + y;
        break;
    case IR_ISUB:
        value = x - y;
        break;
    case IR_IMUL:
        value = x * y;
        break;
    case IR_AND:
        value = x & y;
        break;
    case IR_OR:
        value = x | y;
        break;
    case IR_XOR:
        value = x ^ y;
        break;
    case IR_SHL:
        if (y >= width) {
            *out = ir_op_undef(type);
            return true;
        }
        value = x << (u32)y;
        break;
    case IR_LSHR:
        if (y >= width) {
            *out = ir_op_undef(type);
            return true;
        }
        value = x >> (u32)y;
        break;
    case IR_ASHR:
        if (y >= width) {
            *out = ir_op_undef(type);
            return true;
        }
        value = x >> (u32)y;
        if (sx && y != 0)
            value |= mask & (UINT64_MAX << (width - (u32)y));
        break;
    case IR_UDIV:
    case IR_UREM:
        if (y == 0) {
            *out = ir_op_undef(type);
            return true;
        }
        value = in->op == IR_UDIV ? x / y : x % y;
        break;
    case IR_SDIV:
    case IR_SREM: {
        u64 sign = 1ull << (width - 1);
        u64 mx, my, q;

        if (y == 0 || (x == sign && y == mask)) {
            *out = ir_op_undef(type);
            return true;
        }
        mx = sx ? (0 - x) & mask : x;
        my = sy ? (0 - y) & mask : y;
        if (in->op == IR_SDIV) {
            q = mx / my;
            value = sx != sy ? (0 - q) & mask : q;
        } else {
            q = mx % my;
            value = sx ? (0 - q) & mask : q;
        }
        break;
    }
    default:
        return false;
    }
    *out = iconst_bits(type, value);
    return true;
}

static bool fold_integer_ub(const IrInst *in, IrOperand *out)
{
    IrType type = (IrType)in->type;
    u32 width = int_width(type);
    u64 count;

    if (!width || in->nops != 2 || in->ops[1].kind != IROP_ICONST)
        return false;
    count = in->ops[1].a & width_mask(int_width((IrType)in->ops[1].type));
    if (((in->op == IR_SDIV || in->op == IR_UDIV || in->op == IR_SREM ||
          in->op == IR_UREM) &&
         count == 0) ||
        ((in->op == IR_SHL || in->op == IR_LSHR || in->op == IR_ASHR) &&
         count >= width)) {
        *out = ir_op_undef(type);
        return true;
    }
    return false;
}

static bool fold_icmp(const IrInst *in, IrOperand *out)
{
    u32 width;
    u64 mask, x, y;
    bool sx, sy, result;

    if (in->op != IR_ICMP || in->nops != 2 || !is_int_const(in->ops[0]) ||
        !is_int_const(in->ops[1]))
        return false;
    width = int_width((IrType)in->ops[0].type);
    mask = width_mask(width);
    x = in->ops[0].a & mask;
    y = in->ops[1].a & mask;
    sx = (x & (1ull << (width - 1))) != 0;
    sy = (y & (1ull << (width - 1))) != 0;
    switch ((IrIcmp)in->subop) {
    case ICMP_EQ:
        result = x == y;
        break;
    case ICMP_NE:
        result = x != y;
        break;
    case ICMP_SLT:
        result = sx != sy ? sx : x < y;
        break;
    case ICMP_SLE:
        result = sx != sy ? sx : x <= y;
        break;
    case ICMP_SGT:
        result = sx != sy ? sy : x > y;
        break;
    case ICMP_SGE:
        result = sx != sy ? sy : x >= y;
        break;
    case ICMP_ULT:
        result = x < y;
        break;
    case ICMP_ULE:
        result = x <= y;
        break;
    case ICMP_UGT:
        result = x > y;
        break;
    case ICMP_UGE:
        result = x >= y;
        break;
    default:
        return false;
    }
    *out = iconst_bits(IRT_I32, result ? 1 : 0);
    return true;
}

static bool fold_fp(const IrInst *in, IrOperand *out)
{
    const SfFormat *format = format_of((IrType)in->type);
    SfStatus status;
    Sf x, y, value;

    if (!format || in->nops < 1 || in->ops[0].kind != IROP_FCONST ||
        (in->op != IR_FNEG &&
         (in->nops != 2 || in->ops[1].kind != IROP_FCONST)))
        return false;
    memset(&status, 0, sizeof(status));
    x = sf_of_operand(in->ops[0]);
    if (in->op != IR_FNEG)
        y = sf_of_operand(in->ops[1]);
    switch ((IrOp)in->op) {
    case IR_FADD:
        value = sf_add(x, y, *format, &status);
        break;
    case IR_FSUB:
        value = sf_sub(x, y, *format, &status);
        break;
    case IR_FMUL:
        value = sf_mul(x, y, *format, &status);
        break;
    case IR_FDIV:
        value = sf_div(x, y, *format, &status);
        break;
    case IR_FNEG:
        value = sf_neg(x);
        break;
    default:
        return false;
    }
    /* softfp intentionally stores only the NaN class today, not its
     * payload/signaling bits.  IR_BITCAST makes those bits observable, so
     * folding either a propagated NaN or an invalid operation that creates
     * one would violate the exact-bit IR contract. */
    if (value.cls == SF_NAN)
        return false;
    *out = operand_of_sf((IrType)in->type, value);
    return true;
}

static bool fold_fcmp(const IrInst *in, IrOperand *out)
{
    Sf x, y;
    bool unordered, result;
    int cmp;

    if (in->op != IR_FCMP || in->nops != 2 || in->ops[0].kind != IROP_FCONST ||
        in->ops[1].kind != IROP_FCONST)
        return false;
    x = sf_of_operand(in->ops[0]);
    y = sf_of_operand(in->ops[1]);
    cmp = sf_cmp(x, y, &unordered);
    switch ((IrFcmp)in->subop) {
    case FCMP_OEQ:
        result = !unordered && cmp == 0;
        break;
    case FCMP_ONE:
        result = !unordered && cmp != 0;
        break;
    case FCMP_OLT:
        result = !unordered && cmp < 0;
        break;
    case FCMP_OLE:
        result = !unordered && cmp <= 0;
        break;
    case FCMP_OGT:
        result = !unordered && cmp > 0;
        break;
    case FCMP_OGE:
        result = !unordered && cmp >= 0;
        break;
    case FCMP_ORD:
        result = !unordered;
        break;
    case FCMP_UEQ:
        result = unordered || cmp == 0;
        break;
    case FCMP_UNE:
        result = unordered || cmp != 0;
        break;
    case FCMP_ULT:
        result = unordered || cmp < 0;
        break;
    case FCMP_ULE:
        result = unordered || cmp <= 0;
        break;
    case FCMP_UGT:
        result = unordered || cmp > 0;
        break;
    case FCMP_UGE:
        result = unordered || cmp >= 0;
        break;
    case FCMP_UNO:
        result = unordered;
        break;
    default:
        return false;
    }
    *out = iconst_bits(IRT_I32, result ? 1 : 0);
    return true;
}

static bool fold_conversion(const IrInst *in, IrOperand *out)
{
    IrOperand op;
    IrType from, to;
    u32 fw, tw;
    u64 bits, mask;
    SfStatus status;

    if (in->nops != 1)
        return false;
    op = in->ops[0];
    from = (IrType)op.type;
    to = (IrType)in->type;
    fw = int_width(from);
    tw = int_width(to);
    switch ((IrOp)in->op) {
    case IR_SEXT:
        if (op.kind != IROP_ICONST || !fw || !tw)
            return false;
        bits = op.a & width_mask(fw);
        if (bits & (1ull << (fw - 1)))
            bits |= ~width_mask(fw);
        *out = iconst_bits(to, bits);
        return true;
    case IR_ZEXT:
    case IR_TRUNC:
        if (op.kind != IROP_ICONST || !fw || !tw)
            return false;
        *out = iconst_bits(to, op.a);
        return true;
    case IR_FPEXT:
    case IR_FPTRUNC: {
        const SfFormat *ff = format_of(from);
        const SfFormat *tf = format_of(to);
        Sf value;

        if (op.kind != IROP_FCONST || !ff || !tf)
            return false;
        memset(&status, 0, sizeof(status));
        value = sf_convert(sf_of_operand(op), *ff, *tf, &status);
        if (value.cls == SF_NAN)
            return false;
        *out = operand_of_sf(to, value);
        return true;
    }
    case IR_FPTOSI:
    case IR_FPTOUI:
        if (op.kind != IROP_FCONST || !format_of(from) || !tw)
            return false;
        memset(&status, 0, sizeof(status));
        bits =
            sf_to_int(sf_of_operand(op), (int)tw, in->op == IR_FPTOUI, &status);
        *out = status.invalid ? ir_op_undef(to) : iconst_bits(to, bits);
        return true;
    case IR_SITOFP:
    case IR_UITOFP: {
        const SfFormat *tf = format_of(to);
        bool negative = false;
        Sf value;

        if (op.kind != IROP_ICONST || !fw || !tf)
            return false;
        mask = width_mask(fw);
        bits = op.a & mask;
        if (in->op == IR_SITOFP && (bits & (1ull << (fw - 1)))) {
            negative = true;
            bits = (0 - bits) & mask;
        }
        memset(&status, 0, sizeof(status));
        value = sf_from_int(bits, negative, *tf, &status);
        *out = operand_of_sf(to, value);
        return true;
    }
    case IR_BITCAST:
        if (op.kind == IROP_ICONST && format_of(to) &&
            fw == (u32)format_of(to)->total_bytes * 8) {
            *out = ir_op_fconst(to, op.a & width_mask(fw), 0);
            return true;
        }
        if (op.kind == IROP_FCONST && format_of(from) &&
            tw == (u32)format_of(from)->total_bytes * 8) {
            *out = iconst_bits(to, op.a);
            return true;
        }
        return false;
    default:
        return false;
    }
}

bool opt_fold_inst(const IrInst *in, IrOperand *out, const OptConfig *cfg)
{
    (void)cfg;
    if (!in || !out || !in->result.v)
        return false;
    if (fold_undef(in, out) || fold_integer_ub(in, out) ||
        fold_integer(in, out) || fold_icmp(in, out) || fold_fp(in, out) ||
        fold_fcmp(in, out) || fold_conversion(in, out))
        return true;
    if (in->op == IR_SELECT && in->nops == 3) {
        if (in->ops[0].kind == IROP_ICONST) {
            *out = in->ops[in->ops[0].a ? 1 : 2];
            return true;
        }
        if (operand_equal(in->ops[1], in->ops[2])) {
            *out = in->ops[1];
            return true;
        }
    }
    if (in->op == IR_PTRADD && in->nops == 2 &&
        in->ops[0].kind == IROP_SYMBOL && in->ops[1].kind == IROP_ICONST) {
        u32 offset_width = int_width((IrType)in->ops[1].type);
        u64 addend;
        u64 sum;

        if (!offset_width)
            return false;
        addend = in->ops[1].a & width_mask(offset_width);
        if (addend & (1ull << (offset_width - 1)))
            addend |= ~width_mask(offset_width);
        sum = in->ops[0].a + addend;
        /* Symbol addends are signed i64.  Wrapping one would silently name
         * a different address, so leave it for the backend instead. */
        if (((in->ops[0].a ^ sum) & (addend ^ sum) & (1ull << 63)) != 0)
            return false;
        *out = in->ops[0];
        out->a = sum;
        return true;
    }
    return false;
}

/* --- algebraic simplify ------------------------------------------------- */

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
                    const IrBlock *target = ir_block((IrFunc *)f, edge->target);
                    u32 ai;

                    if (!target)
                        continue;
                    for (ai = 0; ai < edge->nargs && ai < target->nparams;
                         ai++) {
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

static IrOperand resolve_operand(IrOperand op, const IrOperand *replacement,
                                 u32 nold)
{
    u32 steps = 0;

    while (op.kind == IROP_VALUE && op.a >= 1 && op.a <= nold &&
           replacement[op.a].kind != IROP_NONE) {
        op = replacement[op.a];
        if (++steps > nold)
            CGF_ICE("simplify: cyclic value replacement");
    }
    return op;
}

static IrOperand resolve_inst_operand(IrOperand op,
                                      const IrOperand *replacement, u32 nold,
                                      bool call_operand)
{
    IrOperand old = op;

    op = resolve_operand(op, replacement, nold);
    if (call_operand && (old.kind == IROP_VALUE || old.kind == IROP_SYMBOL) &&
        ir_arg_kind(old.b) != IR_ARG_NONE)
        op.b = old.b;
    return op;
}

static void build_defs(const IrFunc *f, IrInst **defs, u32 nold)
{
    u32 bi;

    memset(defs, 0, (nold + 1) * sizeof(*defs));
    for (bi = 0; bi < f->nblocks; bi++) {
        IrInst *in;

        for (in = f->blocks[bi].first; in; in = in->next)
            if (in->result.v && in->result.v <= nold)
                defs[in->result.v] = in;
    }
}

static IrInst *def_of(IrOperand op, IrInst *const *defs, u32 nold)
{
    if (op.kind != IROP_VALUE || op.a < 1 || op.a > nold)
        return NULL;
    return defs[op.a];
}

static ValueId alloc_inst_value(IrModule *m, IrFunc *f, BlockId block,
                                IrType type)
{
    ValueId value;

    if (f->nvals == f->cap_vals) {
        u32 capacity = f->cap_vals ? f->cap_vals * 2 : 16;
        IrValInfo *values = arena_alloc(m->arena, capacity * sizeof(*values),
                                        _Alignof(IrValInfo));

        if (f->nvals)
            memcpy(values, f->vals, f->nvals * sizeof(*values));
        f->vals = values;
        f->cap_vals = capacity;
    }
    memset(&f->vals[f->nvals], 0, sizeof(f->vals[f->nvals]));
    f->vals[f->nvals].type = (u8)type;
    f->vals[f->nvals].def_kind = VDEF_INST;
    f->vals[f->nvals].def_block = block;
    value.v = ++f->nvals;
    return value;
}

static IrInst *insert2_before(IrModule *m, IrFunc *f, IrBlock *block,
                              BlockId block_id, IrInst **prev, IrInst *before,
                              IrOp op, IrType type, IrOperand x, IrOperand y)
{
    IrInst *in = arena_alloc(m->arena, sizeof(*in), _Alignof(IrInst));

    memset(in, 0, sizeof(*in));
    in->op = (u8)op;
    in->type = (u8)type;
    in->loc = before->loc;
    in->result = alloc_inst_value(m, f, block_id, type);
    in->ops = arena_alloc(m->arena, 2 * sizeof(*in->ops), _Alignof(IrOperand));
    in->ops[0] = x;
    in->ops[1] = y;
    in->nops = 2;
    in->next = before;
    if (*prev)
        (*prev)->next = in;
    else
        block->first = in;
    *prev = in;
    block->ninsts++;
    return in;
}

static void unlink_inst(IrBlock *block, IrInst *prev, IrInst *in, IrInst *next)
{
    if (prev)
        prev->next = next;
    else
        block->first = next;
    if (block->last == in)
        block->last = prev;
    block->ninsts--;
}

static IrIcmp swapped_predicate(IrIcmp predicate)
{
    switch (predicate) {
    case ICMP_EQ:
        return ICMP_EQ;
    case ICMP_NE:
        return ICMP_NE;
    case ICMP_SLT:
        return ICMP_SGT;
    case ICMP_SLE:
        return ICMP_SGE;
    case ICMP_SGT:
        return ICMP_SLT;
    case ICMP_SGE:
        return ICMP_SLE;
    case ICMP_ULT:
        return ICMP_UGT;
    case ICMP_ULE:
        return ICMP_UGE;
    case ICMP_UGT:
        return ICMP_ULT;
    case ICMP_UGE:
        return ICMP_ULE;
    }
    CGF_ICE("simplify: unknown icmp predicate %d", (int)predicate);
}

static void mutate_binary(IrInst *in, IrOp op, IrOperand x, IrOperand y)
{
    in->op = (u8)op;
    if (op != IR_IADD && op != IR_ISUB && op != IR_IMUL)
        in->flags &= (u8)~IRF_NSW;
    in->ops[0] = x;
    in->ops[1] = y;
    in->nops = 2;
}

static bool simplify_one(IrModule *m, IrFunc *f, IrBlock *block,
                         BlockId block_id, IrInst **prev, IrInst *in,
                         IrInst *const *defs, const bool *may_undef,
                         IrOperand *replacement, u32 nold, const OptConfig *cfg,
                         bool *remove, IrOperand *replacement_value)
{
    IrOperand folded;
    IrOperand x = in->nops > 0 ? in->ops[0] : (IrOperand){0};
    IrOperand y = in->nops > 1 ? in->ops[1] : (IrOperand){0};
    u32 width = int_width((IrType)in->type);

    *remove = false;
    memset(replacement_value, 0, sizeof(*replacement_value));
    if (opt_fold_inst(in, &folded, cfg)) {
        *remove = true;
        *replacement_value = folded;
        return true;
    }

    if (in->op == IR_ICMP && in->nops == 2 && x.kind == IROP_ICONST &&
        y.kind != IROP_ICONST) {
        in->ops[0] = y;
        in->ops[1] = x;
        in->subop = (u8)swapped_predicate((IrIcmp)in->subop);
        return true;
    }

    if (width && in->nops == 2) {
        u64 mask = width_mask(width);
        bool x_zero = x.kind == IROP_ICONST && (x.a & mask) == 0;
        bool y_zero = y.kind == IROP_ICONST && (y.a & mask) == 0;
        bool y_one = y.kind == IROP_ICONST && (y.a & mask) == 1;
        bool y_ones = y.kind == IROP_ICONST && (y.a & mask) == mask;
        u32 shift;

        if ((in->op == IR_IADD && (x_zero || y_zero)) ||
            (in->op == IR_ISUB && y_zero) || (in->op == IR_IMUL && y_one) ||
            (in->op == IR_AND && y_ones) ||
            ((in->op == IR_OR || in->op == IR_XOR) && y_zero)) {
            *remove = true;
            *replacement_value = x_zero && in->op == IR_IADD ? y : x;
            return true;
        }
        if (in->op == IR_IMUL && (x_zero || y_zero)) {
            *remove = true;
            *replacement_value = iconst_bits((IrType)in->type, 0);
            return true;
        }
        if (in->op == IR_ISUB && operand_equal(x, y) &&
            !operand_may_undef(x, may_undef, nold)) {
            *remove = true;
            *replacement_value = iconst_bits((IrType)in->type, 0);
            return true;
        }
        if (in->op == IR_IMUL) {
            IrOperand value = x;
            IrOperand constant = y;

            if (x.kind == IROP_ICONST && y.kind != IROP_ICONST) {
                value = y;
                constant = x;
            }
            if (constant.kind == IROP_ICONST &&
                power_of_two(constant.a, width, &shift)) {
                if (shift == 0) {
                    *remove = true;
                    *replacement_value = value;
                } else {
                    mutate_binary(in, IR_SHL, value,
                                  iconst_bits((IrType)in->type, shift));
                }
                return true;
            }
        }
        if (in->op == IR_ISUB && x_zero) {
            IrInst *inner = def_of(y, defs, nold);

            if (inner && inner->op == IR_ISUB && inner->nops == 2 &&
                inner->ops[0].kind == IROP_ICONST &&
                (inner->ops[0].a & mask) == 0) {
                *remove = true;
                *replacement_value =
                    resolve_operand(inner->ops[1], replacement, nold);
                return true;
            }
        }
        if (in->op == IR_XOR && y.kind == IROP_ICONST) {
            IrInst *inner = def_of(x, defs, nold);

            if (inner && inner->op == IR_XOR && inner->nops == 2 &&
                operand_equal(resolve_operand(inner->ops[1], replacement, nold),
                              y)) {
                *remove = true;
                *replacement_value =
                    resolve_operand(inner->ops[0], replacement, nold);
                return true;
            }
        }
        if ((in->op == IR_SDIV || in->op == IR_SREM || in->op == IR_UDIV ||
             in->op == IR_UREM) &&
            y.kind == IROP_ICONST && power_of_two(y.a, width, &shift)) {
            IrType type = (IrType)in->type;

            if ((in->op == IR_SDIV || in->op == IR_SREM) &&
                operand_may_undef(x, may_undef, nold))
                return false;
            /* In an N-bit signed type, the sign-bit constant denotes
             * -2^(N-1), not the positive power of two recognized by the
             * bit-pattern predicate.  The positive-divisor bias recipe is
             * therefore inapplicable. */
            if ((in->op == IR_SDIV || in->op == IR_SREM) && shift == width - 1)
                return false;
            if (shift == 0) {
                *remove = true;
                *replacement_value = (in->op == IR_SDIV || in->op == IR_UDIV)
                                         ? x
                                         : iconst_bits(type, 0);
                return true;
            }
            if (in->op == IR_UDIV) {
                mutate_binary(in, IR_LSHR, x, iconst_bits(type, shift));
                return true;
            }
            if (in->op == IR_UREM) {
                mutate_binary(in, IR_AND, x,
                              iconst_bits(type, (1ull << shift) - 1));
                return true;
            }
            {
                IrInst *sign =
                    insert2_before(m, f, block, block_id, prev, in, IR_ASHR,
                                   type, x, iconst_bits(type, width - 1));
                IrInst *bias =
                    insert2_before(m, f, block, block_id, prev, in, IR_LSHR,
                                   type, ir_op_value(f, sign->result),
                                   iconst_bits(type, width - shift));
                IrInst *adjusted =
                    insert2_before(m, f, block, block_id, prev, in, IR_IADD,
                                   type, x, ir_op_value(f, bias->result));
                IrOperand adjusted_op = ir_op_value(f, adjusted->result);

                /* -7/2: sign=-1, bias=1, adjusted=-6, ashr=-3.  A bare
                 * ashr would produce -4 because it rounds toward -inf. */
                if (in->op == IR_SDIV) {
                    mutate_binary(in, IR_ASHR, adjusted_op,
                                  iconst_bits(type, shift));
                } else {
                    IrInst *quotient = insert2_before(
                        m, f, block, block_id, prev, in, IR_ASHR, type,
                        adjusted_op, iconst_bits(type, shift));
                    IrInst *product =
                        insert2_before(m, f, block, block_id, prev, in, IR_SHL,
                                       type, ir_op_value(f, quotient->result),
                                       iconst_bits(type, shift));

                    mutate_binary(in, IR_ISUB, x,
                                  ir_op_value(f, product->result));
                }
                return true;
            }
        }
        if ((in->op == IR_LSHR || in->op == IR_ASHR) && y.kind == IROP_ICONST &&
            y.a < width) {
            IrInst *shifted = def_of(x, defs, nold);

            if (shifted && shifted->op == IR_SHL && shifted->nops == 2 &&
                shifted->ops[1].kind == IROP_ICONST &&
                shifted->ops[1].a == y.a) {
                IrOperand base =
                    resolve_operand(shifted->ops[0], replacement, nold);

                if (in->op == IR_LSHR) {
                    mutate_binary(
                        in, IR_AND, base,
                        iconst_bits((IrType)in->type, mask >> (u32)y.a));
                    return true;
                }
                {
                    IrInst *extend = def_of(base, defs, nold);
                    u32 narrow = width - (u32)y.a;

                    if (extend && extend->op == IR_ZEXT && extend->nops == 1 &&
                        int_width((IrType)extend->ops[0].type) == narrow) {
                        in->op = IR_SEXT;
                        in->ops[0] =
                            resolve_operand(extend->ops[0], replacement, nold);
                        in->nops = 1;
                        return true;
                    }
                }
            }
        }
    }

    if (in->op == IR_ICMP && in->subop == ICMP_ULT && in->nops == 2 &&
        y.kind == IROP_ICONST) {
        IrInst *extend = def_of(x, defs, nold);

        if (extend && extend->op == IR_ZEXT && extend->nops == 1) {
            IrOperand source =
                resolve_operand(extend->ops[0], replacement, nold);
            u32 source_width = int_width((IrType)source.type);
            u64 maximum = width_mask(source_width);
            u64 threshold = y.a & width_mask(int_width((IrType)y.type));

            if (threshold == 0) {
                *remove = true;
                *replacement_value = iconst_bits(IRT_I32, 0);
            } else if (threshold > maximum) {
                *remove = true;
                *replacement_value = iconst_bits(IRT_I32, 1);
            } else {
                in->ops[0] = source;
                in->ops[1] = iconst_bits((IrType)source.type, threshold);
            }
            return true;
        }
    }

    if (in->op == IR_FNEG && in->nops == 1) {
        IrInst *inner = def_of(x, defs, nold);

        if (inner && inner->op == IR_FNEG && inner->nops == 1) {
            *remove = true;
            *replacement_value =
                resolve_operand(inner->ops[0], replacement, nold);
            return true;
        }
    }
    return false;
}

static bool simplify_func(IrModule *m, IrFunc *f, const OptConfig *cfg)
{
    Arena scratch;
    IrOperand *replacement;
    IrInst **defs;
    bool *may_undef;
    u32 nold = f->nvals;
    u32 bi;
    bool changed = false;

    arena_init(&scratch);
    replacement = arena_alloc(&scratch, (nold + 1) * sizeof(*replacement),
                              _Alignof(IrOperand));
    defs =
        arena_alloc(&scratch, (nold + 1) * sizeof(*defs), _Alignof(IrInst *));
    may_undef = arena_alloc(&scratch, (nold + 1) * sizeof(*may_undef), 1);
    memset(replacement, 0, (nold + 1) * sizeof(*replacement));
    memset(may_undef, 0, (nold + 1) * sizeof(*may_undef));
    build_defs(f, defs, nold);
    find_may_undef(f, may_undef);

    for (bi = 0; bi < f->nblocks; bi++) {
        IrBlock *block = &f->blocks[bi];
        BlockId block_id = {bi + 1};
        IrInst *in = block->first;
        IrInst *prev = NULL;

        while (in) {
            IrInst *next = in->next;
            IrOperand value;
            bool remove;
            bool local_changed;
            u32 oi;

            for (oi = 0; oi < in->nops; oi++)
                in->ops[oi] = resolve_inst_operand(in->ops[oi], replacement,
                                                   nold, in->op == IR_CALL);
            local_changed =
                simplify_one(m, f, block, block_id, &prev, in, defs, may_undef,
                             replacement, nold, cfg, &remove, &value);
            if (remove) {
                replacement[in->result.v] = value;
                unlink_inst(block, prev, in, next);
            } else {
                prev = in;
            }
            changed |= local_changed;
            in = next;
        }
    }

    if (changed) {
        for (bi = 0; bi < f->nblocks; bi++) {
            IrInst *in;

            for (in = f->blocks[bi].first; in; in = in->next) {
                u32 oi, ei, ai;

                for (oi = 0; oi < in->nops; oi++)
                    in->ops[oi] = resolve_inst_operand(in->ops[oi], replacement,
                                                       nold, in->op == IR_CALL);
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

bool opt_simplify(IrModule *m, const OptConfig *cfg)
{
    bool changed = false;
    u32 i;

    for (i = 0; i < m->nfuncs; i++)
        changed |= simplify_func(m, &m->funcs[i], cfg);
    return changed;
}
