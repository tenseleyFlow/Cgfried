#ifndef CGF_A64_MIR_H
#define CGF_A64_MIR_H

#include "ir/ir.h"

/* AArch64 owns its instruction constraints, register file, selection, and
 * allocation.  Target-independent liveness/linear-scan machinery belongs in
 * cg/shared.c; this MIR deliberately exposes plain blocks and operands to the
 * CgMirView adapter rather than duplicating that machinery here. */

typedef enum A64PhysReg {
    A64_X0,
    A64_X1,
    A64_X2,
    A64_X3,
    A64_X4,
    A64_X5,
    A64_X6,
    A64_X7,
    A64_X8,
    A64_X9,
    A64_X10,
    A64_X11,
    A64_X12,
    A64_X13,
    A64_X14,
    A64_X15,
    A64_X16,
    A64_X17,
    A64_X18,
    A64_X19,
    A64_X20,
    A64_X21,
    A64_X22,
    A64_X23,
    A64_X24,
    A64_X25,
    A64_X26,
    A64_X27,
    A64_X28,
    A64_X29,
    A64_X30,
    A64_SP,  /* encodes 31 where the position names the stack pointer */
    A64_XZR, /* also encodes 31, but is never interchangeable with SP */
    A64_V0,
    A64_V1,
    A64_V2,
    A64_V3,
    A64_V4,
    A64_V5,
    A64_V6,
    A64_V7,
    A64_V8,
    A64_V9,
    A64_V10,
    A64_V11,
    A64_V12,
    A64_V13,
    A64_V14,
    A64_V15,
    A64_V16,
    A64_V17,
    A64_V18,
    A64_V19,
    A64_V20,
    A64_V21,
    A64_V22,
    A64_V23,
    A64_V24,
    A64_V25,
    A64_V26,
    A64_V27,
    A64_V28,
    A64_V29,
    A64_V30,
    A64_V31,
    A64_NZCV,
    A64_REG_COUNT,
    A64_REG_NONE = 255
} A64PhysReg;

typedef enum A64RegClass { A64RC_GP, A64RC_FP, A64RC_NZCV } A64RegClass;

/* id 0 is invalid.  A virtual id is 1..nvregs; a physical id is an
 * A64PhysReg + 1.  Keeping the physical tag explicit prevents v31 from ever
 * being mistaken for architectural encoding 31. */
typedef struct A64Reg {
    u32 id;
    u8 physical;
} A64Reg;

/* Operation width. SF128 names a whole q register and appears only on the
 * NEON forms; the ELEMENT arrangement (4s, 8h, ...) is per-instruction and
 * rides in src_sf as the vector IrType, because `add v0.4s` and `add v0.8h`
 * are different instructions over the same registers. */
typedef enum A64Sf { A64_SF32, A64_SF64, A64_SF128 } A64Sf;

/* Values match the architectural four-bit condition encoding. */
typedef enum A64Cond {
    A64_CC_EQ = 0,
    A64_CC_NE = 1,
    A64_CC_HS = 2,
    A64_CC_LO = 3,
    A64_CC_MI = 4,
    A64_CC_PL = 5,
    A64_CC_VS = 6,
    A64_CC_VC = 7,
    A64_CC_HI = 8,
    A64_CC_LS = 9,
    A64_CC_GE = 10,
    A64_CC_LT = 11,
    A64_CC_GT = 12,
    A64_CC_LE = 13,
    A64_CC_AL = 14,
    A64_CC_NV = 15
} A64Cond;

typedef enum A64FpPred {
    A64_FP_OEQ,
    A64_FP_ONE,
    A64_FP_OLT,
    A64_FP_OLE,
    A64_FP_OGT,
    A64_FP_OGE,
    A64_FP_UEQ,
    A64_FP_UNE,
    A64_FP_ULT,
    A64_FP_ULE,
    A64_FP_UGT,
    A64_FP_UGE,
    A64_FP_ORD,
    A64_FP_UNO,
    A64_FP_PRED_COUNT
} A64FpPred;

/* Some FP predicates need two conditional tests.  `first || second` when
 * combine_or is set, otherwise `first && second`; A64_CC_AL means absent. */
typedef struct A64FpCondMap {
    u8 first;
    u8 second;
    bool combine_or;
} A64FpCondMap;

extern const A64FpCondMap a64_fp_cond_map[A64_FP_PRED_COUNT];

typedef enum A64Op {
    A64_OP_MOV,
    A64_OP_MOVZ,
    A64_OP_MOVN,
    A64_OP_MOVK,
    A64_OP_ADD,
    A64_OP_ADDS,
    A64_OP_SUB,
    A64_OP_SUBS,
    A64_OP_AND,
    A64_OP_ANDS,
    A64_OP_ORR,
    A64_OP_EOR,
    A64_OP_LSL,
    A64_OP_LSR,
    A64_OP_ASR,
    A64_OP_MUL,
    A64_OP_MADD,
    A64_OP_MSUB,
    A64_OP_MNEG,
    A64_OP_SDIV,
    A64_OP_UDIV,
    A64_OP_SMULL,
    A64_OP_UMULL,
    A64_OP_SMULH,
    A64_OP_UMULH,
    A64_OP_CSEL,
    A64_OP_CSINC,
    A64_OP_CSINV,
    A64_OP_CSNEG,
    A64_OP_CSET,
    A64_OP_CSETM,
    A64_OP_ADDR,
    A64_OP_LOAD,
    A64_OP_STORE,
    A64_OP_LDP,
    A64_OP_STP,
    A64_OP_B,
    A64_OP_BCOND,
    A64_OP_CBZ,
    A64_OP_CBNZ,
    A64_OP_TBZ,
    A64_OP_TBNZ,
    A64_OP_CALL,
    A64_OP_RET,
    A64_OP_BR,
    A64_OP_UNREACHABLE,
    A64_OP_FMOV,
    A64_OP_FADD,
    A64_OP_FSUB,
    A64_OP_FMUL,
    A64_OP_FDIV,
    A64_OP_FSQRT,
    A64_OP_FNEG,
    A64_OP_FABS,
    A64_OP_FCMP,
    A64_OP_FCSEL,
    A64_OP_SCVTF,
    A64_OP_UCVTF,
    A64_OP_FCVTZS,
    A64_OP_FCVTZU,
    A64_OP_FCVT,
    A64_OP_ALLOCA,
    A64_OP_ALLOCA_DYN,
    A64_OP_STACKSAVE,
    A64_OP_STACKRESTORE,
    A64_OP_VASTART,
    /* Two pseudo-instructions, each expanded at EMISSION into a complete
     * ll/sc loop. They stay single instructions through allocation on
     * purpose: a spill or reload landing between the ldaxr and the stlxr
     * would clear the exclusive monitor, and the loop would spin forever
     * rather than fail visibly.
     *
     * UPGRADE(armv8.1-lse): with LSE these collapse to single instructions
     * (ldadd/swp/cas) and the pseudo-ops can go away entirely. */
    A64_OP_ATOMIC_LLSC, /* dst, [addr], val, #rmw-op */
    A64_OP_ATOMIC_CAS,  /* dst(old), [addr], expected, new */
    /* NEON (Sprint 49). Arrangement comes from src_fs; unlike SSE2 there is
     * no aligned/unaligned split to model — ldr/str q never fault on
     * alignment — so the x86 movaps/movups machinery has no counterpart
     * here and is deliberately not ported. */
    A64_OP_VADD,
    A64_OP_VSUB,
    A64_OP_VMUL,
    A64_OP_VAND,
    A64_OP_VORR,
    A64_OP_VEOR,
    A64_OP_VFADD,
    A64_OP_VFSUB,
    A64_OP_VFMUL,
    A64_OP_VFDIV,
    A64_OP_VDUP,     /* dup vd.<T>, wn   -- splat from a general register */
    A64_OP_VDUPLANE, /* dup vd.<T>, vn.<T>[0] -- splat from lane zero */
    A64_OP_VEXT,     /* ext vd.16b, vn.16b, vm.16b, #imm -- byte rotate */
    A64_OP_VUMOV,    /* umov wd, vn.<T>[i] -- lane to general register */
    A64_OP_VLANE,    /* mov sd, vn.<T>[i]  -- lane to scalar FP register */
    /* Fused multiply-add: ONE rounding step, not two. Only emitted when the
     * language policy permits contraction (IrFunc.fp_contract). */
    A64_OP_FMADD,
    A64_OP_FMSUB,
    A64_OP_COUNT
} A64Op;

#define A64IF_DEFS_NZCV 0x1u
#define A64IF_USES_NZCV 0x2u
#define A64IF_VOLATILE 0x4u
#define A64IF_ATOMIC 0x8u

typedef enum A64AddrMode {
    A64_ADDR_SCALED,
    A64_ADDR_UNSCALED,
    A64_ADDR_PRE,
    A64_ADDR_POST,
    A64_ADDR_REG_LSL,
    A64_ADDR_REG_UXTW,
    A64_ADDR_REG_SXTW,
    A64_ADDR_MATERIALIZE,
    /* Frame-pointer-relative, in the INCOMING argument area: the offset is
     * measured from the first incoming stack argument, and frame
     * finalization biases it once the frame size is known. Selection cannot
     * compute the real displacement because it does not yet know how big the
     * frame is. */
    A64_ADDR_INCOMING
} A64AddrMode;

typedef struct A64Mem {
    A64Reg base;
    A64Reg index;
    i64 offset;
    u8 mode;
    u8 size;
    u8 shift;
} A64Mem;

typedef enum A64OperandKind {
    A64O_NONE,
    A64O_REG,
    A64O_IMM,
    A64O_MEM,
    A64O_LABEL,
    A64O_SYM
} A64OperandKind;

typedef struct A64Operand {
    u8 kind;
    A64Reg reg;
    i64 imm;
    A64Mem mem;
    u32 id; /* label block id or module symbol index + 1 */
} A64Operand;

/* Calls remain ABI-neutral until Sprint 48: selection records every logical
 * argument and its exact IR ABI annotation, but assigns no x/v register or
 * stack slot. This side record avoids imposing the four-inline-operand limit
 * on calls while keeping ordinary A64Inst compact and easy to rewrite. */
typedef struct A64CallArg {
    A64Reg value;
    u8 type;       /* IrType */
    u64 abi_annot; /* exact IrOperand.b: byval/pair classification + size */
} A64CallArg;

typedef struct A64CallInfo {
    u8 callee_ref_kind; /* IrFuncRefKind */
    u32 callee_id;      /* raw IrInst.callee: func or symbol index */
    A64Reg indirect;    /* FUNCREF_INDIRECT only */
    A64Reg result;      /* invalid for void */
    u8 result_type;     /* IrType */
    u8 abi_ret;         /* IrAbiRet */
    bool variadic;
    bool noreturn;
    A64CallArg *args;
    u32 nargs;
} A64CallInfo;

typedef struct A64Inst {
    u16 op;
    u8 sf;     /* destination/operation width */
    u8 src_sf; /* mixed-width conversions: independent source width */
    u8 flags;
    u8 cond;
    u8 nops;
    A64Operand ops[4];
    A64CallInfo *call; /* A64_OP_CALL only; arena-owned */
    u32 flags_src;     /* producer index in this block for USES_NZCV */
    u32 loc;
} A64Inst;

typedef struct A64Block {
    const char *name;
    A64Inst *insts;
    u32 n, cap;
} A64Block;

typedef struct A64Func {
    const char *name;
    bool allocated;
    Arena *arena;
    A64Block *blocks;
    u32 nblocks, cap_blocks;
    u32 nvregs;
    u8 *vclass;
    u8 *vwidth;
    /* Pre-colouring: a physical register id + 1 that this vreg MUST take,
     * or zero. Call marshalling is the only producer. */
    u8 *vfixed;
    u32 cap_vclass;
    u32 spill_bytes; /* post-allocation: bytes of spill/alloca area */
    u32 frame_bytes; /* post-allocation: whole frame, a multiple of 16 */
    u32 out_args;    /* bytes of outgoing stack arguments, 16-byte rounded */
    bool variadic;   /* needs the AAPCS64 register save area */
    u32 va_named_gp; /* general registers the named parameters consumed */
    u32 va_named_fp; /* vector registers the named parameters consumed */
    const IrModule *m;
} A64Func;

typedef enum A64MovKind {
    A64_MOV_ORR,
    A64_MOV_MOVZ,
    A64_MOV_MOVN,
    A64_MOV_MOVK
} A64MovKind;

typedef struct A64MovSynth {
    u8 kind;
    u16 imm16;
    u8 shift;
    u32 logical; /* packed N:immr:imms for A64_MOV_ORR */
} A64MovSynth;

typedef struct A64AddSubImm {
    bool is_sub;
    u16 imm12;
    u8 shift; /* 0 or 12 */
} A64AddSubImm;

A64Reg a64_newv(A64Func *f, A64RegClass rc);
A64Reg a64_newv_width(A64Func *f, A64RegClass rc, A64Sf sf);
A64Reg a64_newv_fixed(A64Func *f, A64RegClass rc, A64Sf sf, u8 phys);
A64Reg a64_phys(A64PhysReg reg);
u8 a64_vclass(const A64Func *f, A64Reg reg);
u8 a64_vwidth(const A64Func *f, A64Reg reg);
u8 a64_phys_encode(A64PhysReg reg);
void a64_block_append(A64Func *f, A64Block *b, A64Inst inst);
A64CallInfo *a64_call_info_new(A64Func *f, A64Inst *inst, u8 callee_ref_kind,
                               u32 callee_id, A64Reg indirect, A64Reg result,
                               u8 result_type, u8 abi_ret, bool variadic,
                               bool noreturn);
void a64_call_add_arg(A64Func *f, A64CallInfo *call, A64Reg value, u8 type,
                      u64 abi_annot);

bool a64_logical_imm_encode(u64 value, unsigned width, u32 *n_immr_imms);
u32 a64_synth_mov_width(u64 value, unsigned width, A64MovSynth out[4]);
u32 a64_synth_mov(u64 value, A64MovSynth out[4]);
bool a64_addsub_imm(i64 value, A64AddSubImm *out);
bool a64_fp_imm_encode(u64 bits, unsigned width, u8 *imm8);
A64AddrMode a64_isel_addr(i64 offset, u8 size, bool pre, bool post);
A64AddrMode a64_addr_reg_mode(bool index32, bool index_signed, bool scaled);

const char *a64_op_name(u16 op);
const char *a64_cond_name(u8 cond);
const char *a64_phys_name(u8 reg, u8 sf);
const char *a64_vec_name(u8 reg);
const char *a64_vec_arrangement(u8 ir_vector_type);
u8 a64_vec_lane_sf(u8 ir_vector_type);
void a64_mir_print(const A64Func *f, Buf *out);
int a64_mir_verify(const A64Func *f, DiagCtx *dc);
A64Func *a64_isel_function(const IrModule *m, const IrFunc *f, Arena *a);
void a64_regalloc(A64Func *f);
void a64_emit_function(const A64Func *f, const IrModule *m, u32 fidx,
                       u8 linkage, Buf *out);
void a64_emit_globals(const IrModule *m, Buf *out);
u32 a64_liveness_words(const A64Func *f);
void a64_liveness(const A64Func *f, u64 *live_in, u64 *live_out);
bool a64_reg_is_callee_saved_gp(u8 reg);
bool a64_reg_preserved_across_call(u8 reg, bool wide128);
u32 a64_frame_total(u32 csr_bytes, u32 local_bytes, u32 out_args);
bool a64_peep_pair_mem(A64Func *f);
bool a64_relax_branches(A64Func *f);
bool a64_branch_delta_fits(u16 op, i64 delta);

#endif
