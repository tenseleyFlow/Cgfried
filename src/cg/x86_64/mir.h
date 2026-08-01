#ifndef CGF_X64_MIR_H
#define CGF_X64_MIR_H

#include "ir/ir.h"

/* The x86_64 machine IR (Sprint 21): three-address over VIRTUAL registers
 * until Sprint 22 allocates and does the two-address fixup. Width is
 * chosen AT ISEL and never inferred later — the stale-upper-bits trap
 * (a 32-bit op implicitly zeroes 63:32; a trunc is a rename that zeroes
 * nothing) is only survivable when every instruction states its width.
 *
 * EFLAGS ARE RECORDED, NOT REMEMBERED: nearly every ALU op sets
 * X64IF_DEFS_FLAGS (mov/lea/movzx/movsx do not; inc-style partial-flags
 * ops are never emitted). Consumers carry X64IF_USES_FLAGS plus the
 * INDEX of their producer (flags_src) — the MIR verifier rejects any
 * flags-defining instruction scheduled between the two, which is the
 * bit Sprint 22's scheduler and Sprint 53's peepholes must consult. */

typedef struct {
    u32 v; /* 0 = invalid; virtual until Sprint 22 assigns physical */
} X64VReg;

typedef enum X64Reg { /* physical, encoding order; NONE for "no color" */
                      X64_RAX,
                      X64_RCX,
                      X64_RDX,
                      X64_RBX,
                      X64_RSP,
                      X64_RBP,
                      X64_RSI,
                      X64_RDI,
                      X64_R8,
                      X64_R9,
                      X64_R10,
                      X64_R11,
                      X64_R12,
                      X64_R13,
                      X64_R14,
                      X64_R15,
                      X64_GP_COUNT, /* = 16; GP ids stop here */
                      X64_XMM0 = 16,
                      X64_XMM1,
                      X64_XMM2,
                      X64_XMM3,
                      X64_XMM4,
                      X64_XMM5,
                      X64_XMM6,
                      X64_XMM7,
                      X64_XMM8,
                      X64_XMM9,
                      X64_XMM10,
                      X64_XMM11,
                      X64_XMM12,
                      X64_XMM13,
                      X64_XMM14,
                      X64_XMM15,
                      X64_REG_COUNT, /* = 32 */
                      X64_REG_NONE = 255
} X64Reg;

/* Register classes (Sprint 23). Every vreg has one, recorded at mint
 * time in X64Func.vclass — the allocator keeps separate pools. NO xmm
 * register is callee-saved (the SysV surprise): FP values living across
 * calls always spill. */
typedef enum X64RegClass { X64RC_GP, X64RC_XMM } X64RegClass;

/* X64_T (10) is the f80 memory width for the x87 ops; f80 values NEVER
 * occupy registers — load-op-store only, so the width appears solely on
 * fld/fstp-style memory traffic. */
typedef enum X64Width {
    X64_B = 1,
    X64_W = 2,
    X64_L = 4,
    X64_Q = 8,
    X64_T = 10,
    X64_X = 16
} X64Width;

/* Condition codes, one per icmp predicate (signed/unsigned split), plus
 * P/NP (Sprint 23): PF is the ucomi UNORDERED flag — the FP compare
 * recipes are built on it, since unordered sets ZF=PF=CF=1 and a naive
 * je on == is wrong on NaN. */
typedef enum X64Cc {
    X64_CC_E,
    X64_CC_NE,
    X64_CC_L,
    X64_CC_LE,
    X64_CC_G,
    X64_CC_GE,
    X64_CC_B,
    X64_CC_BE,
    X64_CC_A,
    X64_CC_AE,
    X64_CC_P,
    X64_CC_NP
} X64Cc;

typedef enum X64Op {
    X64_OP_MOV,    /* def <- reg/imm32/mem */
    X64_OP_MOVABS, /* def <- imm64 (the ONLY 64-bit-imm form) */
    X64_OP_MOVZX,  /* def <- zero-extended narrower reg/mem */
    X64_OP_MOVSX,  /* def <- sign-extended narrower reg/mem */
    X64_OP_LEA,    /* def <- address of mem (no flags, three-operand) */
    X64_OP_ADD,
    X64_OP_SUB,
    X64_OP_AND,
    X64_OP_OR,
    X64_OP_XOR,
    X64_OP_IMUL,
    X64_OP_NEG,
    X64_OP_NOT,
    X64_OP_SHL, /* variable count = CL fixed-reg on operand b */
    X64_OP_SHR,
    X64_OP_SAR,
    X64_OP_CMP,
    X64_OP_TEST,
    X64_OP_SETCC, /* writes 8 BITS; always paired with movzx by isel */
    X64_OP_CQO,   /* rax -> rdx:rax (or cdq for .l) */
    X64_OP_IDIV,  /* fixed: uses rdx:rax + operand; defs rax(quot) rdx(rem) */
    X64_OP_DIV,
    X64_OP_LOAD,   /* def <- [mem] */
    X64_OP_STORE,  /* [mem] <- reg/imm32 */
    X64_OP_JMP,    /* target */
    X64_OP_JCC,    /* cc + target/target2(fallthrough) */
    X64_OP_JMPTBL, /* indirect jump through a rodata label table */
    X64_OP_RET,
    X64_OP_UD2,  /* IR unreachable */
    X64_OP_PUSH, /* Sprint 22 frame code (phys regs only) */
    X64_OP_POP,
    /* Pre-RA markers regalloc expands and frame-finalize consumes; the
     * post-RA verifier rejects any that survive. */
    X64_OP_ALLOCA_DYN,   /* def <- rsp after `sub rsp, round16(a)` */
    X64_OP_STACKSAVE,    /* def <- rsp */
    X64_OP_STACKRESTORE, /* rsp <- a */
    /* --- Sprint 23: SSE2 scalar FP (widths: L = ss, Q = sd) ------------- */
    X64_OP_FMOV,   /* def <- xmm reg (movss/movsd reg-reg) */
    X64_OP_FLOAD,  /* def <- [mem] (movss/movsd load) */
    X64_OP_FSTORE, /* [mem] <- xmm reg */
    X64_OP_FADD,   /* adds{s,d}: TWO_ADDR, does NOT touch EFLAGS */
    X64_OP_FSUB,
    X64_OP_FMUL,
    X64_OP_FDIV,
    X64_OP_UCOMI,  /* ucomis{s,d}: DEFS_FLAGS; unordered => ZF=PF=CF=1 */
    X64_OP_CVTF2I, /* cvtts{s,d}2si — the TRUNCATING form. width = int
                      width (l/q), src_width = float width. The rounding
                      form (no 't') is a silent-wrong-answer bug. */
    X64_OP_CVTI2F, /* cvtsi2s{s,d}: width = float width, src_width = int */
    X64_OP_CVTF2F, /* cvtss2sd / cvtsd2ss: width = dst, src_width = src */
    X64_OP_FXORM,  /* xorp{s,d} with a cpool mask (b): negate via sign bit */
    X64_OP_FANDM,  /* andp{s,d} with a cpool mask (b): fabs */
    X64_OP_MOVQXR, /* def(xmm) <- gp reg bits (movq xmm, r64) */
    X64_OP_MOVQRX, /* def(gp) <- xmm bits (movq r64, xmm) */
    /* --- Sprint 36: SSE2 packed vectors (width X = 128 bits). -------- */
    X64_OP_VMOV,
    X64_OP_VLOAD,
    X64_OP_VSTORE,
    X64_OP_VADD,
    X64_OP_VSUB,
    X64_OP_VMUL,
    X64_OP_VDIV,
    X64_OP_VAND,
    X64_OP_VOR,
    X64_OP_VXOR,
    X64_OP_VSHUF32,   /* b.imm = pshufd control */
    X64_OP_VSHUFLO16, /* b.imm = pshuflw control */
    X64_OP_VUNPCKLBW,
    X64_OP_VUNPCKLWD,
    X64_OP_VUNPCKLQ,
    X64_OP_VSRLDQ, /* b.imm = byte count */
    /* --- Sprint 23: calls ------------------------------------------------ */
    X64_OP_CALL,    /* a = MEM(rip_sym) direct | VREG indirect. Arg regs
                       ride in xuses (pre-colored, so their intervals
                       reach the call); clobbers = caller-saved GP + ALL
                       xmm (regalloc consults call points). def optional
                       (rax/xmm0-fixed result). */
    X64_OP_READREG, /* def <- current content of the def_fixed register.
                       Function entry (incoming args) and post-call
                       second results / sret echo. Rewrite turns it into
                       a mov (or nothing when def landed there). */
    X64_OP_ARGLD,   /* def <- incoming stack arg at [rbp+16+b.imm] */
    X64_OP_ARGLEA,  /* def <- ADDRESS rbp+16+b.imm (byval/f80 params) */
    X64_OP_VASTART, /* a = va_list ptr: frame-finalize stores the
                       overflow_arg_area and reg_save_area pointers */
    /* --- Sprint 23: x87, the ONLY sanctioned x87 (f80). Values live in
     * MEMORY; every sequence is locally balanced load-op-store — the
     * verifier tracks stack depth and rejects any imbalance at block
     * ends, calls, and branches. --------------------------------------- */
    X64_OP_X87_FLD,   /* push [mem]; width T/Q/L = fldt/fldl/flds */
    X64_OP_X87_FSTP,  /* pop into [mem]; width T/Q/L */
    X64_OP_X87_FILD,  /* push (integer load); width Q/L */
    X64_OP_X87_FISTP, /* pop into [mem] as integer (RC-dance around it) */
    X64_OP_X87_FADDP, /* st1 <- st1 op st0, pop */
    X64_OP_X87_FSUBP,
    X64_OP_X87_FSUBRP,
    X64_OP_X87_FMULP,
    X64_OP_X87_FDIVP,
    X64_OP_X87_FDIVRP,
    X64_OP_X87_FCHS, /* st0 = -st0 */
    X64_OP_X87_FABS,
    X64_OP_X87_FUCOMIP, /* EFLAGS <- st0 vs st1, pop (DEFS_FLAGS) */
    X64_OP_X87_FPOP,    /* fstp %st(0): discard st0 */
    X64_OP_X87_FNSTCW,  /* [mem] <- control word (16 bits) */
    X64_OP_X87_FLDCW,   /* control word <- [mem] */
    X64_OP_COUNT
} X64Op;

#define X64IF_TWO_ADDR 0x1 /* dst must equal src1 after Sprint 22 */
#define X64IF_DEFS_FLAGS 0x2
#define X64IF_USES_FLAGS 0x4

typedef struct X64Mem {  /* [base + index*scale + disp32] or sym(%rip) */
    X64VReg base, index; /* either may be invalid */
    u8 scale;            /* 1,2,4,8 */
    u8 rsp_rel;          /* Sprint 23: outgoing-arg slot [rsp + disp].
                            Pre-RA rsp has no spelling; the rewrite
                            substitutes the real base. Survives dynamic
                            allocas by construction (args always sit at
                            the CURRENT stack bottom). */
    i32 disp;            /* must fit signed 32 always */
    u32 rip_sym;         /* module symbol index + 1; 0 = none.
                            EXCLUSIVE with base/index: 64-bit absolute
                            addresses never fold — RIP-relative always */
    u32 cpool;           /* Sprint 23: X64Func.consts index + 1 (rodata
                            constants: FP literals, sign/abs masks).
                            Exclusive with base/index/rip_sym. */
} X64Mem;

typedef enum X64OperandKind {
    X64O_NONE,
    X64O_VREG,
    X64O_IMM, /* fits simm32 unless the op is MOVABS */
    X64O_MEM
} X64OperandKind;

typedef struct X64Operand {
    u8 kind;
    u8 fixed; /* X64Reg + 1 pre-color constraint; 0 = none (CL, RAX...) */
    X64VReg r;
    i64 imm;
    X64Mem mem;
} X64Operand;

/* One implicit register use: idiv's rdx, a call's argument registers,
 * ret's return register. The vreg's interval extends to the instruction
 * (that is the whole point), and the fixed color pre-colors it. */
typedef struct X64XUse {
    X64VReg r;
    u8 fixed; /* X64Reg + 1; 0 = unconstrained (rare) */
} X64XUse;

typedef struct X64Inst {
    u16 op;
    u8 width;     /* X64Width of the OPERATION */
    u8 src_width; /* MOVZX/MOVSX/CVT*: the source width */
    u8 flags;     /* X64IF_* */
    u8 cc;        /* X64Cc for JCC/SETCC */
    u8 def_fixed; /* X64Reg + 1 pre-color on the def; 0 = none */
    X64VReg def;  /* 0 = defines nothing */
    X64Operand a, b;
    X64XUse *xuses; /* implicit extra uses (arena; see X64XUse) */
    u32 nxuses;
    u32 target, target2; /* MIR block ids (1-based) for jmp/jcc */
    u32 flags_src;       /* USES_FLAGS: producer's index in this block */
    u32 table;           /* JMPTBL: table index; ALLOCA static lea marker
                            + ALLOCA_DYN: the alloca's align */
    u32 loc;             /* IrModule debug-location id; 0 = no attribution */
    u32 debug_label;     /* prepared post-RA: .Lloc_<func>_<id>, 0 = none */
} X64Inst;

typedef struct X64Block {
    const char *name;
    X64Inst *insts;
    u32 n, cap;
} X64Block;

typedef struct X64Table { /* one switch jump table, emitted to .rodata */
    u32 *targets;         /* MIR block ids, dense from min case */
    u32 n;
} X64Table;

/* One .rodata constant (FP literal or sign/abs mask), 16 bytes max.
 * Deduped by bit pattern at isel; Sprint 24 emits the pool. */
typedef struct X64Const {
    u64 lo, hi;
    u8 size;  /* 4, 8, 10, or 16 bytes */
    u8 align; /* 4, 8, or 16 */
} X64Const;

typedef struct X64Func {
    const char *name;
    bool allocated;   /* post-RA: X64VReg.v is X64Reg + 1 */
    bool variadic;    /* Sprint 23: frame gets the 176-byte reg save area */
    bool ret_f80;     /* f80 return: st0 is legitimately loaded at ret */
    bool debug_lines; /* emit prepared line labels for this function */
    u32 frame_size;   /* finalized rbp-relative bytes (spills + locals) */
    u32 spill_slots;  /* count, for the printer's accounting line */
    u32 out_args;     /* max outgoing-arg bytes over all call sites */
    u32 named_stack_bytes; /* incoming stack bytes used by NAMED params
                              (va_start's overflow_arg_area starts after) */
    Arena *arena;
    X64Block *blocks; /* [0] mirrors IR block 1; splits appended */
    u32 nblocks, cap_blocks;
    u32 nvregs; /* vreg ids are 1..nvregs */
    u8 *vclass; /* [nvregs+1] X64RegClass per vreg (Sprint 23) */
    u8 *vwidth; /* [nvregs+1] full value width; vector XMM values are 16 */
    u32 cap_vclass;
    X64Table *tables;
    u32 ntables, cap_tables;
    X64Const *consts; /* rodata pool, insertion-ordered, deduped */
    u32 nconsts, cap_consts;
    const IrModule *m; /* symbol names for printing */
} X64Func;

/* Mint a vreg of the given class (grows vclass). regalloc's repair
 * copies inherit the class of the vreg they localize. */
X64VReg x64_newv(X64Func *f, X64RegClass rc);
X64VReg x64_newv_width(X64Func *f, X64RegClass rc, X64Width width);
u8 x64_vclass(const X64Func *f, u32 v);
u8 x64_vwidth(const X64Func *f, u32 v);
/* Intern a constant into the pool (dedup by bits+size); returns index+1
 * for X64Mem.cpool. */
u32 x64_cpool_intern(X64Func *f, u64 lo, u64 hi, u8 size, u8 align);
/* Append one implicit use to an instruction (arena re-alloc). */
void x64_add_xuse(X64Func *f, X64Inst *in, X64VReg r, u8 fixed);

/* --- the shared backend interface (src/cg/cg.h re-exports these) --------- */

X64Func *x64_isel_function(const IrModule *m, const IrFunc *f, Arena *a);
void x64_mir_print(const X64Func *f, Buf *out);
/* Returns the number of violations reported (0 = clean). */
int x64_mir_verify(const X64Func *f, DiagCtx *dc);

/* The sign-extended-imm32 gate: 64-bit ALU immediates sign-extend, so
 * 0x80000000 does NOT fit (it would ADD a negative). Every imm use is
 * gated on this; failures materialize via movl (free zext) or movabs. */
bool x64_imm_fits_simm32(i64 v);

/* Addressing-fold legality (the folder consults this; unit-tabled). */
bool x64_fold_ok(u8 scale, bool index_is_rsp, i64 disp);
const char *x64_cc_name(u8 cc);
const char *x64_reg_name(u8 reg);

/* --- Sprint 22: allocation -------------------------------------------------
 */

/* ONE allocator at every opt level — the golden invariant. The enum
 * exists so the invariant is testable: x64_regalloc_entry returns the
 * SAME function pointer for every level, asserted by unit test. */
typedef enum CgOptLevel { CG_O0, CG_O1, CG_O2 } CgOptLevel;

typedef void (*X64RegallocFn)(X64Func *f);
X64RegallocFn x64_regalloc_entry(CgOptLevel level);

/* Liveness + linear scan + spill + two-address fixup + frame layout.
 * After this: f->allocated, every operand physical, prologue/epilogue
 * in place, allocas rbp-relative. CGF_SPILL_ALL=1 forces every interval
 * to spill (the -O0-quality baseline through the SAME code path — the
 * most effective allocator-correctness weapon we have). */
void x64_regalloc(X64Func *f);

/* Exposed pieces, unit-driven directly (the two-address hazard table and
 * the alignment law are exhaustively tabled in tests/unit). */

/* Block-level live-in/out bitsets to fixpoint. words = ceil((nvregs+1)/64);
 * in/out are [nblocks * words], caller-zeroed. */
u32 x64_liveness_words(const X64Func *f);
void x64_liveness(const X64Func *f, u64 *live_in, u64 *live_out);

/* Two-address fixup on ALLOCATED MIR: every X64IF_TWO_ADDR inst becomes
 * the x86 form def == a, with the dst==src2 hazard resolved (swap when
 * commutative, reserved-scratch rescue when not). */
void x64_twoaddr_fixup(X64Func *f);

/* The alignment LAW: rsp must be 0 mod 16 at every call. Entry is 8 mod
 * 16; push rbp restores 0; each callee-saved push flips by 8. Returns
 * the `sub rsp, N` with N >= raw_bytes making the running total 0 again:
 * N = 8 * (pushes_after_rbp mod 2) (mod 16). */
u32 x64_frame_align_pad(u32 pushes_after_rbp, u32 raw_bytes);

/* --- Sprint 24: AT&T emission ----------------------------------------------
 */

/* Post-RA MIR -> gas-assemblable text. fidx namespaces the .Lf<i>_<n>
 * local labels (determinism across runs); linkage picks .globl/.local. */
void x64_emit_function(const X64Func *f, const IrModule *m, u32 fidx,
                       u8 linkage, Buf *out);
/* Globals: Sprint 19 byte images + reloc lists, emitted numerically. */
void x64_emit_globals(const IrModule *m, Buf *out);

#endif
