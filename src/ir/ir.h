#ifndef CGF_IR_H
#define CGF_IR_H

#include <stdio.h>

#include "diag.h"
#include "util/arena.h"
#include "util/base.h"
#include "util/buf.h"

/* The SSA IR everything downstream stands on.
 *
 * TWO LAWS, both load-bearing:
 *
 * LAW 1 — AGGREGATES ARE NOT SSA VALUES. Structs, unions and arrays live
 * in memory (alloca or global), are moved only by the memcpy intrinsic,
 * and are named only by `ptr` values. Only the scalar IrTypes below flow
 * through registers. This single decision kills a whole family of bugs
 * before they can exist — partial-aggregate folds, first-class-struct ABI
 * confusion, padding loss in copies. It is enforced STRUCTURALLY: there
 * is no aggregate IrType to smuggle, so no instruction can produce or
 * consume one.
 *
 * LAW 2 — BLOCK PARAMETERS, NOT PHIS. Branches PASS ARGUMENTS to their
 * target blocks; each block declares typed parameters. Construction never
 * needs phi back-patching, the critical-edge and lost-copy problems
 * become plain argument passing, and the verifier's job is a per-edge
 * arity/type match instead of a phi-coherence proof. (The design the
 * sister project proved; we inherit it deliberately.)
 *
 * Also deliberate:
 * - No i1: comparisons produce i32 0/1 (C's semantics), and condbr /
 *   select test i32 nonzero.
 * - Constants are OPERANDS, not instructions: blocks stay free of
 *   materialization noise and backends decide immediate-vs-load.
 * - Float constants are Sprint 15 softfloat BIT PATTERNS. A host double
 *   never appears in the IR — the same determinism law as constexpr.
 * - `alloca` is legal in ANY block, not just entry. VLAs (Sprint 20)
 *   require dynamic alloca at the point of declaration; mem2reg (Sprint
 *   30) simply only promotes statically-sized entry-block allocas. Do
 *   not "fix" this to LLVM's entry-only convention.
 * - No typed GEP: `ptradd` is pointer + byte offset, and lowering makes
 *   every address computation explicit byte arithmetic. Backends fold
 *   into addressing modes; the IR stays honest.
 *
 * UNDEF: reading an uninitialized local yields the `undef` operand, with
 * LLVM-style PER-READ freedom — each use may independently observe any
 * value of the type. Consequences folds must respect: `%x == %x` on
 * undef need NOT fold to 1 (the two reads may differ); duplicating a use
 * can diverge. Fold `op(undef, k)` only where the result is independent
 * of the undef bits: `and undef, 0` -> 0, but `xor undef, undef` ->
 * undef, never 0. */

typedef struct {
    u32 v; /* 0 = invalid */
} ValueId;

typedef struct {
    u32 v; /* 0 = invalid; block N has id N+1 */
} BlockId;

#define VALUE_INVALID ((ValueId){0})
#define BLOCK_INVALID ((BlockId){0})

typedef enum IrType {
    IRT_I8,
    IRT_I16,
    IRT_I32,
    IRT_I64,
    IRT_F32,
    IRT_F64,
    IRT_F80,
    IRT_F128,
    IRT_PTR,
    IRT_VOID /* call results only; never a value type anywhere else */
} IrType;

/* ~40 live opcodes plus a RESERVED band. The reserved ones exist so their
 * opcode space is stable from day one; the builder hard-errors on each,
 * naming the sprint that lands it (19: ABI/varargs; 20: VLA stack ops and
 * atomics). */
typedef enum IrOp {
    /* integer arithmetic */
    IR_IADD,
    IR_ISUB,
    IR_IMUL,
    IR_SDIV,
    IR_UDIV,
    IR_SREM,
    IR_UREM,
    /* bitwise */
    IR_AND,
    IR_OR,
    IR_XOR,
    IR_SHL,
    IR_LSHR,
    IR_ASHR,
    /* comparisons */
    IR_ICMP,
    IR_FCMP,
    /* float arithmetic (no frem: C fmod is a libcall and stays one) */
    IR_FADD,
    IR_FSUB,
    IR_FMUL,
    IR_FDIV,
    IR_FNEG,
    /* conversions */
    IR_SEXT,
    IR_ZEXT,
    IR_TRUNC,
    IR_FPEXT,
    IR_FPTRUNC,
    IR_FPTOSI,
    IR_FPTOUI,
    IR_SITOFP,
    IR_UITOFP,
    IR_BITCAST, /* same-width reinterpret: int<->float, and i64<->ptr
                   (Sprint 18: C's pointer<->integer casts need it; all
                   five targets have 64-bit pointers, so i64 is THE
                   integer side — narrower casts go through trunc/ext) */
    /* memory */
    IR_ALLOCA,
    IR_LOAD,
    IR_STORE,
    IR_PTRADD,
    IR_MEMCPY,
    IR_MEMSET,
    /* calls and misc */
    IR_CALL,
    IR_SELECT,
    /* terminators */
    IR_RET,
    IR_BR,
    IR_CONDBR,
    IR_SWITCH,
    IR_UNREACHABLE,
    /* --- reserved: builder hard-errors naming the landing sprint --- */
    IR_STACKSAVE,    /* Sprint 20: VLA scope exit */
    IR_STACKRESTORE, /* Sprint 20 */
    IR_VA_START,     /* Sprint 19: varargs */
    IR_VA_ARG,       /* Sprint 19 */
    IR_VA_END,       /* Sprint 19 */
    IR_VA_COPY,      /* Sprint 19 */
    IR_ATOMICRMW,    /* Sprint 20 */
    IR_CMPXCHG,      /* Sprint 20 */
    IR_OP_COUNT
} IrOp;

typedef enum IrIcmp {
    ICMP_EQ,
    ICMP_NE,
    ICMP_SLT,
    ICMP_SLE,
    ICMP_SGT,
    ICMP_SGE,
    ICMP_ULT,
    ICMP_ULE,
    ICMP_UGT,
    ICMP_UGE
} IrIcmp;

/* The NaN story lives here: o* predicates are FALSE if either operand is
 * NaN, u* are TRUE. C's `==` lowers to oeq and `!=` to une — NaN != NaN
 * is true, which is the corner everyone forgets. */
typedef enum IrFcmp {
    FCMP_OEQ,
    FCMP_ONE,
    FCMP_OLT,
    FCMP_OLE,
    FCMP_OGT,
    FCMP_OGE,
    FCMP_ORD,
    FCMP_UEQ,
    FCMP_UNE,
    FCMP_ULT,
    FCMP_ULE,
    FCMP_UGT,
    FCMP_UGE,
    FCMP_UNO
} IrFcmp;

typedef enum IrOpKind {
    IROP_NONE,
    IROP_VALUE,
    IROP_ICONST,
    IROP_FCONST, /* Sprint 15 softfloat bits, exact, target format */
    IROP_SYMBOL, /* module symbol + addend */
    IROP_UNDEF
} IrOpKind;

typedef struct IrOperand {
    u8 kind; /* IrOpKind */
    u8 type; /* IrType */
    u32 sym; /* IROP_SYMBOL: module symbol index */
    u64 a;   /* VALUE: id | ICONST: bits | FCONST: bits lo | SYMBOL: addend */
    u64 b;   /* FCONST: bits hi (f80/f128); else 0 */
} IrOperand;

/* One outgoing CFG edge: a target block plus the arguments the branch
 * passes to its parameters. condbr has two of these, switch has 1+N. */
typedef struct IrEdge {
    BlockId target;
    IrOperand *args;
    u32 nargs;
    i64 case_val; /* IR_SWITCH non-default edges: the case constant */
} IrEdge;

/* Memory-op flags. */
#define IRF_VOLATILE 0x1u
#define IRF_SEQ_CST 0x2u /* the only atomic ordering in v0.1.0 */

/* Callee encoding for IR_CALL (the sister project's shape). */
typedef enum IrFuncRefKind {
    FUNCREF_INTERNAL, /* callee = IrFunc index in this module */
    FUNCREF_EXTERNAL, /* callee = module symbol index */
    FUNCREF_INDIRECT  /* ops[0] is the function pointer */
} IrFuncRefKind;

typedef struct IrInst {
    u8 op;    /* IrOp */
    u8 type;  /* result IrType (IRT_VOID when none) */
    u8 subop; /* IrIcmp / IrFcmp / IrFuncRefKind */
    u8 flags; /* IRF_* */
    u32 align;
    ValueId result; /* 0 when the op defines nothing */
    u32 nops;
    u32 nedges;
    u32 callee; /* IR_CALL: func index or symbol index, per subop */
    IrOperand *ops;
    IrEdge *edges;
    struct IrInst *next;
} IrInst;

/* 48 bytes is the budget (DoD): one cache line holds one instruction and
 * a third of the next. The static assert keeps growth deliberate. */
_Static_assert(sizeof(IrInst) <= 48, "IrInst grew past its 48-byte budget");

typedef struct IrBlock {
    const char *name; /* interned; parser/builder-assigned */
    ValueId *params;
    u32 nparams;
    u32 ninsts;
    IrInst *first;
    IrInst *last;
} IrBlock;

/* Per-value bookkeeping: the type, and where it was defined — which is
 * exactly what the dominance check needs. */
typedef enum { VDEF_NONE, VDEF_FPARAM, VDEF_BPARAM, VDEF_INST } IrValDef;

typedef struct IrValInfo {
    u8 type;     /* IrType */
    u8 def_kind; /* IrValDef */
    BlockId def_block;
    u32 def_pos; /* instruction index within the block, 0 for params */
} IrValInfo;

typedef struct IrFunc {
    const char *name; /* interned */
    u8 ret;           /* IrType */
    u8 *param_types;
    u32 nparams;
    ValueId *param_vals; /* function params are the entry block's defs */
    IrBlock *blocks;     /* block N has BlockId N+1 */
    u32 nblocks;
    u32 cap_blocks;
    IrValInfo *vals; /* value N at vals[N-1] */
    u32 nvals;
    u32 cap_vals;
} IrFunc;

typedef enum IrLinkage {
    IRLINK_INTERNAL,
    IRLINK_EXTERNAL,
    IRLINK_COMMON /* Sprint 16's -fcommon tentatives */
} IrLinkage;

typedef struct IrReloc {
    u64 offset;
    u32 symbol;
    i64 addend;
} IrReloc;

typedef struct IrGlobal {
    const char *name; /* interned */
    u64 size;
    u32 align;
    u8 linkage; /* IrLinkage */
    bool is_tentative;
    u8 *init; /* Sprint 15 byte image; NULL = zeroinit / BSS */
    IrReloc *relocs;
    u32 nrelocs;
} IrGlobal;

/* The module symbol table: every name the IR can reference — globals,
 * external functions, string-literal objects. Insertion-ordered (the
 * determinism law); IROP_SYMBOL/IrReloc/FUNCREF_EXTERNAL index into it. */
typedef struct IrModule {
    Arena *arena;
    DiagCtx *dc;
    IrFunc *funcs;
    u32 nfuncs;
    u32 cap_funcs;
    IrGlobal *globals;
    u32 nglobals;
    u32 cap_globals;
    const char **syms;
    u32 nsyms;
    u32 cap_syms;
} IrModule;

/* --- construction (src/ir/ir.c, src/ir/build.c) -------------------------- */

IrModule *ir_module_new(Arena *arena, DiagCtx *dc);
u32 ir_sym(IrModule *m, const char *name); /* interned name -> index */
IrGlobal *ir_global_new(IrModule *m, const char *name);
IrFunc *ir_func_new(IrModule *m, const char *name, IrType ret,
                    const IrType *params, u32 nparams);
BlockId ir_block_new(IrModule *m, IrFunc *f, const char *name);
ValueId ir_block_param(IrModule *m, IrFunc *f, BlockId b, IrType t);
IrBlock *ir_block(IrFunc *f, BlockId b);
IrType ir_value_type(const IrFunc *f, ValueId v);

/* Operand constructors. */
IrOperand ir_op_value(const IrFunc *f, ValueId v);
IrOperand ir_op_iconst(IrType t, i64 v);
IrOperand ir_op_fconst(IrType t, u64 lo, u64 hi);
IrOperand ir_op_symbol(IrType t, u32 sym, i64 addend);
IrOperand ir_op_undef(IrType t);

/* The builder appends to a block; terminators seal it. Reserved opcodes
 * hard-error naming their sprint — see ir_build_reserved. */
typedef struct IrBuilder {
    IrModule *m;
    IrFunc *f;
    BlockId block;
} IrBuilder;

void ir_builder_at(IrBuilder *b, IrModule *m, IrFunc *f, BlockId blk);
ValueId ir_build2(IrBuilder *b, IrOp op, IrType t, IrOperand x, IrOperand y);
ValueId ir_build1(IrBuilder *b, IrOp op, IrType t, IrOperand x);
ValueId ir_build_icmp(IrBuilder *b, IrIcmp p, IrOperand x, IrOperand y);
ValueId ir_build_fcmp(IrBuilder *b, IrFcmp p, IrOperand x, IrOperand y);
ValueId ir_build_alloca(IrBuilder *b, IrOperand size, u32 align);
ValueId ir_build_load(IrBuilder *b, IrType t, IrOperand ptr, u32 align,
                      u8 flags);
void ir_build_store(IrBuilder *b, IrOperand val, IrOperand ptr, u32 align,
                    u8 flags);
ValueId ir_build_ptradd(IrBuilder *b, IrOperand ptr, IrOperand off);
void ir_build_memcpy(IrBuilder *b, IrOperand dst, IrOperand src, IrOperand size,
                     u32 align, u8 flags);
void ir_build_memset(IrBuilder *b, IrOperand dst, IrOperand byte,
                     IrOperand size, u32 align, u8 flags);
ValueId ir_build_select(IrBuilder *b, IrOperand c, IrOperand x, IrOperand y);
ValueId ir_build_call(IrBuilder *b, IrType ret, IrFuncRefKind kind, u32 callee,
                      const IrOperand *args, u32 nargs);
ValueId ir_build_call_indirect(IrBuilder *b, IrType ret, IrOperand fp,
                               const IrOperand *args, u32 nargs);
void ir_build_ret(IrBuilder *b, const IrOperand *val); /* NULL = ret void */
void ir_build_br(IrBuilder *b, BlockId target, const IrOperand *args,
                 u32 nargs);
void ir_build_condbr(IrBuilder *b, IrOperand c, BlockId t,
                     const IrOperand *targs, u32 ntargs, BlockId e,
                     const IrOperand *eargs, u32 neargs);
void ir_build_switch(IrBuilder *b, IrOperand x, BlockId defblk,
                     const i64 *case_vals, const BlockId *case_blks, u32 n);
void ir_build_unreachable(IrBuilder *b);
/* Every reserved opcode routes here and ICEs naming its sprint. */
void ir_build_reserved(IrBuilder *b, IrOp op);

/* --- text (src/ir/print.c, src/ir/parse.c) ------------------------------- */

/* Deterministic: values renumber %0.. in definition order per function,
 * blocks print in layout order, globals in declaration order. No
 * pointers, no hashes — two prints of one module are byte-identical. */
void ir_print_module(FILE *out, const IrModule *m);
void ir_print_module_buf(Buf *out, const IrModule *m);
const char *ir_type_name(IrType t);
const char *ir_op_name(IrOp op);
const char *ir_icmp_name(IrIcmp p);
const char *ir_fcmp_name(IrFcmp p);
IrModule *ir_parse_module(Arena *arena, DiagCtx *dc, const char *src,
                          const char *path);

/* Structural equality — the round-trip invariant's judge. Compares
 * everything except interned pointer identities (names by content). */
bool ir_module_struct_eq(const IrModule *a, const IrModule *b);

/* Deletes blocks unreachable from the entry, compacting the block array
 * and remapping every edge target. The verifier REJECTS orphans (check
 * 6), and C legally puts statements after `return` — so producers run
 * this instead of abandoning dead blocks. Values defined in deleted
 * blocks stay in the vals table (ids must remain stable) but lose their
 * def_block; lowering never lets an SSA value cross from dead code into
 * live code (locals travel through allocas), so no live use can see one. */
void ir_func_remove_unreachable(IrFunc *f);

/* Renumbers every value into DOCUMENT order (fparams, then per block in
 * layout order: params, then instruction results) and rebuilds the vals
 * table. Producers whose creation order differs from layout order — any
 * lowering that fills a join block after a later-created block — run
 * this so parse(print(M)) == M holds; parser-built modules are already
 * canonical. Uses of values with no surviving definition renumber to 0,
 * which verifier check 1 rejects loudly. */
void ir_func_renumber(Arena *arena, IrFunc *f);

/* --- dominance (src/ir/dom.c) -------------------------------------------- */

typedef struct IrDomTree IrDomTree;
/* Cooper-Harvey-Kennedy over a reverse-postorder numbering. Unreachable
 * blocks get no entry (the verifier rejects them anyway). */
IrDomTree *ir_domtree_build(Arena *arena, const IrFunc *f);
BlockId ir_idom(const IrDomTree *t, BlockId b); /* BLOCK_INVALID for entry */
bool ir_dominates(const IrDomTree *t, BlockId a, BlockId b);

/* --- verifier (src/ir/verify.c) ------------------------------------------ */

/* The ten checks, numbered in verify.c; each has a fixture proving it
 * fires. Reports through the DiagCtx and returns false on any failure —
 * the CALLER decides severity: hand-written IR gets exit 1, internally
 * generated IR becomes an ICE (and CGF_DUMP_BAD_IR=path dumps the module
 * first). */
bool ir_verify(DiagCtx *dc, const IrModule *m);

#endif
