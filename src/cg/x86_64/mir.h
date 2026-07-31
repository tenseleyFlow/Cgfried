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
                      X64_REG_COUNT,
                      X64_REG_NONE = 255
} X64Reg;

typedef enum X64Width { X64_B = 1, X64_W = 2, X64_L = 4, X64_Q = 8 } X64Width;

/* Condition codes, one per icmp predicate (signed/unsigned split). */
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
    X64_CC_AE
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
    X64_OP_COUNT
} X64Op;

#define X64IF_TWO_ADDR 0x1 /* dst must equal src1 after Sprint 22 */
#define X64IF_DEFS_FLAGS 0x2
#define X64IF_USES_FLAGS 0x4

typedef struct X64Mem {  /* [base + index*scale + disp32] or sym(%rip) */
    X64VReg base, index; /* either may be invalid */
    u8 scale;            /* 1,2,4,8 */
    i32 disp;            /* must fit signed 32 always */
    u32 rip_sym;         /* module symbol index + 1; 0 = none.
                            EXCLUSIVE with base/index: 64-bit absolute
                            addresses never fold — RIP-relative always */
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

typedef struct X64Inst {
    u16 op;
    u8 width;     /* X64Width of the OPERATION */
    u8 src_width; /* MOVZX/MOVSX: the narrower source width */
    u8 flags;     /* X64IF_* */
    u8 cc;        /* X64Cc for JCC/SETCC */
    u8 def_fixed; /* X64Reg + 1 pre-color on the def; 0 = none */
    X64VReg def;  /* 0 = defines nothing */
    X64Operand a, b;
    X64VReg xuse;        /* implicit extra use (idiv/div read rdx:rax; rdx has
                            no operand slot). 0 = none. Liveness includes it. */
    u8 xuse_fixed;       /* X64Reg + 1 pre-color on xuse; 0 = none */
    u32 target, target2; /* MIR block ids (1-based) for jmp/jcc */
    u32 flags_src;       /* USES_FLAGS: producer's index in this block */
    u32 table;           /* JMPTBL: table index; ALLOCA static lea marker
                            + ALLOCA_DYN: the alloca's align */
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

typedef struct X64Func {
    const char *name;
    bool allocated;  /* post-RA: X64VReg.v is X64Reg + 1 */
    u32 frame_size;  /* finalized rbp-relative bytes (spills + locals) */
    u32 spill_slots; /* count, for the printer's accounting line */
    Arena *arena;
    X64Block *blocks; /* [0] mirrors IR block 1; splits appended */
    u32 nblocks, cap_blocks;
    u32 nvregs; /* vreg ids are 1..nvregs */
    X64Table *tables;
    u32 ntables, cap_tables;
    const IrModule *m; /* symbol names for printing */
} X64Func;

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

#endif
