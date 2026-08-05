#include "cg/arm64/mir.h"

#include <stdarg.h>
#include <stdio.h>

static const char *const op_names[] = {
    "mov",         "movz",        "movn",       "movk",      "add",
    "adds",        "sub",         "subs",       "and",       "ands",
    "orr",         "eor",         "lsl",        "lsr",       "asr",
    "mul",         "madd",        "msub",       "mneg",      "sdiv",
    "udiv",        "smull",       "umull",      "smulh",     "umulh",
    "csel",        "csinc",       "csinv",      "csneg",     "cset",
    "csetm",       "addr",        "load",       "store",     "ldp",
    "stp",         "b",           "b",          "cbz",       "cbnz",
    "tbz",         "tbnz",        "call",       "ret",       "br",
    "unreachable", "fmov",        "fadd",       "fsub",      "fmul",
    "fdiv",        "fsqrt",       "fneg",       "fabs",      "fcmp",
    "fcsel",       "scvtf",       "ucvtf",      "fcvtzs",    "fcvtzu",
    "fcvt",        "alloca",      "alloca_dyn", "stacksave", "stackrestore",
    "vastart",     "atomic_llsc", "atomic_cas", "vadd",      "vsub",
    "vmul",        "vand",        "vorr",       "veor",      "vfadd",
    "vfsub",       "vfmul",       "vfdiv",      "vdup",      "vduplane",
    "vext",        "vumov",       "vlane",
};

_Static_assert(sizeof(op_names) / sizeof(op_names[0]) == A64_OP_COUNT,
               "op_names covers every A64Op");

static const char *const cond_names[] = {
    "eq", "ne", "hs", "lo", "mi", "pl", "vs", "vc",
    "hi", "ls", "ge", "lt", "gt", "le", "al", "nv",
};

const char *a64_op_name(u16 op)
{
    return op < A64_OP_COUNT ? op_names[op] : "op?";
}

const char *a64_cond_name(u8 cond)
{
    return cond < 16 ? cond_names[cond] : "?";
}

/* NEON arrangement specifiers, keyed by the vector IrType. `add v0.4s` and
 * `add v0.8h` are distinct instructions over identical registers, so the
 * element shape has to travel with the instruction. */
/* `vN`, the spelling every arrangement form uses: `add v0.4s, ...`. The `qN`
 * spelling is reserved for whole-register loads and stores. */
const char *a64_vec_name(u8 reg)
{
    static const char *const names[] = {
        "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
        "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
        "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31",
    };

    if (reg < A64_V0 || reg > A64_V31)
        return "v?";
    return names[reg - A64_V0];
}

const char *a64_vec_arrangement(u8 t)
{
    switch (t) {
    case IRT_V16I8:
        return "16b";
    case IRT_V8I16:
        return "8h";
    case IRT_V4I32:
    case IRT_V4F32:
        return "4s";
    case IRT_V2I64:
    case IRT_V2F64:
        return "2d";
    default:
        return "?";
    }
}

/* The width a single lane occupies when it is moved out into a scalar. */
u8 a64_vec_lane_sf(u8 t)
{
    return t == IRT_V2I64 || t == IRT_V2F64 ? A64_SF64 : A64_SF32;
}

const char *a64_phys_name(u8 reg, u8 sf)
{
    static const char *const xnames[] = {
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",
        "x8",  "x9",  "x10", "x11", "x12", "x13", "x14", "x15",
        "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
        "x24", "x25", "x26", "x27", "x28", "x29", "x30",
    };
    static const char *const wnames[] = {
        "w0",  "w1",  "w2",  "w3",  "w4",  "w5",  "w6",  "w7",
        "w8",  "w9",  "w10", "w11", "w12", "w13", "w14", "w15",
        "w16", "w17", "w18", "w19", "w20", "w21", "w22", "w23",
        "w24", "w25", "w26", "w27", "w28", "w29", "w30",
    };
    static const char *const vnames64[] = {
        "d0",  "d1",  "d2",  "d3",  "d4",  "d5",  "d6",  "d7",
        "d8",  "d9",  "d10", "d11", "d12", "d13", "d14", "d15",
        "d16", "d17", "d18", "d19", "d20", "d21", "d22", "d23",
        "d24", "d25", "d26", "d27", "d28", "d29", "d30", "d31",
    };
    static const char *const vnames32[] = {
        "s0",  "s1",  "s2",  "s3",  "s4",  "s5",  "s6",  "s7",
        "s8",  "s9",  "s10", "s11", "s12", "s13", "s14", "s15",
        "s16", "s17", "s18", "s19", "s20", "s21", "s22", "s23",
        "s24", "s25", "s26", "s27", "s28", "s29", "s30", "s31",
    };

    if (reg <= A64_X30)
        return sf == A64_SF32 ? wnames[reg] : xnames[reg];
    if (reg == A64_SP)
        return sf == A64_SF32 ? "wsp" : "sp";
    if (reg == A64_XZR)
        return sf == A64_SF32 ? "wzr" : "xzr";
    if (reg >= A64_V0 && reg <= A64_V31) {
        static const char *const vnames128[] = {
            "q0",  "q1",  "q2",  "q3",  "q4",  "q5",  "q6",  "q7",
            "q8",  "q9",  "q10", "q11", "q12", "q13", "q14", "q15",
            "q16", "q17", "q18", "q19", "q20", "q21", "q22", "q23",
            "q24", "q25", "q26", "q27", "q28", "q29", "q30", "q31",
        };

        /* SF128 names the whole register for a load or store; the
         * arrangement forms spell the SAME register `vN` instead, which is
         * what a64_vec_name gives them. */
        if (sf == A64_SF128)
            return vnames128[reg - A64_V0];
        return sf == A64_SF32 ? vnames32[reg - A64_V0] : vnames64[reg - A64_V0];
    }
    if (reg == A64_NZCV)
        return "nzcv";
    return "r?";
}

static void print_reg(Buf *out, A64Reg reg, u8 sf)
{
    if (!reg.id) {
        buf_printf(out, "v?");
    } else if (reg.physical) {
        buf_printf(out, "%s", a64_phys_name((u8)(reg.id - 1), sf));
    } else {
        buf_printf(out, "v%u", reg.id);
    }
}

static void print_mem(Buf *out, const A64Mem *mem, u8 sf)
{
    buf_printf(out, "[");
    print_reg(out, mem->base, sf);
    if (mem->index.id) {
        buf_printf(out, ", ");
        print_reg(out, mem->index,
                  mem->mode == A64_ADDR_REG_LSL ? sf : A64_SF32);
        if (mem->mode == A64_ADDR_REG_UXTW)
            buf_printf(out, ", uxtw");
        else if (mem->mode == A64_ADDR_REG_SXTW)
            buf_printf(out, ", sxtw");
        else
            buf_printf(out, ", lsl");
        if (mem->shift)
            buf_printf(out, " #%u", mem->shift);
    } else if (mem->offset) {
        buf_printf(out, ", #%lld", (long long)mem->offset);
    }
    buf_printf(out, "]");
    if (mem->mode == A64_ADDR_PRE)
        buf_printf(out, "!");
    else if (mem->mode == A64_ADDR_POST)
        buf_printf(out, ", #%lld", (long long)mem->offset);
}

static void print_operand(Buf *out, const A64Func *f, const A64Operand *op,
                          u8 sf)
{
    switch (op->kind) {
    case A64O_REG:
        print_reg(out, op->reg, sf);
        break;
    case A64O_IMM:
        buf_printf(out, "#%lld", (long long)op->imm);
        break;
    case A64O_MEM:
        print_mem(out, &op->mem, sf);
        break;
    case A64O_LABEL:
        buf_printf(out, "bb%u", op->id);
        break;
    case A64O_SYM:
        if (f->m && op->id && op->id <= f->m->nsyms)
            buf_printf(out, "@%s", f->m->syms[op->id - 1]);
        else
            buf_printf(out, "@sym%u", op->id);
        break;
    default:
        buf_printf(out, "?");
        break;
    }
}

static bool op_has_size(u16 op)
{
    switch (op) {
    case A64_OP_B:
    case A64_OP_BCOND:
    case A64_OP_CALL:
    case A64_OP_RET:
    case A64_OP_BR:
    case A64_OP_UNREACHABLE:
    case A64_OP_ALLOCA:
    case A64_OP_ALLOCA_DYN:
    case A64_OP_STACKSAVE:
    case A64_OP_STACKRESTORE:
    case A64_OP_VASTART:
    case A64_OP_ATOMIC_LLSC:
    case A64_OP_ATOMIC_CAS:
        return false;
    default:
        return true;
    }
}

static bool op_has_cond(u16 op)
{
    return op == A64_OP_BCOND || op == A64_OP_CSEL || op == A64_OP_CSINC ||
           op == A64_OP_CSINV || op == A64_OP_CSNEG || op == A64_OP_CSET ||
           op == A64_OP_CSETM || op == A64_OP_FCSEL;
}

static bool op_is_fp(u16 op)
{
    return op >= A64_OP_FMOV && op <= A64_OP_FCVT;
}

static bool op_has_mixed_widths(u16 op)
{
    return op == A64_OP_SCVTF || op == A64_OP_UCVTF || op == A64_OP_FCVTZS ||
           op == A64_OP_FCVTZU || op == A64_OP_FCVT;
}

static const char *width_suffix(u16 op, u8 sf)
{
    if (op == A64_OP_FCVT)
        return sf == A64_SF32 ? "s" : "d";
    if (op == A64_OP_SCVTF || op == A64_OP_UCVTF)
        return sf == A64_SF32 ? "s" : "d";
    return sf == A64_SF32 ? "w" : "x";
}

static const char *source_width_suffix(u16 op, u8 sf)
{
    if (op == A64_OP_FCVTZS || op == A64_OP_FCVTZU || op == A64_OP_FCVT)
        return sf == A64_SF32 ? "s" : "d";
    return sf == A64_SF32 ? "w" : "x";
}

static u8 type_sf(u8 type)
{
    return type == IRT_I8 || type == IRT_I16 || type == IRT_I32 ||
                   type == IRT_F32
               ? A64_SF32
               : A64_SF64;
}

void a64_mir_print(const A64Func *f, Buf *out)
{
    u32 bi, ii, oi;

    buf_printf(out, "mir.a64 @%s (%s=%u)\n", f->name,
               f->allocated ? "pregs" : "vregs",
               f->allocated ? (u32)A64_REG_COUNT : f->nvregs);
    for (bi = 0; bi < f->nblocks; bi++) {
        const A64Block *block = &f->blocks[bi];

        buf_printf(out, "bb%u:\n", bi + 1);
        for (ii = 0; ii < block->n; ii++) {
            const A64Inst *inst = &block->insts[ii];

            buf_printf(out, "    %s", a64_op_name(inst->op));
            if (op_has_cond(inst->op))
                buf_printf(out, ".%s", a64_cond_name(inst->cond));
            if (op_has_size(inst->op)) {
                if (op_has_mixed_widths(inst->op))
                    buf_printf(out, ".%s.%s", width_suffix(inst->op, inst->sf),
                               source_width_suffix(inst->op, inst->src_sf));
                else if (op_is_fp(inst->op))
                    buf_printf(out, inst->sf == A64_SF32 ? ".s" : ".d");
                else
                    buf_printf(out, inst->sf == A64_SF32 ? ".w" : ".x");
            }
            if (inst->op == A64_OP_CALL && inst->call) {
                const A64CallInfo *call = inst->call;
                u32 ai;

                buf_printf(out, " ");
                if (call->callee_ref_kind == FUNCREF_INTERNAL) {
                    if (f->m && call->callee_id < f->m->nfuncs)
                        buf_printf(out, "@%s",
                                   f->m->funcs[call->callee_id].name);
                    else
                        buf_printf(out, "@func?%u", call->callee_id);
                } else if (call->callee_ref_kind == FUNCREF_EXTERNAL) {
                    if (f->m && call->callee_id < f->m->nsyms)
                        buf_printf(out, "@%s", f->m->syms[call->callee_id]);
                    else
                        buf_printf(out, "@sym?%u", call->callee_id);
                } else {
                    print_reg(out, call->indirect, A64_SF64);
                }
                buf_printf(out, "(");
                for (ai = 0; ai < call->nargs; ai++) {
                    if (ai)
                        buf_printf(out, ", ");
                    print_reg(out, call->args[ai].value,
                              type_sf(call->args[ai].type));
                    buf_printf(out, ":t%u/a%016llx", call->args[ai].type,
                               (unsigned long long)call->args[ai].abi_annot);
                }
                buf_printf(out, ")");
                if (call->result.id) {
                    buf_printf(out, " -> ");
                    print_reg(out, call->result, inst->sf);
                    buf_printf(out, ":t%u", call->result_type);
                }
                if (call->variadic)
                    buf_printf(out, " variadic");
                if (call->noreturn)
                    buf_printf(out, " noreturn");
            }
            for (oi = 0; oi < inst->nops && oi < 4; oi++) {
                buf_printf(out, oi ? ", " : " ");
                print_operand(out, f, &inst->ops[oi],
                              op_has_mixed_widths(inst->op) && oi == 1
                                  ? inst->src_sf
                                  : inst->sf);
            }
            buf_printf(out, "\n");
        }
    }
}

static bool reg_valid(const A64Func *f, A64Reg reg)
{
    if (!reg.id)
        return false;
    if (reg.physical)
        return reg.id <= A64_REG_COUNT;
    return reg.id <= f->nvregs;
}

static int verify_operand(const A64Func *f, const A64Operand *op)
{
    switch (op->kind) {
    case A64O_REG:
        return !reg_valid(f, op->reg);
    case A64O_IMM:
        return 0;
    case A64O_MEM:
        return !reg_valid(f, op->mem.base) ||
               (op->mem.index.id && !reg_valid(f, op->mem.index)) ||
               op->mem.mode > A64_ADDR_INCOMING || !op->mem.size;
    case A64O_LABEL:
        return !op->id || op->id > f->nblocks;
    case A64O_SYM:
        return !op->id;
    default:
        return 1;
    }
}

static bool operand_is_phys(const A64Operand *op, A64PhysReg reg)
{
    return op->kind == A64O_REG && op->reg.physical &&
           op->reg.id == (u32)reg + 1;
}

static bool mem_reg_is(const A64Operand *op, bool base, A64PhysReg reg)
{
    A64Reg found;

    if (op->kind != A64O_MEM)
        return false;
    found = base ? op->mem.base : op->mem.index;
    return found.physical && found.id == (u32)reg + 1;
}

typedef enum Reg31Meaning { R31_ZR, R31_SP, R31_FORBIDDEN } Reg31Meaning;

/* Return how encoding 31 is interpreted at one direct-register position.
 * The default integer/control rule is XZR.  Only the explicitly listed
 * immediate/extended positions name SP. Scalar-FP positions have no GP
 * register-31 spelling except either GP side of an FP/GP FMOV transfer. */
static Reg31Meaning reg31_meaning(const A64Func *f, const A64Inst *inst,
                                  u32 pos)
{
    bool immediate = inst->nops >= 3 && inst->ops[2].kind == A64O_IMM;
    bool extended = inst->nops == 4 && inst->ops[2].kind == A64O_REG;

    switch (inst->op) {
    case A64_OP_ADD:
    case A64_OP_SUB:
        if ((immediate || extended) && pos < 2)
            return R31_SP;
        return R31_ZR;
    case A64_OP_ADDS:
    case A64_OP_SUBS:
        if ((immediate || extended) && pos == 1)
            return R31_SP;
        return R31_ZR;
    case A64_OP_AND:
    case A64_OP_ORR:
    case A64_OP_EOR:
        if (immediate && pos == 0)
            return R31_SP;
        return R31_ZR;
    case A64_OP_FMOV:
        if (inst->nops == 2 && pos == 1 && inst->ops[0].kind == A64O_REG &&
            a64_vclass(f, inst->ops[0].reg) == A64RC_FP)
            return R31_ZR;
        if (inst->nops == 2 && pos == 0 && inst->ops[1].kind == A64O_REG &&
            a64_vclass(f, inst->ops[1].reg) == A64RC_FP)
            return R31_ZR;
        return R31_FORBIDDEN;
    case A64_OP_FADD:
    case A64_OP_FSUB:
    case A64_OP_FMUL:
    case A64_OP_FDIV:
    case A64_OP_FSQRT:
    case A64_OP_FNEG:
    case A64_OP_FABS:
    case A64_OP_FCMP:
    case A64_OP_FCVT:
    case A64_OP_FCSEL:
        return R31_FORBIDDEN;
    case A64_OP_SCVTF:
    case A64_OP_UCVTF:
        return pos == 1 ? R31_ZR : R31_FORBIDDEN;
    case A64_OP_FCVTZS:
    case A64_OP_FCVTZU:
        return pos == 0 ? R31_ZR : R31_FORBIDDEN;
    default:
        return R31_ZR;
    }
}

/* Encoding 31 names SP or XZR from the instruction position, not from the
 * five register bits. Check every direct register and both registers inside
 * every memory operand; an unlisted opcode therefore gets an explicit XZR
 * interpretation rather than accidentally bypassing the contract. */
static bool invalid_reg31_position(const A64Func *f, const A64Inst *inst)
{
    u32 i;

    for (i = 0; i < inst->nops; i++) {
        Reg31Meaning meaning;

        if (inst->ops[i].kind == A64O_MEM) {
            if (mem_reg_is(&inst->ops[i], true, A64_XZR) ||
                mem_reg_is(&inst->ops[i], false, A64_SP))
                return true;
            continue;
        }
        if (inst->ops[i].kind != A64O_REG)
            continue;
        meaning = reg31_meaning(f, inst, i);
        if (operand_is_phys(&inst->ops[i], A64_SP) && meaning != R31_SP)
            return true;
        if (operand_is_phys(&inst->ops[i], A64_XZR) && meaning != R31_ZR)
            return true;
    }
    return false;
}

static void verify_diag(DiagCtx *dc, const A64Func *f, u32 block, u32 inst,
                        const char *fmt, ...)
{
    char detail[256];
    va_list ap;
    Span sp = {0};

    va_start(ap, fmt);
    (void)vsnprintf(detail, sizeof(detail), fmt, ap);
    va_end(ap);
    diag_emit(dc, DIAG_ERROR, sp, "mir verify @%s bb%u:%u: %s", f->name,
              block + 1, inst, detail);
}

int a64_mir_verify(const A64Func *f, DiagCtx *dc)
{
    int bad = 0;
    u32 bi, ii, oi, k;

    for (bi = 0; bi < f->nblocks; bi++) {
        const A64Block *block = &f->blocks[bi];

        for (ii = 0; ii < block->n; ii++) {
            const A64Inst *inst = &block->insts[ii];

#define A64_BAD(...)                                                           \
    do {                                                                       \
        verify_diag(dc, f, bi, ii, __VA_ARGS__);                               \
        bad++;                                                                 \
    } while (0)
            if (inst->op >= A64_OP_COUNT)
                A64_BAD("invalid opcode %u", inst->op);
            if (inst->sf != A64_SF32 && inst->sf != A64_SF64 &&
                inst->sf != A64_SF128)
                A64_BAD("invalid sf %u", inst->sf);
            if (op_has_mixed_widths(inst->op) && inst->src_sf != A64_SF32 &&
                inst->src_sf != A64_SF64)
                A64_BAD("invalid source sf %u", inst->src_sf);
            if (inst->nops > 4) {
                A64_BAD("instruction has %u operands (maximum is 4)",
                        inst->nops);
                continue;
            }
            for (oi = 0; oi < inst->nops; oi++)
                if (verify_operand(f, &inst->ops[oi]))
                    A64_BAD("invalid operand %u", oi);
            for (; oi < 4; oi++)
                if (inst->ops[oi].kind != A64O_NONE)
                    A64_BAD("non-empty trailing operand %u", oi);
            if (inst->op == A64_OP_CALL) {
                if (!inst->call) {
                    A64_BAD("call is missing ABI-neutral call metadata");
                } else {
                    const A64CallInfo *call = inst->call;

                    if (inst->nops)
                        A64_BAD(
                            "call metadata cannot mix with inline operands");
                    if (call->callee_ref_kind > FUNCREF_INDIRECT)
                        A64_BAD("call has invalid callee-reference kind");
                    else if (call->callee_ref_kind == FUNCREF_INTERNAL &&
                             f->m && call->callee_id >= f->m->nfuncs)
                        A64_BAD("call has invalid internal callee");
                    else if (call->callee_ref_kind == FUNCREF_EXTERNAL &&
                             f->m && call->callee_id >= f->m->nsyms)
                        A64_BAD("call has invalid external callee");
                    else if (call->callee_ref_kind == FUNCREF_INDIRECT &&
                             !reg_valid(f, call->indirect))
                        A64_BAD("call has invalid indirect callee");
                    if (call->result.id && !reg_valid(f, call->result))
                        A64_BAD("call has invalid result register");
                    if (call->result.id && call->result.physical &&
                        call->result.id == (u32)A64_SP + 1)
                        A64_BAD("call result cannot be SP");
                    for (k = 0; k < call->nargs; k++)
                        if (!reg_valid(f, call->args[k].value)) {
                            A64_BAD("call has invalid argument %u", k);
                        } else if (call->args[k].value.physical &&
                                   call->args[k].value.id == (u32)A64_SP + 1) {
                            A64_BAD("call argument %u cannot be SP", k);
                        }
                    if (call->callee_ref_kind == FUNCREF_INDIRECT &&
                        call->indirect.physical &&
                        call->indirect.id == (u32)A64_SP + 1)
                        A64_BAD("indirect call target cannot be SP");
                }
            } else if (inst->call) {
                A64_BAD("non-call instruction carries call metadata");
            }
            if (invalid_reg31_position(f, inst))
                A64_BAD("SP/XZR identity is illegal in this operand position");
            if (inst->flags & A64IF_USES_NZCV) {
                if (inst->flags_src >= ii ||
                    !(block->insts[inst->flags_src].flags & A64IF_DEFS_NZCV)) {
                    A64_BAD("NZCV consumer does not name an earlier producer");
                } else {
                    for (k = inst->flags_src + 1; k < ii; k++)
                        if (block->insts[k].flags & A64IF_DEFS_NZCV)
                            A64_BAD("NZCV clobbered after recorded producer");
                }
            }
#undef A64_BAD
        }
    }
    return bad;
}
