#include "cg/arm64/mir.h"

#include "target.h"

#include <string.h>

/* Sprint 47 selects ABI-neutral SSA into pre-register-allocation A64 MIR.
 * Calls retain their complete target-independent argument/result metadata,
 * but physical AAPCS64 placement remains deliberately unavailable:
 * lower/abi.c still describes SysV x86-64, and assigning physical registers
 * here would bake the wrong calling convention into MIR. Sprint 48 owns that
 * boundary. Final instruction offsets likewise do not exist until register
 * allocation inserts spills and frames; peep.c exposes relaxation for
 * synthetic/finalized MIR, but the Sprint 48 pipeline will invoke it. */

typedef struct ValInfo {
    A64Reg reg;
    A64Reg cmp_lhs;
    A64Reg addr_index;
    A64Reg zero_lhs;
    i64 addr_disp;
    u32 cmp_producer;
    u32 cmp_block;
    u32 first_inst;
    u32 ninst;
    u32 inst_block;
    u8 type;
    u8 cmp_cond;
    u8 test_bit;
    u8 addr_mode;
    bool addr_pattern;
    bool bit_test;
    bool zero_cmp;
} ValInfo;

typedef struct Isel {
    Arena *arena;
    const IrModule *module;
    const IrFunc *ir;
    A64Func *func;
    ValInfo *vals;
    const IrInst **defs;
    u32 *use_count;
    u32 cur;
    u32 cur_loc;
    u32 selection_start;
    u32 selection_block;
    /* A 9-16 byte composite return travels in x0:x1, but the IR keeps it
     * sret-SHAPED: the callee writes through a hidden pointer. The buffer
     * that pointer names is ours, and `ret` loads the pair out of it. */
    A64Reg pair_ret_buf;
    u32 hfa_leaves; /* nonzero: this function returns an HFA in v0-v(n-1) */
    A64Sf hfa_sf;   /* its leaf width */
} Isel;

static A64Sf sf_of(IrType type)
{
    switch (type) {
    case IRT_I8:
    case IRT_I16:
    case IRT_I32:
    case IRT_F32:
        return A64_SF32;
    case IRT_I64:
    case IRT_PTR:
    case IRT_F64:
        return A64_SF64;
    case IRT_V16I8:
    case IRT_V8I16:
    case IRT_V4I32:
    case IRT_V2I64:
    case IRT_V4F32:
    case IRT_V2F64:
    /* binary128 is a 16-byte FP value in a q register. Its ARITHMETIC is
     * soft-float (lower/f128.c rewrites it to libcgf_rt calls), but the
     * value itself moves, spills and travels the AAPCS64 SIMD queue like
     * any other FP type. */
    case IRT_F128:
        return A64_SF128;
    default:
        CGF_ICE("arm64 isel: type '%s' lands in Sprint 49", ir_type_name(type));
    }
}

static bool fp_type(IrType type)
{
    return type == IRT_F32 || type == IRT_F64 || type == IRT_F128;
}

static A64Reg new_reg(Isel *is, IrType type)
{
    /* Vectors live in the FP bank whatever their element type: NEON has one
     * register file, so a v4i32 and a v2f64 are both q registers. */
    bool fp = fp_type(type) || ir_type_is_vector(type);

    return a64_newv_width(is->func, fp ? A64RC_FP : A64RC_GP, sf_of(type));
}

static A64Block *block(Isel *is)
{
    return &is->func->blocks[is->cur];
}

static A64Operand reg_op(A64Reg reg)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_REG;
    op.reg = reg;
    return op;
}

static A64Operand imm_op(i64 value)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_IMM;
    op.imm = value;
    return op;
}

static A64Operand label_op(u32 id)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_LABEL;
    op.id = id;
    return op;
}

static A64Operand sym_op(u32 id)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_SYM;
    op.id = id;
    return op;
}

static A64Operand cpool_op(u32 index)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_CPOOL;
    op.id = index + 1;
    return op;
}

static A64Operand mem_op(A64Reg base, i64 offset, u8 size)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_MEM;
    op.mem.base = base;
    op.mem.offset = offset;
    op.mem.size = size;
    op.mem.mode = (u8)a64_isel_addr(offset, size, false, false);
    return op;
}

static A64Operand mem_reg_op(A64Reg base, A64Reg index, u8 size, u8 mode)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_MEM;
    op.mem.base = base;
    op.mem.index = index;
    op.mem.size = size;
    op.mem.mode = mode;
    return op;
}

static A64Inst *emit(Isel *is, A64Op opcode, A64Sf sf)
{
    A64Block *b = block(is);
    A64Inst inst;
    A64Inst *out;

    memset(&inst, 0, sizeof(inst));
    inst.op = (u16)opcode;
    inst.sf = (u8)sf;
    inst.loc = is->cur_loc;
    switch (opcode) {
    case A64_OP_ADDS:
    case A64_OP_SUBS:
    case A64_OP_ANDS:
    case A64_OP_FCMP:
        inst.flags |= A64IF_DEFS_NZCV;
        break;
    default:
        break;
    }
    a64_block_append(is->func, b, inst);
    out = &b->insts[b->n - 1];
    return out;
}

static void add_operand(A64Inst *inst, A64Operand op)
{
    if (inst->nops >= 4)
        CGF_ICE("arm64 isel: instruction operand capacity exceeded");
    inst->ops[inst->nops++] = op;
}

static void use_nzcv(Isel *is, A64Inst *inst, u32 producer)
{
    if (!producer || producer > block(is)->n)
        CGF_ICE("arm64 isel: missing NZCV producer");
    inst->flags |= A64IF_USES_NZCV;
    inst->flags_src = producer - 1;
}

static bool nzcv_available(const Isel *is, u32 producer)
{
    const A64Block *b = &is->func->blocks[is->cur];
    u32 i;

    if (!producer || producer > b->n)
        return false;
    for (i = producer; i < b->n; i++)
        if (b->insts[i].flags & A64IF_DEFS_NZCV)
            return false;
    return true;
}

static void erase_value_code(Isel *is, const IrInst *producer)
{
    ValInfo *removed;
    A64Block *b = block(is);
    u32 start, count, end, i;

    if (!producer || !producer->result.v)
        return;
    removed = &is->vals[producer->result.v];
    if (removed->inst_block != is->cur + 1 || !removed->ninst)
        return;
    start = removed->first_inst;
    count = removed->ninst;
    end = start + count;
    if (end > b->n || end > is->selection_start)
        CGF_ICE("arm64 isel: invalid folded-producer instruction range");

    memmove(&b->insts[start], &b->insts[end], (b->n - end) * sizeof(*b->insts));
    b->n -= count;
    is->selection_start -= count;

    for (i = 0; i < b->n; i++) {
        A64Inst *inst = &b->insts[i];

        if (!(inst->flags & A64IF_USES_NZCV))
            continue;
        if (inst->flags_src >= end)
            inst->flags_src -= count;
        else if (inst->flags_src >= start)
            CGF_ICE("arm64 isel: folded producer owns live NZCV");
    }
    for (i = 1; i <= is->ir->nvals; i++) {
        ValInfo *value = &is->vals[i];

        if (value->inst_block == is->cur + 1 && value != removed &&
            value->first_inst >= end)
            value->first_inst -= count;
        if (value->cmp_block != is->cur + 1 || !value->cmp_producer)
            continue;
        if (value->cmp_producer > end)
            value->cmp_producer -= count;
        else if (value->cmp_producer > start)
            CGF_ICE("arm64 isel: folded producer owns live compare flags");
    }
    removed->first_inst = 0;
    removed->ninst = 0;
}

static u32 add_block(Isel *is, const char *name)
{
    A64Func *f = is->func;

    if (f->nblocks == f->cap_blocks) {
        u32 nc = f->cap_blocks ? f->cap_blocks * 2 : 8;
        A64Block *blocks =
            arena_alloc(is->arena, nc * sizeof(*blocks), _Alignof(A64Block));

        if (f->nblocks)
            memcpy(blocks, f->blocks, f->nblocks * sizeof(*blocks));
        f->blocks = blocks;
        f->cap_blocks = nc;
    }
    memset(&f->blocks[f->nblocks], 0, sizeof(f->blocks[f->nblocks]));
    f->blocks[f->nblocks].name = name;
    return ++f->nblocks;
}

static A64Reg emit_mov_bits(Isel *is, u64 value, A64Sf sf)
{
    A64MovSynth synth[4];
    A64Reg dest = a64_newv_width(is->func, A64RC_GP, sf);
    u64 narrowed = sf == A64_SF32 ? value & 0xffffffffu : value;
    u32 n, i;

    n = a64_synth_mov_width(narrowed, sf == A64_SF32 ? 32 : 64, synth);
    for (i = 0; i < n; i++) {
        A64Op opcode = synth[i].kind == A64_MOV_ORR    ? A64_OP_ORR
                       : synth[i].kind == A64_MOV_MOVZ ? A64_OP_MOVZ
                       : synth[i].kind == A64_MOV_MOVN ? A64_OP_MOVN
                                                       : A64_OP_MOVK;
        A64Inst *inst = emit(is, opcode, sf);

        add_operand(inst, reg_op(dest));
        if (opcode == A64_OP_ORR) {
            add_operand(inst, reg_op(a64_phys(A64_XZR)));
            add_operand(inst, imm_op((i64)narrowed));
        } else {
            add_operand(inst, imm_op(synth[i].imm16));
            add_operand(inst, imm_op(synth[i].shift));
        }
    }
    return dest;
}

static A64Reg value_reg(Isel *is, ValueId id, IrType type)
{
    ValInfo *value = &is->vals[id.v];

    if (!value->reg.id)
        value->reg = new_reg(is, type);
    return value->reg;
}

static A64Reg to_gp(Isel *is, const IrOperand *operand)
{
    switch (operand->kind) {
    case IROP_VALUE:
        return value_reg(is, (ValueId){(u32)operand->a}, (IrType)operand->type);
    case IROP_ICONST:
        return emit_mov_bits(is, operand->a, sf_of((IrType)operand->type));
    case IROP_SYMBOL: {
        A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF64);
        A64Inst *inst = emit(is, A64_OP_ADDR, A64_SF64);

        add_operand(inst, reg_op(dest));
        add_operand(inst, sym_op(operand->sym + 1));
        if ((i64)operand->a)
            add_operand(inst, imm_op((i64)operand->a));
        return dest;
    }
    case IROP_UNDEF:
        return new_reg(is, (IrType)operand->type);
    default:
        CGF_ICE("arm64 isel: non-integer operand kind %u", operand->kind);
    }
}

/* Load a 16-byte constant out of the function's pool.
 *
 * adrp/add then a plain `ldr q`, rather than the load-folded `:lo12:` form:
 * that form's immediate is SCALED by the access size, so it constrains the
 * symbol's alignment. The two-instruction form is the same one every global
 * address already uses and is differential-tested against gas. */
static A64Reg emit_cpool_load(Isel *is, u64 lo, u64 hi)
{
    u32 index = a64_cpool_add(is->func, lo, hi);
    A64Reg addr = a64_newv_width(is->func, A64RC_GP, A64_SF64);
    A64Reg dest = a64_newv_width(is->func, A64RC_FP, A64_SF128);
    A64Inst *inst = emit(is, A64_OP_ADDR, A64_SF64);

    add_operand(inst, reg_op(addr));
    add_operand(inst, cpool_op(index));
    inst = emit(is, A64_OP_LOAD, A64_SF128);
    add_operand(inst, reg_op(dest));
    add_operand(inst, mem_op(addr, 0, 16));
    return dest;
}

static A64Reg to_fp(Isel *is, const IrOperand *operand)
{
    IrType type = (IrType)operand->type;

    switch (operand->kind) {
    case IROP_VALUE:
        return value_reg(is, (ValueId){(u32)operand->a}, type);
    case IROP_FCONST: {
        A64Sf sf = sf_of(type);
        A64Reg dest;
        A64Inst *inst;
        u8 encoded;

        /* binary128 has no immediate form of any kind: movz/movk reach only
         * general registers and `fmov` immediates top out at 64 bits. Both
         * halves come from memory, which is also why the operand carries
         * `b` -- the high 64 bits Sprint 15 kept exact. */
        if (type == IRT_F128)
            return emit_cpool_load(is, operand->a, operand->b);
        dest = new_reg(is, type);
        if (operand->a == 0) {
            inst = emit(is, A64_OP_FMOV, sf);
            add_operand(inst, reg_op(dest));
            add_operand(inst, reg_op(a64_phys(A64_XZR)));
        } else if (a64_fp_imm_encode(operand->a, sf == A64_SF32 ? 32 : 64,
                                     &encoded)) {
            inst = emit(is, A64_OP_FMOV, sf);
            add_operand(inst, reg_op(dest));
            add_operand(inst, imm_op(encoded));
        } else {
            A64Reg bits = emit_mov_bits(is, operand->a, sf);

            inst = emit(is, A64_OP_FMOV, sf);
            add_operand(inst, reg_op(dest));
            add_operand(inst, reg_op(bits));
        }
        return dest;
    }
    case IROP_UNDEF:
        return new_reg(is, type);
    default:
        CGF_ICE("arm64 isel: non-FP operand kind %u", operand->kind);
    }
}

/* The per-argument ABI annotation lives in IrOperand.b, but ONLY for VALUE
 * and SYMBOL operands: on an FCONST that same field carries the high 64 bits
 * of an f80/f128 constant, and reading it as an annotation makes a large
 * long-double literal look like a pair/sret hidden pointer. print.c,
 * verify.c and the x86 selector all scope the read this way; this is the
 * same class of mistake as binding the hidden return pointer from
 * param_annots. */
static u64 arg_annot(const IrOperand *operand)
{
    return operand->kind == IROP_VALUE || operand->kind == IROP_SYMBOL
               ? operand->b
               : 0;
}

static A64Reg to_reg(Isel *is, const IrOperand *operand)
{
    return fp_type((IrType)operand->type) ? to_fp(is, operand)
                                          : to_gp(is, operand);
}

static A64Cond int_cond(IrIcmp pred)
{
    static const u8 table[] = {
        A64_CC_EQ, A64_CC_NE, A64_CC_LT, A64_CC_LE, A64_CC_GT,
        A64_CC_GE, A64_CC_LO, A64_CC_LS, A64_CC_HI, A64_CC_HS,
    };

    if ((u32)pred >= sizeof(table) / sizeof(table[0]))
        CGF_ICE("arm64 isel: invalid integer predicate %u", pred);
    return (A64Cond)table[pred];
}

static A64FpPred fp_pred(IrFcmp pred)
{
    static const u8 table[] = {
        A64_FP_OEQ, A64_FP_ONE, A64_FP_OLT, A64_FP_OLE, A64_FP_OGT,
        A64_FP_OGE, A64_FP_ORD, A64_FP_UEQ, A64_FP_UNE, A64_FP_ULT,
        A64_FP_ULE, A64_FP_UGT, A64_FP_UGE, A64_FP_UNO,
    };

    if ((u32)pred >= sizeof(table) / sizeof(table[0]))
        CGF_ICE("arm64 isel: invalid FP predicate %u", pred);
    return (A64FpPred)table[pred];
}

static A64Reg emit_cset(Isel *is, A64Cond cond, u32 producer)
{
    A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF32);
    A64Inst *inst = emit(is, A64_OP_CSET, A64_SF32);

    inst->cond = (u8)cond;
    add_operand(inst, reg_op(dest));
    use_nzcv(is, inst, producer);
    return dest;
}

static A64Reg emit_fp_bool(Isel *is, A64FpPred pred, u32 producer)
{
    const A64FpCondMap *map = &a64_fp_cond_map[pred];
    A64Reg first = emit_cset(is, (A64Cond)map->first, producer);

    if (map->second == A64_CC_AL)
        return first;
    {
        A64Reg second = emit_cset(is, (A64Cond)map->second, producer);
        A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF32);
        A64Inst *inst =
            emit(is, map->combine_or ? A64_OP_ORR : A64_OP_AND, A64_SF32);

        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(first));
        add_operand(inst, reg_op(second));
        return dest;
    }
}

static A64Op binary_opcode(IrOp op)
{
    switch (op) {
    case IR_IADD:
        return A64_OP_ADD;
    case IR_ISUB:
        return A64_OP_SUB;
    case IR_IMUL:
        return A64_OP_MUL;
    case IR_AND:
        return A64_OP_AND;
    case IR_OR:
        return A64_OP_ORR;
    case IR_XOR:
        return A64_OP_EOR;
    case IR_SHL:
        return A64_OP_LSL;
    case IR_LSHR:
        return A64_OP_LSR;
    case IR_ASHR:
        return A64_OP_ASR;
    default:
        CGF_ICE("arm64 isel: invalid binary opcode %u", op);
    }
}

static bool logical_opcode(IrOp op)
{
    return op == IR_AND || op == IR_OR || op == IR_XOR;
}

static bool shift_opcode(IrOp op)
{
    return op == IR_SHL || op == IR_LSHR || op == IR_ASHR;
}

static A64Operand binary_rhs(Isel *is, IrOp op, const IrOperand *rhs, A64Sf sf,
                             A64Op *opcode)
{
    unsigned width = sf == A64_SF32 ? 32 : 64;

    if (rhs->kind == IROP_ICONST) {
        i64 value = (i64)rhs->a;
        i64 canonical = value;
        A64AddSubImm addsub;
        u32 logical;

        if (op == IR_ISUB && value == 0) {
            *opcode = A64_OP_SUB;
            return imm_op(0);
        }
        if (op == IR_ISUB && value != INT64_MIN)
            canonical = -value;
        if ((op == IR_IADD || (op == IR_ISUB && value != INT64_MIN)) &&
            a64_addsub_imm(canonical, &addsub)) {
            *opcode = addsub.is_sub ? A64_OP_SUB : A64_OP_ADD;
            /* Preserve the optional LSL #12 bit beside the immediate. */
            return imm_op((i64)addsub.imm12 << addsub.shift);
        }
        if (logical_opcode(op) &&
            a64_logical_imm_encode(rhs->a, width, &logical))
            return imm_op(
                (i64)(rhs->a & (sf == A64_SF32 ? 0xffffffffu : ~(u64)0)));
        if (shift_opcode(op) && (u64)value < width)
            return imm_op(value);
    }
    return reg_op(to_gp(is, rhs));
}

static A64Reg materialize_address(Isel *is, A64Reg base, i64 offset)
{
    A64AddSubImm addsub;
    A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF64);
    A64Inst *inst;

    if (a64_addsub_imm(offset, &addsub)) {
        inst = emit(is, addsub.is_sub ? A64_OP_SUB : A64_OP_ADD, A64_SF64);
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(base));
        add_operand(inst, imm_op((i64)addsub.imm12 << addsub.shift));
    } else {
        A64Reg off = emit_mov_bits(is, (u64)offset, A64_SF64);

        inst = emit(is, A64_OP_ADD, A64_SF64);
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(base));
        add_operand(inst, reg_op(off));
    }
    return dest;
}

static A64Operand address_operand(Isel *is, const IrOperand *address, u8 size)
{
    A64Reg base;
    i64 offset = 0;
    A64AddrMode mode;

    if (address->kind == IROP_VALUE && is->vals[(u32)address->a].addr_pattern) {
        u32 id = (u32)address->a;
        const IrInst *definition = is->defs[id];
        const ValInfo *value = &is->vals[id];

        base = value->cmp_lhs;
        if (is->use_count[id] == 1) {
            /* Erasing the PTRADD also erases any constant/symbol operand
             * materialization selected in its range.  Reselect the raw base
             * so the folded memory operand never retains an undefined vreg. */
            erase_value_code(is, definition);
            base = to_gp(is, &definition->ops[0]);
        }
        if (value->addr_index.id)
            return mem_reg_op(base, value->addr_index, size, value->addr_mode);
        offset = value->addr_disp;
    } else {
        base = to_gp(is, address);
    }
    mode = a64_isel_addr(offset, size, false, false);
    if (mode == A64_ADDR_MATERIALIZE) {
        base = materialize_address(is, base, offset);
        offset = 0;
    }
    return mem_op(base, offset, size);
}

static A64Operand offset_address_operand(Isel *is, A64Reg base, i64 offset,
                                         u8 size)
{
    A64AddrMode mode = a64_isel_addr(offset, size, false, false);

    if (mode == A64_ADDR_MATERIALIZE) {
        base = materialize_address(is, base, offset);
        offset = 0;
    }
    return mem_op(base, offset, size);
}

typedef struct ParallelMove {
    A64Reg dest;
    A64Reg source;
    u8 sf;
    u8 reg_class;
    bool done;
} ParallelMove;

static void parallel_moves(Isel *is, ParallelMove *moves, u32 count)
{
    u32 left = count;

    while (left) {
        u32 i, j;
        bool progress = false;

        for (i = 0; i < count; i++) {
            bool pending_source = false;

            if (moves[i].done)
                continue;
            for (j = 0; j < count; j++)
                if (!moves[j].done && j != i &&
                    moves[j].source.id == moves[i].dest.id &&
                    moves[j].source.physical == moves[i].dest.physical)
                    pending_source = true;
            if (pending_source)
                continue;
            {
                A64Inst *inst = emit(
                    is,
                    moves[i].reg_class == A64RC_FP ? A64_OP_FMOV : A64_OP_MOV,
                    (A64Sf)moves[i].sf);

                add_operand(inst, reg_op(moves[i].dest));
                add_operand(inst, reg_op(moves[i].source));
            }
            moves[i].done = true;
            left--;
            progress = true;
        }
        if (!progress) {
            A64Reg scratch;

            for (i = 0; i < count && moves[i].done; i++)
                ;
            scratch = a64_newv_width(is->func, (A64RegClass)moves[i].reg_class,
                                     (A64Sf)moves[i].sf);
            {
                A64Inst *inst = emit(
                    is,
                    moves[i].reg_class == A64RC_FP ? A64_OP_FMOV : A64_OP_MOV,
                    (A64Sf)moves[i].sf);

                add_operand(inst, reg_op(scratch));
                add_operand(inst, reg_op(moves[i].dest));
            }
            for (j = 0; j < count; j++)
                if (!moves[j].done && moves[j].source.id == moves[i].dest.id &&
                    moves[j].source.physical == moves[i].dest.physical)
                    moves[j].source = scratch;
        }
    }
}

static void edge_moves(Isel *is, const IrEdge *edge)
{
    const IrBlock *target = ir_block((IrFunc *)is->ir, edge->target);
    ParallelMove *moves;
    u32 i, count;

    if (!target || !edge->nargs)
        return;
    count = edge->nargs < target->nparams ? edge->nargs : target->nparams;
    moves = arena_alloc(is->arena, (count ? count : 1) * sizeof(*moves),
                        _Alignof(ParallelMove));
    memset(moves, 0, count * sizeof(*moves));
    for (i = 0; i < count; i++) {
        IrType type = (IrType)edge->args[i].type;

        moves[i].dest = value_reg(is, target->params[i], type);
        moves[i].source = to_reg(is, &edge->args[i]);
        moves[i].sf = (u8)sf_of(type);
        moves[i].reg_class = fp_type(type) ? A64RC_FP : A64RC_GP;
    }
    parallel_moves(is, moves, count);
}

static u32 edge_target(Isel *is, const IrEdge *edge)
{
    u32 saved, split;
    A64Inst *branch;

    if (!edge->nargs)
        return edge->target.v;
    saved = is->cur;
    split = add_block(is, "split");
    is->cur = split - 1;
    edge_moves(is, edge);
    branch = emit(is, A64_OP_B, A64_SF64);
    add_operand(branch, label_op(edge->target.v));
    is->cur = saved;
    return split;
}

static void bind_result(Isel *is, const IrInst *ir, A64Reg produced)
{
    A64Reg stable;

    if (!ir->result.v)
        return;
    stable = is->vals[ir->result.v].reg;
    if (stable.id &&
        (stable.id != produced.id || stable.physical != produced.physical)) {
        A64Inst *move =
            emit(is, fp_type((IrType)ir->type) ? A64_OP_FMOV : A64_OP_MOV,
                 sf_of((IrType)ir->type));

        add_operand(move, reg_op(stable));
        add_operand(move, reg_op(produced));
    } else {
        is->vals[ir->result.v].reg = produced;
    }
    is->vals[ir->result.v].type = ir->type;
}

static const IrInst *value_def(const Isel *is, const IrOperand *op)
{
    return op->kind == IROP_VALUE ? is->defs[(u32)op->a] : NULL;
}

static bool same_operand(const IrOperand *a, const IrOperand *b)
{
    return a->kind == b->kind && a->type == b->type && a->a == b->a &&
           a->sym == b->sym;
}

static bool iconst_is(const IrOperand *op, u64 value)
{
    return op->kind == IROP_ICONST && op->a == value;
}

static bool iconst_all_ones(const IrOperand *op)
{
    if (op->kind != IROP_ICONST)
        return false;
    return op->a ==
           (sf_of((IrType)op->type) == A64_SF32 ? 0xffffffffu : ~(u64)0);
}

static bool select_widening_mul(Isel *is, const IrInst *ir)
{
    const IrInst *lhs = value_def(is, &ir->ops[0]);
    const IrInst *rhs = value_def(is, &ir->ops[1]);
    bool sign;
    A64Reg lhs_reg;
    A64Reg rhs_reg;
    A64Reg dest;
    A64Inst *inst;

    if (!lhs || !rhs || (lhs->op != IR_SEXT && lhs->op != IR_ZEXT) ||
        rhs->op != lhs->op || lhs->nops != 1 || rhs->nops != 1 ||
        ir->type != IRT_I64 || lhs->ops[0].type != IRT_I32 ||
        rhs->ops[0].type != IRT_I32)
        return false;
    sign = lhs->op == IR_SEXT;
    if (is->use_count[lhs->result.v] == 1)
        erase_value_code(is, lhs);
    if (rhs != lhs && is->use_count[rhs->result.v] == 1)
        erase_value_code(is, rhs);
    lhs_reg = to_gp(is, &lhs->ops[0]);
    rhs_reg = to_gp(is, &rhs->ops[0]);
    dest = a64_newv_width(is->func, A64RC_GP, A64_SF64);
    inst = emit(is, sign ? A64_OP_SMULL : A64_OP_UMULL, A64_SF64);
    inst->src_sf = A64_SF32;
    add_operand(inst, reg_op(dest));
    add_operand(inst, reg_op(lhs_reg));
    add_operand(inst, reg_op(rhs_reg));
    bind_result(is, ir, dest);
    return true;
}

static bool select_mul_fusion(Isel *is, const IrInst *ir)
{
    const IrInst *mul = NULL;
    const IrInst *mul_lhs;
    const IrInst *mul_rhs;
    const IrOperand *addend = NULL;
    A64Op opcode = A64_OP_MADD;
    A64Sf sf = sf_of((IrType)ir->type);
    A64Reg lhs;
    A64Reg rhs;
    A64Reg acc = {0};
    A64Reg dest;
    A64Inst *inst;

    if (ir->op == IR_IADD) {
        mul = value_def(is, &ir->ops[0]);
        addend = &ir->ops[1];
        if (!mul || mul->op != IR_IMUL) {
            mul = value_def(is, &ir->ops[1]);
            addend = &ir->ops[0];
        }
        if (!mul || mul->op != IR_IMUL)
            return false;
    } else if (ir->op == IR_ISUB) {
        mul = value_def(is, &ir->ops[1]);
        if (!mul || mul->op != IR_IMUL)
            return false;
        if (iconst_is(&ir->ops[0], 0))
            opcode = A64_OP_MNEG;
        else {
            opcode = A64_OP_MSUB;
            addend = &ir->ops[0];
        }
    } else {
        return false;
    }
    if (mul->type != ir->type || mul->nops != 2 ||
        is->use_count[mul->result.v] != 1)
        return false;
    mul_lhs = value_def(is, &mul->ops[0]);
    mul_rhs = value_def(is, &mul->ops[1]);
    /* SMADDL/UMADDL are intentionally not in Sprint 47 MIR. Preserve a
     * selected SMULL/UMULL instead of reinterpreting its erased extensions
     * as ordinary 64-bit MADD inputs. */
    if (mul->type == IRT_I64 && mul_lhs && mul_rhs &&
        (mul_lhs->op == IR_SEXT || mul_lhs->op == IR_ZEXT) &&
        mul_rhs->op == mul_lhs->op && mul_lhs->nops == 1 &&
        mul_rhs->nops == 1 && mul_lhs->ops[0].type == IRT_I32 &&
        mul_rhs->ops[0].type == IRT_I32)
        return false;
    erase_value_code(is, mul);
    lhs = to_gp(is, &mul->ops[0]);
    rhs = to_gp(is, &mul->ops[1]);
    if (addend)
        acc = to_gp(is, addend);
    dest = a64_newv_width(is->func, A64RC_GP, sf);
    inst = emit(is, opcode, sf);
    add_operand(inst, reg_op(dest));
    add_operand(inst, reg_op(lhs));
    add_operand(inst, reg_op(rhs));
    if (addend)
        add_operand(inst, reg_op(acc));
    bind_result(is, ir, dest);
    return true;
}

static void select_binary(Isel *is, const IrInst *ir)
{
    IrOp ir_op = (IrOp)ir->op;
    A64Sf sf = sf_of((IrType)ir->type);
    A64Op opcode = binary_opcode(ir_op);
    A64Reg lhs;
    A64Operand rhs;
    A64Reg dest;
    A64Inst *inst;

    if (ir_op == IR_IMUL && select_widening_mul(is, ir))
        return;
    if ((ir_op == IR_IADD || ir_op == IR_ISUB) && select_mul_fusion(is, ir))
        return;
    lhs = to_gp(is, &ir->ops[0]);
    rhs = binary_rhs(is, ir_op, &ir->ops[1], sf, &opcode);
    dest = a64_newv_width(is->func, A64RC_GP, sf);
    inst = emit(is, opcode, sf);

    add_operand(inst, reg_op(dest));
    add_operand(inst, reg_op(lhs));
    add_operand(inst, rhs);
    bind_result(is, ir, dest);
}

static void select_divrem(Isel *is, const IrInst *ir)
{
    A64Sf sf = sf_of((IrType)ir->type);
    bool is_signed = ir->op == IR_SDIV || ir->op == IR_SREM;
    bool remainder = ir->op == IR_SREM || ir->op == IR_UREM;
    A64Reg lhs = to_gp(is, &ir->ops[0]);
    A64Reg rhs = to_gp(is, &ir->ops[1]);
    A64Reg quotient = a64_newv_width(is->func, A64RC_GP, sf);
    A64Inst *div = emit(is, is_signed ? A64_OP_SDIV : A64_OP_UDIV, sf);

    add_operand(div, reg_op(quotient));
    add_operand(div, reg_op(lhs));
    add_operand(div, reg_op(rhs));
    if (remainder) {
        A64Reg rem = a64_newv_width(is->func, A64RC_GP, sf);
        A64Inst *msub = emit(is, A64_OP_MSUB, sf);

        add_operand(msub, reg_op(rem));
        add_operand(msub, reg_op(quotient));
        add_operand(msub, reg_op(rhs));
        add_operand(msub, reg_op(lhs));
        bind_result(is, ir, rem);
    } else {
        bind_result(is, ir, quotient);
    }
}

static void select_icmp(Isel *is, const IrInst *ir)
{
    A64Sf sf = sf_of((IrType)ir->ops[0].type);
    A64Reg lhs = to_gp(is, &ir->ops[0]);
    A64Operand rhs;
    A64Op opcode = A64_OP_SUBS;
    A64Reg discard = a64_phys(A64_XZR);
    A64Inst *cmp;
    A64Cond cond = int_cond((IrIcmp)ir->subop);
    bool direct_branch = ir->result.v && ir->next &&
                         ir->next->op == IR_CONDBR &&
                         ir->next->ops[0].kind == IROP_VALUE &&
                         ir->next->ops[0].a == ir->result.v;
    bool direct_select = ir->result.v && ir->next &&
                         ir->next->op == IR_SELECT &&
                         ir->next->ops[0].kind == IROP_VALUE &&
                         ir->next->ops[0].a == ir->result.v;
    bool direct_consumer =
        (direct_branch || direct_select) && is->use_count[ir->result.v] == 1;
    bool zero_branch = direct_consumer && direct_branch &&
                       (ir->subop == ICMP_EQ || ir->subop == ICMP_NE) &&
                       ir->ops[1].kind == IROP_ICONST && ir->ops[1].a == 0;
    u32 producer;
    A64Reg result;

    if (zero_branch) {
        const IrInst *and_inst = value_def(is, &ir->ops[0]);
        const IrOperand *tested = NULL;
        u64 mask = 0;

        is->vals[ir->result.v].type = ir->type;
        is->vals[ir->result.v].cmp_cond = (u8)cond;
        is->vals[ir->result.v].zero_cmp = true;
        is->vals[ir->result.v].zero_lhs = lhs;
        if (and_inst && and_inst->op == IR_AND && and_inst->nops == 2) {
            if (and_inst->ops[0].kind == IROP_ICONST) {
                mask = and_inst->ops[0].a;
                tested = &and_inst->ops[1];
            } else if (and_inst->ops[1].kind == IROP_ICONST) {
                mask = and_inst->ops[1].a;
                tested = &and_inst->ops[0];
            }
        }
        if (tested && mask && !(mask & (mask - 1))) {
            u8 bit = 0;

            while ((mask >> bit) != 1)
                bit++;
            if (bit < (sf_of((IrType)tested->type) == A64_SF32 ? 32 : 64)) {
                is->vals[ir->result.v].bit_test = true;
                is->vals[ir->result.v].test_bit = bit;
                is->vals[ir->result.v].zero_lhs = to_gp(is, tested);
            }
        }
        if (is->vals[ir->result.v].bit_test &&
            is->use_count[and_inst->result.v] == 1)
            erase_value_code(is, and_inst);
        return;
    }
    rhs = binary_rhs(is, IR_ISUB, &ir->ops[1], sf, &opcode);
    /* A comparison always subtracts lhs-rhs. If the immediate canonicalizer
     * flipped a negative rhs to ADD, use ADDS with XZR as its discarded Rd. */
    opcode = opcode == A64_OP_ADD ? A64_OP_ADDS : A64_OP_SUBS;
    cmp = emit(is, opcode, sf);
    add_operand(cmp, reg_op(discard));
    add_operand(cmp, reg_op(lhs));
    add_operand(cmp, rhs);
    producer = block(is)->n;
    if (!direct_consumer) {
        result = emit_cset(is, cond, producer);
        bind_result(is, ir, result);
    } else {
        is->vals[ir->result.v].type = ir->type;
    }
    is->vals[ir->result.v].cmp_lhs = lhs;
    is->vals[ir->result.v].cmp_cond = (u8)cond;
    is->vals[ir->result.v].cmp_producer = producer;
    is->vals[ir->result.v].cmp_block = is->cur + 1;
}

static void select_fcmp(Isel *is, const IrInst *ir)
{
    A64Sf sf = sf_of((IrType)ir->ops[0].type);
    A64Reg lhs = to_fp(is, &ir->ops[0]);
    A64Reg rhs = to_fp(is, &ir->ops[1]);
    A64Inst *cmp = emit(is, A64_OP_FCMP, sf);
    A64Reg result;

    add_operand(cmp, reg_op(lhs));
    add_operand(cmp, reg_op(rhs));
    result = emit_fp_bool(is, fp_pred((IrFcmp)ir->subop), block(is)->n);
    bind_result(is, ir, result);
}

static void select_conversion(Isel *is, const IrInst *ir)
{
    IrType dst_type = (IrType)ir->type;
    IrType src_type = (IrType)ir->ops[0].type;
    A64Reg src = to_reg(is, &ir->ops[0]);
    A64Reg dest = new_reg(is, dst_type);
    A64Inst *inst;

    switch (ir->op) {
    case IR_SEXT: {
        unsigned src_bits = ir_type_size(src_type) * 8;
        unsigned dst_bits = sf_of(dst_type) == A64_SF32 ? 32 : 64;
        A64Reg shifted = a64_newv_width(is->func, A64RC_GP, sf_of(dst_type));

        inst = emit(is, A64_OP_LSL, sf_of(dst_type));
        add_operand(inst, reg_op(shifted));
        add_operand(inst, reg_op(src));
        add_operand(inst, imm_op(dst_bits - src_bits));
        inst = emit(is, A64_OP_ASR, sf_of(dst_type));
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(shifted));
        add_operand(inst, imm_op(dst_bits - src_bits));
        break;
    }
    case IR_ZEXT:
    case IR_TRUNC: {
        unsigned bits =
            ir_type_size(ir->op == IR_ZEXT ? src_type : dst_type) * 8;
        u64 mask = bits == 64 ? ~(u64)0 : ((u64)1 << bits) - 1;

        /* A w-register write is the architectural zero-extension for i32.
         * It also avoids the forbidden 32-bit logical immediate ~0u. */
        inst = emit(is, bits >= 32 ? A64_OP_MOV : A64_OP_AND,
                    bits == 32 ? A64_SF32 : sf_of(dst_type));
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(src));
        if (bits < 32)
            add_operand(inst, imm_op((i64)mask));
        break;
    }
    case IR_FPEXT:
    case IR_FPTRUNC:
        if (!fp_type(src_type) || !fp_type(dst_type))
            CGF_ICE("arm64 isel: f80/f128 conversion lands in Sprint 49");
        inst = emit(is, A64_OP_FCVT, sf_of(dst_type));
        inst->src_sf = sf_of(src_type);
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(src));
        break;
    case IR_FPTOSI:
    case IR_FPTOUI:
        inst = emit(is, ir->op == IR_FPTOSI ? A64_OP_FCVTZS : A64_OP_FCVTZU,
                    sf_of(dst_type));
        inst->src_sf = sf_of(src_type);
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(src));
        break;
    case IR_SITOFP:
    case IR_UITOFP:
        inst = emit(is, ir->op == IR_SITOFP ? A64_OP_SCVTF : A64_OP_UCVTF,
                    sf_of(dst_type));
        inst->src_sf = sf_of(src_type);
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(src));
        break;
    case IR_BITCAST:
        inst = emit(is,
                    fp_type(src_type) == fp_type(dst_type) ? A64_OP_MOV
                                                           : A64_OP_FMOV,
                    sf_of(dst_type));
        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(src));
        break;
    default:
        CGF_ICE("arm64 isel: invalid conversion opcode %u", ir->op);
    }
    bind_result(is, ir, dest);
}

/* Contraction: `a * b + c` becomes one fmadd with a SINGLE rounding step.
 * That changes results, which is why it happens only when the language
 * policy allows it — IrFunc.fp_contract, set from -ffp-contract and the
 * -std dialect.
 *
 * The multiply must have exactly one use, or fusing would compute it twice
 * (or, worse, leave a second consumer reading a value that no longer exists
 * as a separate instruction). */
static const IrInst *contractable_mul(Isel *is, const IrOperand *op,
                                      IrType type)
{
    const IrInst *def;

    if (!is->ir->fp_contract || op->kind != IROP_VALUE)
        return NULL;
    def = is->defs[(u32)op->a];
    if (!def || def->op != IR_FMUL || (IrType)def->type != type)
        return NULL;
    if (is->use_count[(u32)op->a] != 1)
        return NULL;
    return def;
}

static bool select_fma(Isel *is, const IrInst *ir)
{
    IrType type = (IrType)ir->type;
    const IrInst *mul;
    A64Reg a, b, c, dest;
    A64Inst *inst;
    bool negate;

    if (ir->op != IR_FADD && ir->op != IR_FSUB)
        return false;
    if (!fp_type(type))
        return false;
    /* For a subtract only the LEFT operand may be the multiply: `c - a * b`
     * is fmsub, but `a * b - c` is not — fmsub negates the PRODUCT. */
    mul = contractable_mul(is, &ir->ops[0], type);
    negate = false;
    if (!mul && ir->op == IR_FADD) {
        mul = contractable_mul(is, &ir->ops[1], type);
        if (!mul)
            return false;
        c = to_fp(is, &ir->ops[0]);
    } else if (mul && ir->op == IR_FADD) {
        c = to_fp(is, &ir->ops[1]);
    } else if (mul) {
        /* a * b - c  ==  -(c - a * b) is not a single fmsub, so leave the
         * subtract alone rather than inventing a sign flip. */
        return false;
    } else {
        mul = contractable_mul(is, &ir->ops[1], type);
        if (!mul)
            return false;
        c = to_fp(is, &ir->ops[0]);
        negate = true;
    }
    a = to_fp(is, &mul->ops[0]);
    b = to_fp(is, &mul->ops[1]);
    /* The multiply already selected into an fmul when its own instruction
     * was walked; folding it into the fmadd means deleting that code, or the
     * product would be computed twice. */
    erase_value_code(is, mul);
    dest = new_reg(is, type);
    inst = emit(is, negate ? A64_OP_FMSUB : A64_OP_FMADD, sf_of(type));
    add_operand(inst, reg_op(dest));
    add_operand(inst, reg_op(a));
    add_operand(inst, reg_op(b));
    add_operand(inst, reg_op(c));
    bind_result(is, ir, dest);
    return true;
}

static void select_fp_arith(Isel *is, const IrInst *ir)
{
    A64Sf sf = sf_of((IrType)ir->type);
    A64Op opcode = ir->op == IR_FADD   ? A64_OP_FADD
                   : ir->op == IR_FSUB ? A64_OP_FSUB
                   : ir->op == IR_FMUL ? A64_OP_FMUL
                   : ir->op == IR_FDIV ? A64_OP_FDIV
                                       : A64_OP_FNEG;
    A64Reg lhs = to_fp(is, &ir->ops[0]);
    A64Reg rhs = {0};
    A64Reg dest = new_reg(is, (IrType)ir->type);
    A64Inst *inst;

    if (ir->op != IR_FNEG)
        rhs = to_fp(is, &ir->ops[1]);
    inst = emit(is, opcode, sf);

    add_operand(inst, reg_op(dest));
    add_operand(inst, reg_op(lhs));
    if (ir->op != IR_FNEG)
        add_operand(inst, reg_op(rhs));
    bind_result(is, ir, dest);
}

static void select_memory_copy(Isel *is, const IrInst *ir, bool set)
{
    enum { A64_INLINE_MEM_MAX = 64 * 1024 };
    A64Reg dest;
    A64Reg source = {0};
    A64Reg pattern = {0};
    u64 size, offset = 0;

    if (ir->ops[2].kind != IROP_ICONST ||
        (set && ir->ops[1].kind != IROP_ICONST))
        CGF_ICE("arm64 isel: dynamic memcpy/memset lands in Sprint 49");
    size = ir->ops[2].a;
    if (size > A64_INLINE_MEM_MAX)
        CGF_ICE("arm64 isel: memcpy/memset larger than 65536 bytes lands in "
                "Sprint 49");
    dest = to_gp(is, &ir->ops[0]);
    if (!set)
        source = to_gp(is, &ir->ops[1]);
    else {
        u64 byte = ir->ops[1].a & 0xff;

        pattern = emit_mov_bits(is, byte * 0x0101010101010101ull, A64_SF64);
    }
    while (offset < size) {
        u8 step = size - offset >= 8   ? 8
                  : size - offset >= 4 ? 4
                  : size - offset >= 2 ? 2
                                       : 1;
        A64Reg temp = set ? pattern
                          : a64_newv_width(is->func, A64RC_GP,
                                           step == 8 ? A64_SF64 : A64_SF32);
        A64Operand address;
        A64Inst *inst;

        if (!set) {
            address = offset_address_operand(is, source, (i64)offset, step);
            inst = emit(is, A64_OP_LOAD, step == 8 ? A64_SF64 : A64_SF32);
            add_operand(inst, reg_op(temp));
            add_operand(inst, address);
        }
        address = offset_address_operand(is, dest, (i64)offset, step);
        inst = emit(is, A64_OP_STORE, step == 8 ? A64_SF64 : A64_SF32);
        add_operand(inst, reg_op(temp));
        add_operand(inst, address);
        offset += step;
    }
}

static A64Cond invert_cond(A64Cond cond)
{
    if (cond >= A64_CC_AL)
        CGF_ICE("arm64 isel: cannot invert condition %u", cond);
    return (A64Cond)(cond ^ 1u);
}

static bool unary_idiom(const Isel *is, const IrOperand *candidate, IrOp op,
                        u64 constant, const IrOperand *base)
{
    const IrInst *def = value_def(is, candidate);
    bool rhs_constant;
    bool lhs_constant;

    if (!def || def->op != op || def->nops != 2)
        return false;
    rhs_constant = op == IR_XOR ? iconst_all_ones(&def->ops[1])
                                : iconst_is(&def->ops[1], constant);
    lhs_constant = op == IR_XOR ? iconst_all_ones(&def->ops[0])
                                : iconst_is(&def->ops[0], constant);
    if (same_operand(&def->ops[0], base) && rhs_constant)
        return true;
    return (op == IR_IADD || op == IR_XOR) &&
           same_operand(&def->ops[1], base) && lhs_constant;
}

static bool select_conditional_idiom(Isel *is, const IrInst *ir, A64Cond cond,
                                     u32 producer)
{
    const IrOperand *yes = &ir->ops[1];
    const IrOperand *no = &ir->ops[2];
    const IrOperand *base = NULL;
    const IrInst *folded = NULL;
    A64Op opcode = A64_OP_CSEL;
    A64Reg source = {0};
    A64Reg dest;
    A64Inst *inst;

    if (iconst_all_ones(yes) && iconst_is(no, 0)) {
        opcode = A64_OP_CSETM;
    } else if (iconst_is(yes, 0) && iconst_all_ones(no)) {
        opcode = A64_OP_CSETM;
        cond = invert_cond(cond);
    } else if (unary_idiom(is, no, IR_IADD, 1, yes)) {
        opcode = A64_OP_CSINC;
        base = yes;
        folded = value_def(is, no);
    } else if (unary_idiom(is, yes, IR_IADD, 1, no)) {
        opcode = A64_OP_CSINC;
        base = no;
        folded = value_def(is, yes);
        cond = invert_cond(cond);
    } else if (unary_idiom(is, no, IR_XOR, ~(u64)0, yes)) {
        opcode = A64_OP_CSINV;
        base = yes;
        folded = value_def(is, no);
    } else if (unary_idiom(is, yes, IR_XOR, ~(u64)0, no)) {
        opcode = A64_OP_CSINV;
        base = no;
        folded = value_def(is, yes);
        cond = invert_cond(cond);
    } else if (unary_idiom(is, no, IR_ISUB, 0, yes)) {
        /* ISUB's negation form is 0-base, unlike the other unary idioms. */
        return false;
    } else {
        const IrInst *ndef = value_def(is, no);
        const IrInst *ydef = value_def(is, yes);

        if (ndef && ndef->op == IR_ISUB && ndef->nops == 2 &&
            iconst_is(&ndef->ops[0], 0) && same_operand(&ndef->ops[1], yes)) {
            opcode = A64_OP_CSNEG;
            base = yes;
            folded = ndef;
        } else if (ydef && ydef->op == IR_ISUB && ydef->nops == 2 &&
                   iconst_is(&ydef->ops[0], 0) &&
                   same_operand(&ydef->ops[1], no)) {
            opcode = A64_OP_CSNEG;
            base = no;
            folded = ydef;
            cond = invert_cond(cond);
        } else {
            return false;
        }
    }
    if (folded && is->use_count[folded->result.v] == 1) {
        const ValInfo *folded_value = &is->vals[folded->result.v];
        u32 end = folded_value->first_inst + folded_value->ninst;

        if (folded_value->inst_block == is->cur + 1 && folded_value->ninst) {
            if (producer > end)
                producer -= folded_value->ninst;
            else if (producer > folded_value->first_inst)
                CGF_ICE("arm64 isel: conditional idiom owns NZCV producer");
        }
        erase_value_code(is, folded);
    }
    if (base)
        source = to_reg(is, base);
    dest = new_reg(is, (IrType)ir->type);
    inst = emit(is, opcode, sf_of((IrType)ir->type));
    inst->cond = (u8)cond;
    add_operand(inst, reg_op(dest));
    if (base) {
        add_operand(inst, reg_op(source));
        add_operand(inst, reg_op(source));
    }
    use_nzcv(is, inst, producer);
    bind_result(is, ir, dest);
    return true;
}

/* The acquire/release and exclusive forms take a BARE base register: none
 * of ldar/stlr/ldaxr/stlxr has an offset field. Ordinary accesses fold an
 * offset into the addressing mode, so an atomic one must not go through
 * address_operand at all. */
static A64Operand atomic_addr(Isel *is, const IrOperand *ptr, u8 size)
{
    A64Operand op;

    memset(&op, 0, sizeof(op));
    op.kind = A64O_MEM;
    op.mem.base = to_gp(is, ptr);
    op.mem.size = size;
    op.mem.mode = A64_ADDR_SCALED;
    return op;
}

/* --- NEON (Sprint 49) -------------------------------------------------------
 *
 * The Sprint 36 vectorizer emits target-neutral IR, so the port is entirely
 * here and in emission. Two things are simpler than SSE2: ldr/str q never
 * fault on alignment, so there is no aligned/unaligned split to model, and
 * the element arrangement travels on the instruction rather than being
 * implied by the opcode.
 *
 * Reductions have no single NEON instruction for most operations. addv
 * exists for integer add, but mul/and/or/xor have nothing, so all of them
 * use the same halving sequence: rotate the register by half its width with
 * `ext`, apply the operation, repeat. Uniformity beats a special case that
 * only one opcode can take -- UPGRADE(neon-addv) marks where the shortcut
 * would go. */

static A64Reg new_vreg128(Isel *is)
{
    return a64_newv_width(is->func, A64RC_FP, A64_SF128);
}

static u16 vector_alu_op(IrOp op, bool fp)
{
    if (fp)
        return op == IR_FADD   ? A64_OP_VFADD
               : op == IR_FSUB ? A64_OP_VFSUB
               : op == IR_FMUL ? A64_OP_VFMUL
                               : A64_OP_VFDIV;
    switch (op) {
    case IR_IADD:
        return A64_OP_VADD;
    case IR_ISUB:
        return A64_OP_VSUB;
    case IR_IMUL:
        return A64_OP_VMUL;
    case IR_AND:
        return A64_OP_VAND;
    case IR_OR:
        return A64_OP_VORR;
    case IR_XOR:
        return A64_OP_VEOR;
    default:
        CGF_ICE("arm64 isel: IR op %u has no NEON form", (unsigned)op);
    }
}

static void select_vector_binary(Isel *is, const IrInst *ir)
{
    IrType vt = (IrType)ir->type;
    bool fp = vt == IRT_V4F32 || vt == IRT_V2F64;
    A64Reg a = to_reg(is, &ir->ops[0]);
    A64Reg b = to_reg(is, &ir->ops[1]);
    A64Reg dst = new_vreg128(is);
    A64Inst *inst = emit(is, vector_alu_op((IrOp)ir->op, fp), A64_SF128);

    inst->src_sf = (u8)vt;
    add_operand(inst, reg_op(dst));
    add_operand(inst, reg_op(a));
    add_operand(inst, reg_op(b));
    bind_result(is, ir, dst);
}

static void select_vsplat(Isel *is, const IrInst *ir)
{
    IrType vt = (IrType)ir->type;
    IrType et = ir_vector_elem_type(vt);
    bool fp = et == IRT_F32 || et == IRT_F64;
    A64Reg dst = new_vreg128(is);
    A64Inst *inst;

    if (fp) {
        /* the scalar already sits in the low lane of an FP register */
        A64Reg src = to_reg(is, &ir->ops[0]);

        inst = emit(is, A64_OP_VDUPLANE, A64_SF128);
        inst->src_sf = (u8)vt;
        add_operand(inst, reg_op(dst));
        add_operand(inst, reg_op(src));
    } else {
        A64Reg src = to_gp(is, &ir->ops[0]);

        inst = emit(is, A64_OP_VDUP, A64_SF128);
        inst->src_sf = (u8)vt;
        add_operand(inst, reg_op(dst));
        add_operand(inst, reg_op(src));
    }
    bind_result(is, ir, dst);
}

/* One lane out of a vector: umov for an integer element, a scalar mov for a
 * floating one (which is also how a reduction delivers its result). */
static void emit_lane_extract(Isel *is, IrType vt, A64Reg src, A64Reg dst,
                              u32 lane)
{
    IrType et = ir_vector_elem_type(vt);
    bool fp = et == IRT_F32 || et == IRT_F64;
    A64Inst *inst =
        emit(is, fp ? A64_OP_VLANE : A64_OP_VUMOV, a64_vec_lane_sf((u8)vt));

    inst->src_sf = (u8)vt;
    add_operand(inst, reg_op(dst));
    add_operand(inst, reg_op(src));
    add_operand(inst, imm_op((i64)lane));
}

static void select_vextract(Isel *is, const IrInst *ir)
{
    IrType vt = (IrType)ir->ops[0].type;
    A64Reg src = to_reg(is, &ir->ops[0]);
    A64Reg dst = new_reg(is, (IrType)ir->type);

    emit_lane_extract(is, vt, src, dst, ir->subop);
    bind_result(is, ir, dst);
}

static void select_vreduce(Isel *is, const IrInst *ir)
{
    IrType vt = (IrType)ir->ops[0].type;
    IrType et = ir_vector_elem_type(vt);
    bool fp = et == IRT_F32 || et == IRT_F64;
    u32 elem = ir_type_size(et);
    A64Reg cur = to_reg(is, &ir->ops[0]);
    A64Reg dst = new_reg(is, (IrType)ir->type);
    IrOp op = ir->op == IR_VREDUCE_ADD   ? (fp ? IR_FADD : IR_IADD)
              : ir->op == IR_VREDUCE_MUL ? (fp ? IR_FMUL : IR_IMUL)
              : ir->op == IR_VREDUCE_AND ? IR_AND
              : ir->op == IR_VREDUCE_OR  ? IR_OR
                                         : IR_XOR;
    u32 half;

    /* UPGRADE(neon-addv): integer add could use addv in one instruction.
     * Every other operation needs this halving anyway, so one shape serves
     * all five rather than a fast path only add can take. */
    for (half = 8; half >= elem; half >>= 1) {
        A64Reg rot = new_vreg128(is);
        A64Reg acc = new_vreg128(is);
        A64Inst *inst = emit(is, A64_OP_VEXT, A64_SF128);

        inst->src_sf = (u8)vt;
        add_operand(inst, reg_op(rot));
        add_operand(inst, reg_op(cur));
        add_operand(inst, reg_op(cur));
        add_operand(inst, imm_op((i64)half));

        inst = emit(is, vector_alu_op(op, fp), A64_SF128);
        inst->src_sf = (u8)vt;
        add_operand(inst, reg_op(acc));
        add_operand(inst, reg_op(cur));
        add_operand(inst, reg_op(rot));
        cur = acc;
    }
    emit_lane_extract(is, vt, cur, dst, 0);
    bind_result(is, ir, dst);
}

static void select_call(Isel *is, const IrInst *ir)
{
    u32 first = ir->subop == FUNCREF_INDIRECT ? 1u : 0u;
    A64Reg indirect = {0};
    A64Reg result = {0};
    A64Reg *args;
    u32 nargs = ir->nops - first;
    u8 abi_ret = IR_ABIRET_NONE;
    A64Inst *inst;
    A64CallInfo *call;
    u32 i;

    if (first)
        indirect = to_gp(is, &ir->ops[0]);
    if (ir->type != IRT_VOID)
        result = new_reg(is, (IrType)ir->type);
    args = arena_alloc(is->arena, (nargs ? nargs : 1) * sizeof(*args),
                       _Alignof(A64Reg));
    for (i = first; i < ir->nops; i++) {
        u32 kind = ir_arg_kind(arg_annot(&ir->ops[i]));

        args[i - first] = to_reg(is, &ir->ops[i]);
        if (kind >= IR_ARG_SRET && kind <= IR_ARG_PAIR_SS)
            abi_ret = (u8)(IR_ABIRET_SRET + (kind - IR_ARG_SRET));
    }
    inst = emit(is, A64_OP_CALL,
                ir->type == IRT_VOID ? A64_SF64 : sf_of((IrType)ir->type));
    call = a64_call_info_new(is->func, inst, ir->subop, ir->callee, indirect,
                             result, ir->type, abi_ret,
                             (ir->flags & IRF_CALL_VARIADIC) != 0,
                             (ir->flags & IRF_NORETURN) != 0);
    for (i = first; i < ir->nops; i++)
        a64_call_add_arg(is->func, call, args[i - first], ir->ops[i].type,
                         ir->ops[i].argflags, arg_annot(&ir->ops[i]));
    if (ir->type != IRT_VOID)
        bind_result(is, ir, result);
}

static void select_inst(Isel *is, const IrInst *ir)
{
    is->cur_loc = ir->loc;
    is->selection_start = block(is)->n;
    is->selection_block = is->cur + 1;
    switch (ir->op) {
    case IR_IADD:
    case IR_ISUB:
    case IR_IMUL:
    case IR_AND:
    case IR_OR:
    case IR_XOR:
    case IR_SHL:
    case IR_LSHR:
    case IR_ASHR:
        if (ir_type_is_vector((IrType)ir->type)) {
            select_vector_binary(is, ir);
            break;
        }
        select_binary(is, ir);
        break;
    case IR_SDIV:
    case IR_UDIV:
    case IR_SREM:
    case IR_UREM:
        select_divrem(is, ir);
        break;
    case IR_ICMP:
        select_icmp(is, ir);
        break;
    case IR_FCMP:
        select_fcmp(is, ir);
        break;
    case IR_FADD:
    case IR_FSUB:
    case IR_FMUL:
    case IR_FDIV:
    case IR_FNEG:
        if (ir_type_is_vector((IrType)ir->type)) {
            select_vector_binary(is, ir);
            break;
        }
        if (!fp_type((IrType)ir->type))
            CGF_ICE("arm64 isel: f80/f128 arithmetic lands in Sprint 49");
        if (select_fma(is, ir))
            break;
        select_fp_arith(is, ir);
        break;
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
        select_conversion(is, ir);
        break;
    case IR_ALLOCA: {
        A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF64);
        bool dynamic = ir->ops[0].kind != IROP_ICONST;
        A64Reg count = {0};
        A64Inst *inst;

        if (dynamic)
            count = to_gp(is, &ir->ops[0]);
        inst = emit(is, dynamic ? A64_OP_ALLOCA_DYN : A64_OP_ALLOCA, A64_SF64);

        add_operand(inst, reg_op(dest));
        if (!dynamic)
            add_operand(inst, imm_op((i64)ir->ops[0].a));
        else
            add_operand(inst, reg_op(count));
        add_operand(inst, imm_op(ir->align));
        bind_result(is, ir, dest);
        break;
    }
    case IR_LOAD: {
        A64Sf sf = sf_of((IrType)ir->type);
        A64Reg dest = new_reg(is, (IrType)ir->type);
        A64Operand address =
            (ir->flags & IRF_SEQ_CST)
                ? atomic_addr(is, &ir->ops[0], (u8)ir_type_size(ir->type))
                : address_operand(is, &ir->ops[0], ir_type_size(ir->type));
        A64Inst *inst = emit(is, A64_OP_LOAD, sf);

        if (ir->flags & IRF_VOLATILE)
            inst->flags |= A64IF_VOLATILE;
        if (ir->flags & IRF_SEQ_CST)
            inst->flags |= A64IF_ATOMIC;
        add_operand(inst, reg_op(dest));
        add_operand(inst, address);
        bind_result(is, ir, dest);
        break;
    }
    case IR_STORE: {
        A64Sf sf = sf_of((IrType)ir->ops[0].type);
        A64Reg value = to_reg(is, &ir->ops[0]);
        A64Operand address =
            (ir->flags & IRF_SEQ_CST)
                ? atomic_addr(is, &ir->ops[1],
                              (u8)ir_type_size(ir->ops[0].type))
                : address_operand(is, &ir->ops[1],
                                  ir_type_size(ir->ops[0].type));
        A64Inst *inst = emit(is, A64_OP_STORE, sf);

        if (ir->flags & IRF_VOLATILE)
            inst->flags |= A64IF_VOLATILE;
        if (ir->flags & IRF_SEQ_CST)
            inst->flags |= A64IF_ATOMIC;
        add_operand(inst, reg_op(value));
        add_operand(inst, address);
        break;
    }
    case IR_PTRADD: {
        A64Reg base = to_gp(is, &ir->ops[0]);
        A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF64);
        A64Op opcode = A64_OP_ADD;
        A64Operand rhs =
            binary_rhs(is, IR_IADD, &ir->ops[1], A64_SF64, &opcode);
        A64Inst *inst = emit(is, opcode, A64_SF64);

        add_operand(inst, reg_op(dest));
        add_operand(inst, reg_op(base));
        add_operand(inst, rhs);
        bind_result(is, ir, dest);
        if (ir->ops[1].kind == IROP_ICONST) {
            is->vals[ir->result.v].addr_pattern = true;
            is->vals[ir->result.v].cmp_lhs = base;
            is->vals[ir->result.v].addr_disp = (i64)ir->ops[1].a;
        } else if (ir->ops[1].kind == IROP_VALUE) {
            ValInfo *value = &is->vals[ir->result.v];

            value->addr_pattern = true;
            value->cmp_lhs = base;
            value->addr_index = to_gp(is, &ir->ops[1]);
            value->addr_mode =
                (u8)a64_addr_reg_mode(ir->ops[1].type == IRT_I32, false, false);
        }
        break;
    }
    case IR_MEMCPY:
        select_memory_copy(is, ir, false);
        break;
    case IR_MEMSET:
        select_memory_copy(is, ir, true);
        break;
    case IR_SELECT: {
        const ValInfo *cmp =
            ir->ops[0].kind == IROP_VALUE ? &is->vals[(u32)ir->ops[0].a] : NULL;
        bool fused = cmp && cmp->cmp_block == is->cur + 1 &&
                     nzcv_available(is, cmp->cmp_producer);
        A64Reg cond = {0};
        A64Reg yes;
        A64Reg no;
        A64Reg dest = new_reg(is, (IrType)ir->type);
        A64Inst *compare;
        A64Inst *select;
        u32 producer = fused ? cmp->cmp_producer : 0;

        if (!fused) {
            cond = to_gp(is, &ir->ops[0]);
            compare = emit(is, A64_OP_SUBS, A64_SF32);
            add_operand(compare, reg_op(a64_phys(A64_XZR)));
            add_operand(compare, reg_op(cond));
            add_operand(compare, imm_op(0));
            producer = block(is)->n;
        }
        if (!fp_type((IrType)ir->type) &&
            select_conditional_idiom(
                is, ir, fused ? (A64Cond)cmp->cmp_cond : A64_CC_NE, producer))
            break;
        yes = to_reg(is, &ir->ops[1]);
        no = to_reg(is, &ir->ops[2]);
        select =
            emit(is, fp_type((IrType)ir->type) ? A64_OP_FCSEL : A64_OP_CSEL,
                 sf_of((IrType)ir->type));
        select->cond = fused ? cmp->cmp_cond : A64_CC_NE;
        add_operand(select, reg_op(dest));
        add_operand(select, reg_op(yes));
        add_operand(select, reg_op(no));
        use_nzcv(is, select, producer);
        bind_result(is, ir, dest);
        break;
    }
    case IR_STACKSAVE: {
        A64Reg dest = a64_newv_width(is->func, A64RC_GP, A64_SF64);
        A64Inst *inst = emit(is, A64_OP_STACKSAVE, A64_SF64);

        add_operand(inst, reg_op(dest));
        bind_result(is, ir, dest);
        break;
    }
    case IR_STACKRESTORE: {
        A64Reg saved = to_gp(is, &ir->ops[0]);
        A64Inst *inst = emit(is, A64_OP_STACKRESTORE, A64_SF64);

        add_operand(inst, reg_op(saved));
        break;
    }
    case IR_BR: {
        A64Inst *branch;

        edge_moves(is, &ir->edges[0]);
        branch = emit(is, A64_OP_B, A64_SF64);
        add_operand(branch, label_op(ir->edges[0].target.v));
        break;
    }
    case IR_CONDBR: {
        u32 yes = edge_target(is, &ir->edges[0]);
        u32 no = edge_target(is, &ir->edges[1]);
        A64Inst *branch;

        if (ir->ops[0].kind == IROP_VALUE &&
            is->vals[(u32)ir->ops[0].a].zero_cmp) {
            const ValInfo *cmp = &is->vals[(u32)ir->ops[0].a];
            bool eq = cmp->cmp_cond == A64_CC_EQ;

            branch = emit(is,
                          cmp->bit_test ? (eq ? A64_OP_TBZ : A64_OP_TBNZ)
                                        : (eq ? A64_OP_CBZ : A64_OP_CBNZ),
                          a64_vwidth(is->func, cmp->zero_lhs));
            add_operand(branch, reg_op(cmp->zero_lhs));
            if (cmp->bit_test)
                add_operand(branch, imm_op(cmp->test_bit));
        } else if (ir->ops[0].kind == IROP_VALUE &&
                   is->vals[(u32)ir->ops[0].a].cmp_producer &&
                   is->vals[(u32)ir->ops[0].a].cmp_block == is->cur + 1 &&
                   nzcv_available(is,
                                  is->vals[(u32)ir->ops[0].a].cmp_producer)) {
            const ValInfo *cmp = &is->vals[(u32)ir->ops[0].a];

            branch = emit(is, A64_OP_BCOND, A64_SF64);
            branch->cond = cmp->cmp_cond;
            use_nzcv(is, branch, cmp->cmp_producer);
        } else {
            A64Reg cond = to_gp(is, &ir->ops[0]);

            branch = emit(is, A64_OP_CBNZ, A64_SF32);
            add_operand(branch, reg_op(cond));
        }
        add_operand(branch, label_op(yes));
        add_operand(branch, label_op(no));
        break;
    }
    case IR_SWITCH: {
        A64Reg value = to_gp(is, &ir->ops[0]);
        u32 i;

        for (i = 1; i < ir->nedges; i++) {
            A64Reg c = emit_mov_bits(is, (u64)ir->edges[i].case_val,
                                     sf_of((IrType)ir->ops[0].type));
            A64Inst *cmp =
                emit(is, A64_OP_SUBS, sf_of((IrType)ir->ops[0].type));
            A64Inst *branch;

            add_operand(cmp, reg_op(a64_phys(A64_XZR)));
            add_operand(cmp, reg_op(value));
            add_operand(cmp, reg_op(c));
            branch = emit(is, A64_OP_BCOND, A64_SF64);
            branch->cond = A64_CC_EQ;
            add_operand(branch, label_op(edge_target(is, &ir->edges[i])));
            use_nzcv(is, branch, block(is)->n - 1);
        }
        {
            A64Inst *branch = emit(is, A64_OP_B, A64_SF64);

            add_operand(branch, label_op(edge_target(is, &ir->edges[0])));
        }
        break;
    }
    case IR_RET: {
        A64Reg value = {0};
        A64Inst *ret;

        if (is->pair_ret_buf.id) {
            /* The value comes out of the buffer the hidden pointer named.
             * A 16-byte composite goes in x0:x1; an HFA goes in v0-v3 with
             * its own leaf width. Same shape, different register file --
             * which is precisely why IR_ABIRET_HFA_* had to be tellable
             * apart from IR_ABIRET_SRET. */
            bool hfa = ir->nops == 0 && is->hfa_leaves != 0;
            u32 n = hfa ? is->hfa_leaves : 2;
            A64Sf lsf = hfa ? is->hfa_sf : A64_SF64;
            u32 lsize = hfa ? (is->hfa_sf == A64_SF32 ? 4u : 8u) : 8u;
            u32 k;

            for (k = 0; k < n; k++) {
                A64Reg out =
                    a64_newv_fixed(is->func, hfa ? A64RC_FP : A64RC_GP, lsf,
                                   (u8)((hfa ? A64_V0 : A64_X0) + k));
                A64Inst *load = emit(is, A64_OP_LOAD, lsf);
                A64Operand mem;

                memset(&mem, 0, sizeof(mem));
                mem.kind = A64O_MEM;
                mem.mem.base = is->pair_ret_buf;
                mem.mem.offset = (i64)(lsize * k);
                mem.mem.size = (u8)lsize;
                mem.mem.mode = A64_ADDR_SCALED;
                add_operand(load, reg_op(out));
                add_operand(load, mem);
            }
            (void)emit(is, A64_OP_RET, A64_SF64);
            break;
        }
        if (ir->nops) {
            /* AAPCS64 returns in x0 or v0. Pre-colouring a copy keeps the
             * producing value free to live anywhere up to this point. */
            IrType ty = (IrType)ir->ops[0].type;
            bool fp = fp_type(ty);
            A64Reg out = a64_newv_fixed(is->func, fp ? A64RC_FP : A64RC_GP,
                                        sf_of(ty), (u8)(fp ? A64_V0 : A64_X0));
            A64Inst *move;

            value = to_reg(is, &ir->ops[0]);
            move = emit(is, fp ? A64_OP_FMOV : A64_OP_MOV, sf_of(ty));
            add_operand(move, reg_op(out));
            add_operand(move, reg_op(value));
            value = out;
        }
        ret = emit(is, A64_OP_RET, A64_SF64);

        if (ir->nops)
            add_operand(ret, reg_op(value));
        break;
    }
    case IR_UNREACHABLE:
        (void)emit(is, A64_OP_UNREACHABLE, A64_SF64);
        break;
    case IR_CALL:
        select_call(is, ir);
        break;
    case IR_VA_START: {
        /* The three pointer fields need the frame size, so the marker
         * survives to frame finalization; lowering has already stored the
         * two offsets, which are compile-time constants. */
        A64Reg ap = to_gp(is, &ir->ops[0]);
        A64Inst *inst = emit(is, A64_OP_VASTART, A64_SF64);

        add_operand(inst, reg_op(ap));
        break;
    }
    case IR_ATOMICRMW: {
        /* UPGRADE(armv8.1-lse): the armv8.0 baseline has no ldadd/cas, so
         * every read-modify-write is an exclusive loop. A feature tier may
         * replace these, but only behind real feature routing. */
        u8 size = (u8)ir_type_size(ir->type);
        A64Operand address = atomic_addr(is, &ir->ops[0], size);
        A64Reg val = to_gp(is, &ir->ops[1]);
        A64Reg dest = new_reg(is, (IrType)ir->type);
        A64Inst *inst = emit(is, A64_OP_ATOMIC_LLSC, sf_of((IrType)ir->type));

        inst->flags |= A64IF_ATOMIC;
        add_operand(inst, reg_op(dest));
        add_operand(inst, address);
        add_operand(inst, reg_op(val));
        add_operand(inst, imm_op((i64)ir->subop));
        bind_result(is, ir, dest);
        break;
    }
    case IR_CMPXCHG: {
        /* UPGRADE(armv8.1-lse): the cas family would collapse this loop. */
        u8 size = (u8)ir_type_size(ir->type);
        A64Operand address = atomic_addr(is, &ir->ops[0], size);
        A64Reg expected = to_gp(is, &ir->ops[1]);
        A64Reg desired = to_gp(is, &ir->ops[2]);
        A64Reg dest = new_reg(is, (IrType)ir->type);
        A64Inst *inst = emit(is, A64_OP_ATOMIC_CAS, sf_of((IrType)ir->type));

        inst->flags |= A64IF_ATOMIC;
        add_operand(inst, reg_op(dest));
        add_operand(inst, address);
        add_operand(inst, reg_op(expected));
        add_operand(inst, reg_op(desired));
        bind_result(is, ir, dest);
        break;
    }
    case IR_VSPLAT:
        select_vsplat(is, ir);
        break;
    case IR_VEXTRACT:
        select_vextract(is, ir);
        break;
    case IR_VREDUCE_ADD:
    case IR_VREDUCE_MUL:
    case IR_VREDUCE_AND:
    case IR_VREDUCE_OR:
    case IR_VREDUCE_XOR:
        select_vreduce(is, ir);
        break;
    default:
        CGF_ICE("arm64 isel: unhandled IR opcode %u", ir->op);
    }
    if (ir->result.v && is->selection_block == is->cur + 1) {
        ValInfo *value = &is->vals[ir->result.v];

        value->first_inst = is->selection_start;
        value->ninst = block(is)->n - is->selection_start;
        value->inst_block = is->cur + 1;
    }
}

/* Callee side of the AAPCS64 stage-C walk — the exact mirror of the argument
 * marshalling in regalloc.c, and it must stay that way: a caller and callee
 * that disagree about which register holds argument three fail silently.
 *
 * Incoming parameters arrive in fixed registers, so each one is copied out
 * of a pre-coloured virtual register into an ordinary one. Binding them to
 * the physical register directly would pin the value for the whole function
 * and make x0 unusable everywhere. */
static void bind_params(Isel *is, const IrFunc *ir)
{
    u32 ngrn = 0, nsrn = 0, nsaa = 0;
    u32 i;
    bool apple = cgf_target_selected().kind == CGF_TARGET_ARM64_MACOS;
    /* Sprint 19 shapes an aggregate return as a hidden POINTER parameter 0 --
     * the SysV convention, spelled into the IR. AAPCS64 disagrees twice, and
     * the difference is on the CALLEE side only (call lowering already gets
     * both right): an indirect result travels in x8 and consumes none of
     * x0-x7, and a 16-byte pair passes NO pointer at all, because the caller
     * reads the value straight out of x0:x1.
     *
     * The shape is named by IrFunc.abi_ret. It is NOT in param_annots, which
     * only ever carries byval (ir.h says so) -- reading it there made both
     * branches below dead code, so the hidden pointer fell through to the
     * ordinary GP queue and landed in x0, on top of the first real argument.
     * `mkp(30, 12)` then stored its result through address 30. */
    bool hidden_ret = ir->abi_ret != IR_ABIRET_NONE;

    is->cur = 0;
    is->func->variadic = ir->variadic;
    for (i = 0; i < ir->nparams; i++) {
        IrType ty = (IrType)ir->param_types[i];
        bool fp = fp_type(ty);
        A64Sf sf = sf_of(ty);
        A64Reg dst = is->vals[ir->param_vals[i].v].reg;
        u8 phys = A64_REG_NONE;

        if (i == 0 && hidden_ret && ir->abi_ret != IR_ABIRET_SRET) {
            /* Pair: nothing arrives. The callee still needs somewhere to
             * build the value, so it allocates that somewhere itself; IR_RET
             * loads x0:x1 back out of it. */
            A64Inst *slot = emit(is, A64_OP_ALLOCA, A64_SF64);

            add_operand(slot, reg_op(dst));
            add_operand(slot, imm_op(16));
            add_operand(slot, imm_op(8));
            is->pair_ret_buf = dst;
            if (ir->abi_ret >= IR_ABIRET_HFA_F32) {
                is->hfa_leaves = ir->abi_ret_n;
                is->hfa_sf =
                    ir->abi_ret == IR_ABIRET_HFA_F32 ? A64_SF32 : A64_SF64;
            }
            continue;
        }
        if (i == 0 && hidden_ret) {
            phys = A64_X8;
        } else if (fp) {
            if (nsrn < 8)
                phys = (u8)(A64_V0 + nsrn++);
            else
                nsrn = 8;
        } else {
            if (ngrn < 8)
                phys = (u8)(A64_X0 + ngrn++);
            else
                ngrn = 8;
        }
        if (phys == A64_REG_NONE) {
            A64Inst *load = emit(is, A64_OP_LOAD, sf);
            A64Operand mem;
            /* Row 3, the callee's half: Apple packs each stack argument at
             * its natural size and alignment, so reading eightbyte slots
             * here finds the NEXT argument. marshal_call computes the same
             * walk on the other side; the two must not drift. */
            u32 slot_bytes = apple ? ir_type_size(ty) : 8u;

            nsaa = (nsaa + slot_bytes - 1u) & ~(slot_bytes - 1u);
            memset(&mem, 0, sizeof(mem));
            mem.kind = A64O_MEM;
            mem.mem.base = a64_phys(A64_X29);
            mem.mem.offset = (i64)nsaa;
            mem.mem.mode = A64_ADDR_INCOMING;
            mem.mem.size = (u8)slot_bytes;
            add_operand(load, reg_op(dst));
            add_operand(load, mem);
            nsaa += slot_bytes;
            if (ir->variadic)
                CGF_ICE("arm64 isel: a variadic function with stack-passed "
                        "named parameters needs __stack biased past them "
                        "(Sprint 48)");
            continue;
        }
        {
            A64Reg src =
                a64_newv_fixed(is->func, fp ? A64RC_FP : A64RC_GP, sf, phys);
            A64Inst *move = emit(is, fp ? A64_OP_FMOV : A64_OP_MOV, sf);

            add_operand(move, reg_op(dst));
            add_operand(move, reg_op(src));
        }
    }
    is->func->va_named_gp = ngrn;
    is->func->va_named_fp = nsrn;
}

A64Func *a64_isel_function(const IrModule *module, const IrFunc *ir,
                           Arena *arena)
{
    Isel is;
    A64Func *func = arena_alloc(arena, sizeof(*func), _Alignof(A64Func));
    u32 bi, i;

    /* f128 is off this list: it rides the SIMD queue like any other FP type
     * once lower/f128.c has turned its arithmetic into calls. Vectors have
     * no AAPCS64 parameter contract in v0.1.0 (Sprint 36 declined to invent
     * one), and f80 does not exist on this target. */
    if (ir_type_is_vector((IrType)ir->ret) || ir->ret == IRT_F80)
        CGF_ICE("arm64 isel: vector/f80 function ABI lands in Sprint 51");

    for (i = 0; i < ir->nparams; i++)
        if (ir_type_is_vector((IrType)ir->param_types[i]) ||
            ir->param_types[i] == IRT_F80)
            CGF_ICE("arm64 isel: vector/f80 parameters land in Sprint 51");

    memset(func, 0, sizeof(*func));
    func->name = ir->name;
    func->arena = arena;
    func->m = module;
    memset(&is, 0, sizeof(is));
    is.arena = arena;
    is.module = module;
    is.ir = ir;
    is.func = func;
    is.vals = arena_alloc(arena, (ir->nvals + 1) * sizeof(*is.vals),
                          _Alignof(ValInfo));
    memset(is.vals, 0, (ir->nvals + 1) * sizeof(*is.vals));
    is.defs = arena_alloc(arena, (ir->nvals + 1) * sizeof(*is.defs),
                          _Alignof(IrInst *));
    memset(is.defs, 0, (ir->nvals + 1) * sizeof(*is.defs));
    is.use_count = arena_alloc(arena, (ir->nvals + 1) * sizeof(*is.use_count),
                               _Alignof(u32));
    memset(is.use_count, 0, (ir->nvals + 1) * sizeof(*is.use_count));

    for (bi = 0; bi < ir->nblocks; bi++)
        add_block(&is, ir->blocks[bi].name);
    for (i = 0; i < ir->nparams; i++)
        is.vals[ir->param_vals[i].v].reg =
            new_reg(&is, (IrType)ir->param_types[i]);
    bind_params(&is, ir);
    for (bi = 0; bi < ir->nblocks; bi++) {
        const IrBlock *ir_block = &ir->blocks[bi];
        const IrInst *inst;

        for (i = 0; i < ir_block->nparams; i++) {
            IrType type = (IrType)ir->vals[ir_block->params[i].v - 1].type;

            is.vals[ir_block->params[i].v].reg = new_reg(&is, type);
        }
        for (inst = ir_block->first; inst; inst = inst->next) {
            u32 oi, ei, ai;

            if (inst->result.v)
                is.defs[inst->result.v] = inst;

            for (oi = 0; oi < inst->nops; oi++)
                if (inst->ops[oi].kind == IROP_VALUE)
                    is.use_count[(u32)inst->ops[oi].a]++;
            for (ei = 0; ei < inst->nedges; ei++)
                for (ai = 0; ai < inst->edges[ei].nargs; ai++)
                    if (inst->edges[ei].args[ai].kind == IROP_VALUE)
                        is.use_count[(u32)inst->edges[ei].args[ai].a]++;
        }
    }
    for (bi = 0; bi < ir->nblocks; bi++) {
        const IrBlock *ir_block = &ir->blocks[bi];
        const IrInst *inst;

        is.cur = bi;
        for (inst = ir_block->first; inst; inst = inst->next)
            select_inst(&is, inst);
    }
    return func;
}
