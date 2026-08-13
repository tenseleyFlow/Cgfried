#ifndef CGF_IR_H
#define CGF_IR_H

#include <stdio.h>

#include "attr.h"
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
    IRT_V16I8,
    IRT_V8I16,
    IRT_V4I32,
    IRT_V2I64,
    IRT_V4F32,
    IRT_V2F64,
    IRT_VOID /* call results only; never a value type anywhere else */
} IrType;

/* C effective-type classes carried by memory instructions.  Signed and
 * unsigned integer types deliberately share a width class; character and
 * void-byte accesses use ETYPE_CHAR, the strict-aliasing wildcard.  The
 * value lives in IrInst.subop for ALLOCA/LOAD/STORE/MEMCPY/MEMSET, so this
 * provenance costs no instruction bytes. */
typedef enum EffTypeId {
    ETYPE_UNKNOWN,
    ETYPE_CHAR,
    ETYPE_I8,
    ETYPE_I16,
    ETYPE_I32,
    ETYPE_I64,
    ETYPE_F32,
    ETYPE_F64,
    ETYPE_F80,
    ETYPE_F128,
    ETYPE_PTR,
    ETYPE_AGGREGATE,
    ETYPE_UNION,
    ETYPE_COUNT
} EffTypeId;

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
    /* 128-bit vectors (Sprint 36). Arithmetic reuses the scalar opcodes. */
    IR_VSPLAT,
    IR_VEXTRACT,
    IR_VREDUCE_ADD,
    IR_VREDUCE_MUL,
    IR_VREDUCE_AND,
    IR_VREDUCE_OR,
    IR_VREDUCE_XOR,
    /* varargs (Sprint 19). ONE live opcode: `va_start ptr` fills the two
     * POINTER fields of a va_list (overflow_arg_area, reg_save_area) —
     * only codegen knows those addresses (Sprint 23 emits the register
     * save area and the AL-driven xmm skip). The gp_offset/fp_offset
     * fields are compile-time constants and lowering stores them as
     * plain i32 stores BEFORE this op. va_arg is never an opcode: it
     * expands to an ordinary branch diamond at lowering (per-target by
     * design — Sprint 50's Apple divergence rewrites the expansion, not
     * an instruction). va_end emits nothing; va_copy is a memcpy. */
    IR_VA_START,
    /* VLA stack discipline (Sprint 20): `%tok = stacksave` captures the
     * stack pointer; `stackrestore %tok` rewinds it. Lowering emits one
     * save per VLA-bearing scope (lazily, at the first VLA) and restores
     * the OUTERMOST live token on every scope-exit edge; `return` emits
     * none (the epilogue's frame teardown subsumes it) and longjmp needs
     * none (frames die wholesale, tokens with them). */
    IR_STACKSAVE,
    IR_STACKRESTORE,
    /* seq_cst atomics (Sprint 20; v0.1.0 accepts every memory_order and
     * honors it as seq_cst — stronger is conformant). atomicrmw's subop
     * is IrAtomicRmw; cmpxchg returns the OLD memory value and SUCCESS
     * is `icmp eq old, expected` — the classic single-result expansion,
     * chosen over a two-result instruction the IR cannot express. */
    IR_ATOMICRMW,
    IR_CMPXCHG,
    /* Inline assembly (Sprint 55). The instruction carries an index into
     * IrModule.asms rather than the template text, because the text and the
     * per-operand constraints are ONE record that every consumer needs
     * whole: the printer, the parser, the register allocator and the
     * emitter all read the same IrAsm.
     *
     * IR_ASM never defines a value. C asm can have SEVERAL outputs and an
     * IR instruction defines at most one, so outputs travel as ADDRESS
     * operands: the backend allocates a register per the constraint, runs
     * the template, then stores that register through the address. The
     * constraint is still honoured -- `=r` really is in a register while
     * the template runs -- and the model needs no tuple type. The cost is
     * that an asm output variable is address-taken and so never promoted
     * out of memory; ASM-001 records it. */
    IR_ASM,
    /* terminators */
    IR_RET,
    IR_BR,
    IR_CONDBR,
    IR_SWITCH,
    IR_UNREACHABLE,
    /* --- reserved: never instructions. va_arg expands at lowering,
     * va_end is nothing, va_copy is a memcpy (Sprint 19); the slots stay
     * so the opcode space and this comment keep the story. Sprint 20
     * took the stack and atomic ops live above. --- */
    IR_VA_ARG,
    IR_VA_END,
    IR_VA_COPY,
    IR_OP_COUNT
} IrOp;

/* atomicrmw's operation, in IrInst.subop. */
typedef enum IrAtomicRmw {
    RMW_ADD,
    RMW_SUB,
    RMW_AND,
    RMW_OR,
    RMW_XOR,
    RMW_XCHG
} IrAtomicRmw;

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

/* Call-argument provenance, carried in IrOperand.argflags.
 *
 * ANON: this operand fills an ANONYMOUS parameter — it lands past the
 * callee's last named one. IRF_CALL_VARIADIC says the callee is variadic; it
 * does not say where the named parameters stop, and only the front end ever
 * knew. Apple's arm64 ABI needs the boundary because it passes every
 * anonymous argument on the STACK while a named one of the same type goes in
 * a register: the same call shape, two placements.
 *
 * It lives in its own byte rather than in the ABI-annotation word `b`
 * because `b` is NOT free on every operand kind — on an f80/f128 fconst it
 * is the high half of the value, and `printf("%Lf", 1.0L)` passes exactly
 * that. The byte is free padding, so nothing grows. Printed ` anon`. */
#define IROPF_ANON 0x1u
/* SEXT/ZEXT: the argument's C type is an integer NARROWER than 32 bits, and
 * this is its signedness. IR integer types are signless, so a backend that
 * must widen an argument cannot recover it. Apple's arm64 ABI makes the
 * CALLER responsible for that widening — the callee reads w0 raw — while
 * AAPCS64 and SysV leave the high bits unspecified and let the callee do it,
 * so these are inert everywhere else. Printed ` sext` / ` zext`. */
#define IROPF_SEXT 0x2u
#define IROPF_ZEXT 0x4u
/* ONSTACK modifies IR_ARG_BYVAL: the callee receives the POINTEE by value on
 * the stack, not the pointer. That is what byval already means on SysV, so
 * the flag is inert there; on AAPCS64 plain byval is INDIRECT (the address
 * rides a GPR) and this is the distinct case where the register bank ran out
 * mid-list and the whole aggregate had to be stacked instead.
 *
 * A flag rather than a new IR_ARG_* kind because that field is three bits and
 * all eight are spoken for. Printed ` onstack`. */
#define IROPF_ONSTACK 0x8u

typedef struct IrOperand {
    u8 kind;     /* IrOpKind */
    u8 type;     /* IrType */
    u8 argflags; /* IROPF_*; call arguments only, zero elsewhere */
    u32 sym;     /* IROP_SYMBOL: module symbol index */
    u64 a; /* VALUE: id | ICONST: bits | FCONST: bits lo | SYMBOL: addend */
    u64 b; /* FCONST: bits hi (f80/f128); else 0 */
} IrOperand;

/* Re-attach a call argument's ABI provenance after its operand was REPLACED
 * by an optimization. The provenance belongs to the argument SLOT, not to
 * the value that happens to fill it, so folding `printf("%d", n)` to a
 * constant must not forget the argument was anonymous.
 *
 * SIX passes rewrite call operands in place (sccp, simplify, gvn, cse,
 * mem2reg, inline) and each had its own copy of this; three of them carried
 * `b` and silently dropped `argflags` the day it was added, and the inliner
 * was still hand-rolling its own version a sprint later. One function, so
 * the next field cannot be missed the same way.
 *
 * An annotation belongs to the USE SITE, not to the value, so the fresh
 * operand takes the old one's outright rather than keeping its own. That
 * matters when the replacement already carries one: substituting a caller's
 * argument operand into an inlined callee body would otherwise leak the
 * OUTER call's annotation onto an ordinary callee use.
 *
 * The single exception is `b` on an fconst, where those bits are the high
 * half of an f80/f128 VALUE rather than an annotation -- writing to it
 * silently rewrites the constant. Only the inliner's private copy had that
 * guard; folding the argument of `f(1.0L)` through any of the other five
 * would have corrupted it. argflags always moves: being a byte of its own
 * is exactly why it works for every operand kind. */
void ir_arg_carry_provenance(IrOperand *fresh, const IrOperand *old);

/* argflags occupies padding that already existed between `type` and `sym`.
 * If this ever fails, the field stopped being free and the trade needs
 * re-deciding rather than silently costing 8 bytes per operand. */
_Static_assert(sizeof(IrOperand) == 24, "IrOperand grew past 24 bytes");

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
/* IR_CALL: the callee's C type is variadic. Codegen owes such a call the
 * AL protocol (mov $n_xmm_args, %eax before the call) — a fact only the
 * front end knows, so the call instruction carries it. Printed ` va`
 * after the argument list (Sprint 23). */
#define IRF_CALL_VARIADIC 0x4u
/* Integer add/sub/mul: the operation came from signed C arithmetic whose
 * overflow is undefined.  The IR remains signless; this provenance is the
 * optimizer's explicit license to use no-signed-wrap reasoning. */
#define IRF_NSW 0x8u
/* IR_ICMP: comparison is a compiler-inserted bounds-safety predicate.
 * Provenance never substitutes for proof: range/BCE clients may fold it only
 * when the comparison is established constant.  Sprint 44 is the producer. */
#define IRF_BOUNDS_CHECK 0x10u
/* IR_CALL: source-level knowledge that control never returns from this
 * callee. Flow analysis treats the call as a path terminator; codegen emits
 * an ordinary call. */
#define IRF_NORETURN 0x20u
/* IR_LOAD: this exact source read occurs in the initializer of the same
 * local. It is diagnostic provenance, not an optimizer license. */
#define IRF_SELF_INIT 0x40u
/* Flow-only source provenance, interpreted by terminator opcode:
 *   CONDBR/SWITCH — the source condition depended on sizeof/_Alignof or a
 *                   macro spelling (configuration idiom);
 *   BR            — a defensive switch-arm break;
 *   RET           — the return was synthesized for source-level falloff.
 * The bit affects diagnostics only and is preserved by textual IR so the
 * parser/printer structural round-trip stays exact. */
#define IRF_FLOW_PROVENANCE 0x80u

/* Callee encoding for IR_CALL (the sister project's shape). */
typedef enum IrFuncRefKind {
    FUNCREF_INTERNAL, /* callee = IrFunc index in this module */
    FUNCREF_EXTERNAL, /* callee = module symbol index */
    FUNCREF_INDIRECT  /* ops[0] is the function pointer */
} IrFuncRefKind;

typedef struct IrInst {
    u8 op;          /* IrOp */
    u8 type;        /* result IrType (IRT_VOID when none) */
    u8 subop;       /* IrIcmp / IrFcmp / IrFuncRefKind */
    u8 flags;       /* IRF_* */
    ValueId result; /* 0 when the op defines nothing */
    u32 loc; /* 1-based index into IrModule.locs; 0 = no source location */
    /* Memory ops use align; IR_CALL uses callee. No opcode uses both. */
    union {
        u32 align;
        u32 callee; /* func index or symbol index, per subop */
    };
    u32 nops;
    u32 nedges;
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

struct OptMem2RegInfo;

/* Source provenance for blocks discarded before Sprint 40's flow pass.
 * Lowering records orphaned statements after a terminator; simplify-cfg
 * records blocks removed after folding a source CFG edge.  BlockIds name the
 * pre-compaction CFG and are diagnostic provenance only. */
typedef struct IrCfgRemoved {
    BlockId block;
    Span loc;
    const char *block_name;
    u32 region; /* nonzero: one contiguous source-unreachable region */
    u8 flags;
} IrCfgRemoved;

#define IR_CFG_REMOVED_CONFIG 0x1u
#define IR_CFG_REMOVED_DEFENSIVE_BREAK 0x2u

/* Front-end provenance for local slots. The textual IR intentionally omits
 * this optional metadata, but flow diagnostics retain it while compiling C. */
typedef struct IrLocalSlot {
    ValueId addr;
    const char *name;
    Span decl_span;
} IrLocalSlot;

/* The ABI-return contract (Sprint 19; Sprint 23 implements the moves).
 * The IR stays single-result, so multi-register returns keep the
 * sret-shaped hidden pointer and this annotation carries the truth:
 *   NONE     — the IR return type says it all (scalars, true void).
 *   SRET     — MEMORY-class aggregate: callee stores through the hidden
 *              ptr param 0 AND echoes that pointer in rax (psABI §3.2.3
 *              — one store path plus a register echo callers may ignore).
 *   PAIR_xy  — two-eightbyte aggregate: the VALUE travels in registers
 *              (rax/rdx for INTEGER, xmm0/xmm1 for SSE, mixed in class
 *              order). The IR still writes through the hidden pointer;
 *              codegen loads the eightbytes from it before `ret` and the
 *              CALLER stores them back after `call`. The hidden pointer
 *              itself is NOT passed at runtime for PAIR — it is IR
 *              bookkeeping only, which is why the annotation matters. */
typedef enum IrAbiRet {
    IR_ABIRET_NONE,
    IR_ABIRET_SRET,
    IR_ABIRET_PAIR_II,
    IR_ABIRET_PAIR_IS,
    IR_ABIRET_PAIR_SI,
    IR_ABIRET_PAIR_SS,
    /* AAPCS64 only: 1-4 HOMOGENEOUS floating leaves come back in v0-v3 and
     * NO hidden pointer is passed. The IR keeps the sret SHAPE exactly as
     * the pairs do -- a hidden ptr parameter the callee builds into -- and
     * this value plus IrFunc.abi_ret_n carries the register truth.
     *
     * It must be distinguishable from IR_ABIRET_SRET, which is why the
     * classifier's original `ir_abi = SRET` placeholder could not stand:
     * sret passes a pointer in x8 and an HFA passes nothing at all. */
    IR_ABIRET_HFA_F32,
    IR_ABIRET_HFA_F64,
    IR_ABIRET_HFA_F128,
} IrAbiRet;

/* Per-call-argument ABI annotation, carried in IrOperand.b (VALUE and
 * SYMBOL operands leave b zero otherwise). Low 32 bits: the byte size;
 * bits 32..34: the kind. byval = MEMORY-class aggregate passed by
 * copying the POINTEE onto the stack (the ptr is IR-level only); sret /
 * pair_* mark the hidden return pointer per IrAbiRet. Printed as
 * `byval(N)` etc. after the argument. */
#define IR_ARG_NONE 0u
#define IR_ARG_BYVAL 1u
#define IR_ARG_SRET 2u
#define IR_ARG_PAIR_II 3u
#define IR_ARG_PAIR_IS 4u
#define IR_ARG_PAIR_SI 5u
#define IR_ARG_PAIR_SS 6u
/* AAPCS64 HFA return: same sret SHAPE (a hidden pointer the callee builds
 * into) but NO pointer is passed at runtime and the value comes back in
 * v0-v3. The leaf COUNT rides bits 35..37; the leaf size is size/count,
 * because an HFA is homogeneous by definition and has no interior padding.
 * Distinguishing it from IR_ARG_SRET matters at the CALL site, where sret
 * really does spend x8 on a pointer and an HFA spends nothing. */
#define IR_ARG_HFA 7u
#define ir_arg_hfa_n(b) ((u32)(((b) >> 35) & 0x7u))
#define ir_arg_annot_hfa(size, n)                                              \
    (((u64)IR_ARG_HFA << 32) | ((u64)(n) << 35) | (u64)(u32)(size))
#define ir_arg_annot(kind, size) (((u64)(kind) << 32) | (u64)(u32)(size))
#define ir_arg_kind(b) ((u32)((b) >> 32) & 0x7u)
#define ir_arg_size(b) ((u32)(b))
/* High-bit parameter-only C provenance.  It composes with the low ABI
 * annotation fields and is compared by structural equality as part of the
 * full annotation word. */
#define IR_PARAM_RESTRICT (1ull << 63)
#define ir_param_is_restrict(b) (((b) & IR_PARAM_RESTRICT) != 0)
/* The parameter-side spelling of IROPF_ONSTACK: this byval parameter arrived
 * by value on the stack rather than as an address in a register. A parameter
 * annotation is a bare u64 with no argflags byte beside it, so the flag has
 * to ride the word itself. */
#define IR_PARAM_ONSTACK (1ull << 62)
#define ir_param_is_onstack(b) (((b) & IR_PARAM_ONSTACK) != 0)

typedef struct IrFunc {
    struct IrModule *module; /* owning module; enables source-log helpers */
    const char *name;        /* interned */
    u32 loc;      /* function definition location; 0 when unavailable */
    u8 ret;       /* IrType */
    u8 abi_ret;   /* IrAbiRet */
    u8 abi_ret_n; /* HFA only: leaf count, 1-4 (printed inside abi(...)) */
    /* `aligned(N)` on a FUNCTION: the code's alignment, printed ` align(N)`.
     * 0 means "the backend's default", which is not the same as 1 -- every
     * emitter already pads function entries and must keep doing so. */
    u32 align;
    /* `used`: nothing here references it, keep it anyway. Consumed by IPO's
     * reachability, which is the only thing that deletes a whole function. */
    bool is_used;
    /* `section("name")` on a FUNCTION: which output section its code lands in.
     * NULL means the backend's default. */
    const char *section;
    /* `constructor` / `destructor`: run around `main`. Independent, because
     * one function may be both. The priorities are only meaningful when the
     * matching flag is set, and CGF_INIT_PRIORITY_DEFAULT is a real priority
     * rather than a sentinel -- gcc emits the same unnumbered section for the
     * bare form and for an explicit 65535.
     *
     * Like `is_used`, these make the function an IPO ROOT: nothing in the
     * module references a constructor, and the entry the backend emits is a
     * relocation the callgraph cannot see. */
    bool is_ctor;
    bool is_dtor;
    u16 ctor_prio;
    u16 dtor_prio;
    bool is_weak;  /* the `weak` attribute, on a FUNCTION */
    u8 visibility; /* GnuVisibility */
    u8 linkage;    /* IrLinkage (Sprint 24: .globl vs .local emission);
                      defaults EXTERNAL, printed ` internal` otherwise */
    bool variadic; /* printed as ', ...' after the last parameter */
    /* An old-style definition or `f()` declaration has no prototype: its
     * body still has concrete incoming parameters, but calls may legally
     * pass a different count and default-promoted types. */
    bool unprototyped;
    /* The blunt setjmp policy (Sprint 20): a function that CALLS
     * setjmp/sigsetjmp/_setjmp compiles with every local memory-pinned
     * (mem2reg skips the whole function) and every call as a full
     * optimization barrier — Sprint 30's pass manager consults this one
     * predicate. longjmp needs nothing: the danger lives entirely in the
     * setjmp-calling frame, whose locals are already pinned. Printed as
     * a `setjmp` marker after the parameter list. */
    bool calls_setjmp;
    /* Whether the backend may fuse a multiply and an add into one
     * rounding step. It is a property of the MODULE's semantics, not a
     * codegen preference — contraction changes results — so it round-trips
     * as a `contract` marker rather than being re-derived from flags the
     * IR does not carry. */
    bool fp_contract;
    /* Validated source ownership contracts.  The immutable list is shared
     * with sema and remains live for the translation-unit arena lifetime. */
    const CgfAttr *cgf_attrs;
    u8 *param_types;
    /* Per-parameter ABI annotation, same encoding as IrOperand.b call
     * annotations (Sprint 23). Today only IR_ARG_BYVAL appears: a
     * MEMORY-class aggregate param whose IR ptr is the ADDRESS OF THE
     * INCOMING STACK COPY, consuming stack bytes and no register — a
     * bare ptr param would be indistinguishable from a pointer in the
     * GP queue. NULL = no annotations. Printed `byval(N)` after the
     * parameter type. */
    u64 *param_annots;
    u32 nparams;
    ValueId *param_vals; /* function params are the entry block's defs */
    IrBlock *blocks;     /* block N has BlockId N+1 */
    u32 nblocks;
    u32 cap_blocks;
    IrValInfo *vals; /* value N at vals[N-1] */
    u32 nvals;
    u32 cap_vals;
    IrLocalSlot *local_slots;
    u32 nlocal_slots;
    u32 cap_local_slots;
    /* Arena-owned analysis provenance retained for Sprint 40's flow
     * warnings. Opaque here so the IR does not depend on optimization. */
    struct OptMem2RegInfo *opt_mem2reg_info;
    /* Transient inliner work state.  The budget is initialized lazily and
     * follows the function through IPO compaction, so a scalar fixpoint cannot
     * replenish its code-growth allowance on every pass-manager iteration.
     * It is deliberately absent from textual IR and structural equality. */
    u32 opt_inline_growth_left;
    bool opt_inline_growth_initialized;
    IrCfgRemoved *cfg_removed;
    u32 ncfg_removed;
    u32 cap_cfg_removed;
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
    /* _Thread_local: one copy PER THREAD. It is a property of the OBJECT,
     * not of any reference to it, so it rides the global and every backend
     * asks the module rather than threading it through operands. Printed
     * ` tls`. */
    bool is_tls;
    /* GNU symbol attributes, same reasoning as is_tls: a property of the
     * OBJECT, so it rides the global and the backend asks the module.
     * Printed ` weak` and ` visibility(hidden)`. */
    bool is_weak;
    bool is_used; /* the `used` attribute, on an OBJECT */
    /* The OBJECT's own type is const-qualified, so its bytes belong in a
     * read-only section. Same reasoning as is_tls: a property of the object,
     * carried on the global rather than rediscovered per backend. Note this
     * is the object's OWN qualifier looked through array-element
     * qualification (6.7.3p9), never a const MEMBER of a non-const record --
     * gcc puts `struct { const int x; } s;` in .data. Printed ` const`. */
    bool is_const;
    /* `section("name")` on an OBJECT. A named section also forces real bytes:
     * an uninitialized object there is PROGBITS, not a .bss reservation, since
     * the section the author named is where the bytes must be. */
    const char *section;
    u8 visibility; /* GnuVisibility */
    u8 *init;      /* Sprint 15 byte image; NULL = zeroinit / BSS */
    IrReloc *relocs;
    u32 nrelocs;
} IrGlobal;

/* Front-end-only object-representation provenance for aggregate copies.
 * C aggregate assignment may copy padding, but memory diagnostics must ask
 * only whether bytes belonging to actual members were initialized.  The
 * textual IR deliberately omits this optional table; entries are keyed by
 * the instruction's interned source location and exact copy size so they
 * survive cloning, inlining and CFG rewrites without growing IrInst. */
typedef struct IrByteRange {
    u64 lo;
    u64 hi; /* half-open */
} IrByteRange;

typedef struct IrMemLayout {
    u32 loc;
    u32 nranges;
    u64 size;
    IrByteRange *ranges;
    bool suppress_uninit; /* union/complex layout: widen toward silence */
} IrMemLayout;

/* The module symbol table: every name the IR can reference — globals,
 * external functions, string-literal objects. Insertion-ordered (the
 * determinism law); IROP_SYMBOL/IrReloc/FUNCREF_EXTERNAL index into it. */
/* An `alias` symbol: a NAME for an object or function defined elsewhere in
 * this module. It is neither an IrGlobal (no size, no initializer, occupies no
 * storage of its own) nor an IrFunc (no body), so it gets its own list rather
 * than a flag that every consumer of those two would have to learn to skip.
 *
 * gcc REQUIRES the target to be defined in the same translation unit -- an
 * alias to an undefined symbol is an error, not a linker problem -- which is
 * what keeps this a purely local fact and lets the emitter write one `.set`. */
typedef struct IrAlias {
    const char *name;   /* interned */
    const char *target; /* interned; defined in THIS module */
    u8 linkage;         /* IrLinkage */
    bool is_weak;
    u8 visibility; /* GnuVisibility */
} IrAlias;

/* Attributes on a symbol that may have no definition in this module.  A
 * weak hidden extern still needs `.weak`/`.hidden` in the object even though
 * it has no IrGlobal or IrFunc record of its own (musl's nullable _DYNAMIC is
 * the canonical case).  This table is parallel to IrModule.syms. */
typedef struct IrSymAttrs {
    bool is_weak;
    u8 visibility; /* GnuVisibility */
} IrSymAttrs;

/* How a constraint letter resolved, decided in lowering because the letters
 * are TARGET vocabulary: `r` means the same everywhere, `d` is rdx on x86-64
 * and a d-register on arm64. The backend sees a class, never a letter. */
typedef enum {
    ASM_CLS_REG,   /* any allocatable GP register */
    ASM_CLS_FPREG, /* any allocatable FP/vector register (x / w) */
    ASM_CLS_X87,   /* x86 x87 stack top (`t`); never an allocatable vreg */
    ASM_CLS_X87UP, /* x86 x87 second stack slot (`u` / st(1)) */
    ASM_CLS_FIXED, /* one named physical register (a b c d S D, or a clobber) */
    ASM_CLS_MEM,   /* a memory operand */
    ASM_CLS_IMM    /* an assemble-time constant */
} IrAsmClass;

typedef struct IrAsmOp {
    const char *constraint; /* as spelled, for diagnostics and round-trip */
    const char *name;       /* [symbolic] name, or NULL */
    u8 cls;                 /* IrAsmClass */
    u8 reg;                 /* ASM_CLS_FIXED: the target's register number */
    bool is_output;
    /* Early clobber (`&`): this output may be written BEFORE the template
     * finishes reading its inputs, so it must not share a register with
     * any of them. Missing this is the classic silent wrong-answer
     * generator -- gcc itself returns 2 rather than 11 for the fixture in
     * tests/programs/gnu/ when the `&` is dropped. */
    bool early_clobber;
    /* Matching constraint (`"0"`): this input must land exactly where that
     * output did. `+` desugars to an output plus one of these BEFORE the
     * allocator runs, so two operands name one location and nothing
     * downstream needs a third concept. */
    i32 tied_to; /* operand index, or -1 */
    i64 imm;     /* ASM_CLS_IMM: the folded value */
    /* Byte size of the C operand, so the template's `%0` prints a register
     * of the right width -- `%eax` for an int, `%rax` for a long. Without it
     * every operand would print 64-bit and `movl %1, %0` would assemble
     * against the wrong register name. */
    u8 size;
} IrAsmOp;

typedef struct IrAsm {
    const char *tmpl;
    IrAsmOp *ops; /* outputs first, then inputs: the template's %0 order */
    u32 nops;
    u32 noutputs;
    bool is_basic;    /* no colon: `%` reaches the assembler verbatim */
    bool is_volatile; /* never deleted, even with unused outputs */
    bool clobbers_memory;
    bool clobbers_cc;
    u8 *clobber_regs; /* named register clobbers, target register numbers */
    u32 nclobber_regs;
} IrAsm;

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
    IrSymAttrs *sym_attrs;
    /* Ownership contracts for external function symbols, parallel to syms.
     * Non-functions and unannotated externals have NULL entries. */
    const CgfAttr **sym_cgf_attrs;
    u32 nsyms;
    u32 cap_syms;
    Span *locs; /* instruction source locations; ids are 1-based */
    u32 nlocs;
    u32 cap_locs;
    IrMemLayout *mem_layouts;
    u32 nmem_layouts;
    u32 cap_mem_layouts;
    IrAlias *aliases;
    /* Inline-asm records, referenced by IR_ASM instructions through a
     * 1-based index in the instruction's `callee` slot. */
    IrAsm *asms;
    u32 nasms;
    u32 cap_asms;
    /* File-scope basic asm, in source order, emitted verbatim between
     * functions the way gcc does. */
    const char **file_asms;
    u32 nfile_asms;
    u32 cap_file_asms;
    u32 naliases;
    u32 cap_aliases;
} IrModule;

/* --- construction (src/ir/ir.c, src/ir/build.c) -------------------------- */

IrModule *ir_module_new(Arena *arena, DiagCtx *dc);
/* Arena-owned exact clone used by analysis pipelines that must not perturb
 * the code-generation module. Optional optimizer metadata is reset, while
 * front-end source provenance is preserved. */
IrModule *ir_module_clone(Arena *arena, const IrModule *source);
u32 ir_sym(IrModule *m, const char *name); /* interned name -> index */
void ir_sym_set_attrs(IrModule *m, u32 index, bool is_weak, u8 visibility);
u32 ir_sym_exact_asm(IrModule *m, const char *name);
/* An asm label is already in assembler spelling. IR keeps it distinct from an
 * ordinary C symbol with the same bytes by an internal leading `!`; textual IR
 * writes that as `@!name`, and emitters strip it instead of applying target
 * mangling. */
bool ir_sym_name_is_exact_asm(const char *name);
const char *ir_sym_asm_spelling(const char *name);
IrGlobal *ir_global_new(IrModule *m, const char *name);
u32 ir_asm_new(IrModule *m, const IrAsm *a); /* returns the 1-based index */
void ir_module_add_file_asm(IrModule *m, const char *text);
IrAlias *ir_alias_new(IrModule *m, const char *name, const char *target);
IrAlias *ir_alias_find(IrModule *m, const char *name);
IrFunc *ir_func_new(IrModule *m, const char *name, IrType ret,
                    const IrType *params, u32 nparams);
BlockId ir_block_new(IrModule *m, IrFunc *f, const char *name);
ValueId ir_block_param(IrModule *m, IrFunc *f, BlockId b, IrType t);
IrBlock *ir_block(IrFunc *f, BlockId b);
IrType ir_value_type(const IrFunc *f, ValueId v);
bool ir_type_is_vector(IrType t);
bool ir_type_is_vector_int(IrType t);
bool ir_type_is_vector_float(IrType t);
IrType ir_vector_elem_type(IrType t);
u32 ir_vector_lanes(IrType t);
u32 ir_type_size(IrType t); /* 0 for void/unknown; vectors are 16 */

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
    Span loc; /* copied into each subsequently appended instruction */
} IrBuilder;

void ir_builder_at(IrBuilder *b, IrModule *m, IrFunc *f, BlockId blk);
void ir_builder_set_span(IrBuilder *b, Span span);
Span ir_builder_span(const IrBuilder *b);
u32 ir_intern_span(IrModule *m, Span span);
Span ir_debug_loc(const IrModule *m, u32 loc);
Span ir_inst_span(const IrModule *m, const IrInst *in);
void ir_mem_layout_register(IrModule *m, Span span, u64 size,
                            const IrByteRange *ranges, u32 nranges,
                            bool suppress_uninit);
const IrMemLayout *ir_mem_layout_find(const IrModule *m, const IrInst *in,
                                      u64 size);
ValueId ir_build2(IrBuilder *b, IrOp op, IrType t, IrOperand x, IrOperand y);
ValueId ir_build2_flags(IrBuilder *b, IrOp op, IrType t, IrOperand x,
                        IrOperand y, u8 flags);
ValueId ir_build1(IrBuilder *b, IrOp op, IrType t, IrOperand x);
ValueId ir_build_icmp(IrBuilder *b, IrIcmp p, IrOperand x, IrOperand y);
ValueId ir_build_fcmp(IrBuilder *b, IrFcmp p, IrOperand x, IrOperand y);
ValueId ir_build_vsplat(IrBuilder *b, IrType vector_type, IrOperand scalar);
ValueId ir_build_vextract(IrBuilder *b, IrOperand vector, u32 lane);
ValueId ir_build_vreduce(IrBuilder *b, IrOp op, IrOperand vector);
ValueId ir_build_alloca(IrBuilder *b, IrOperand size, u32 align);
ValueId ir_build_alloca_typed(IrBuilder *b, IrOperand size, u32 align,
                              EffTypeId etype);
ValueId ir_build_load(IrBuilder *b, IrType t, IrOperand ptr, u32 align,
                      u8 flags);
ValueId ir_build_load_typed(IrBuilder *b, IrType t, IrOperand ptr, u32 align,
                            u8 flags, EffTypeId etype);
void ir_build_store(IrBuilder *b, IrOperand val, IrOperand ptr, u32 align,
                    u8 flags);
void ir_build_store_typed(IrBuilder *b, IrOperand val, IrOperand ptr, u32 align,
                          u8 flags, EffTypeId etype);
ValueId ir_build_ptradd(IrBuilder *b, IrOperand ptr, IrOperand off);
void ir_build_memcpy(IrBuilder *b, IrOperand dst, IrOperand src, IrOperand size,
                     u32 align, u8 flags);
void ir_build_memset(IrBuilder *b, IrOperand dst, IrOperand byte,
                     IrOperand size, u32 align, u8 flags);
ValueId ir_build_select(IrBuilder *b, IrOperand c, IrOperand x, IrOperand y);
void ir_call_mark_variadic(IrBuilder *b);
void ir_call_mark_noreturn(IrBuilder *b);
void ir_load_mark_self_init(IrBuilder *b);
void ir_branch_mark_flow_provenance(IrBuilder *b);
void ir_ret_mark_implicit(IrBuilder *b);
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
/* `va_start ptr` — see the IrOp comment for what codegen fills. */
void ir_build_va_start(IrBuilder *b, IrOperand ap);
void ir_build_asm(IrBuilder *b, u32 asm_index, const IrOperand *ops, u32 nops);
ValueId ir_build_stacksave(IrBuilder *b);
void ir_build_stackrestore(IrBuilder *b, IrOperand tok);
/* seq_cst RMW: returns the OLD value; val/result type = t (int only). */
ValueId ir_build_atomicrmw(IrBuilder *b, IrAtomicRmw op, IrType t,
                           IrOperand ptr, IrOperand val);
/* seq_cst compare-exchange: returns the OLD memory value; success is
 * `icmp eq old, expected` at the call site. */
ValueId ir_build_cmpxchg(IrBuilder *b, IrType t, IrOperand ptr,
                         IrOperand expected, IrOperand desired);
/* Every reserved opcode routes here and ICEs naming its sprint. */
void ir_build_reserved(IrBuilder *b, IrOp op);

/* --- text (src/ir/print.c, src/ir/parse.c) ------------------------------- */

/* Deterministic: values renumber %0.. in definition order per function,
 * blocks print in layout order, globals in declaration order. No
 * pointers, no hashes — two prints of one module are byte-identical. */
void ir_print_module(FILE *out, const IrModule *m);
void ir_print_module_buf(Buf *out, const IrModule *m);
const char *ir_type_name(IrType t);
const char *ir_etype_name(EffTypeId t);
const char *ir_op_name(IrOp op);
const char *ir_icmp_name(IrIcmp p);
const char *ir_fcmp_name(IrFcmp p);
const char *ir_abi_ret_name(u8 k);
const char *ir_rmw_name(u8 k);
IrModule *ir_parse_module(Arena *arena, DiagCtx *dc, const char *src,
                          const char *path);

/* Structural equality — the round-trip invariant's judge. Compares
 * everything except interned pointer identities (names by content) and
 * optional debug locations, which the textual .cgfir format does not carry. */
bool ir_module_struct_eq(const IrModule *a, const IrModule *b);

/* Deletes blocks unreachable from the entry, compacting the block array
 * and remapping every edge target. The verifier REJECTS orphans (check
 * 6), and C legally puts statements after `return` — so producers run
 * this instead of abandoning dead blocks. Values defined in deleted
 * blocks stay in the vals table (ids must remain stable) but lose their
 * def_block; lowering never lets an SSA value cross from dead code into
 * live code (locals travel through allocas), so no live use can see one. */
void ir_func_remove_unreachable(IrFunc *f);
/* Same cleanup, retaining the first source span from each removed block for
 * the flow-warning analysis clone. */
void ir_func_remove_unreachable_with_log(IrFunc *f);
void ir_func_record_removed(IrFunc *f, BlockId block, u8 flags);
void ir_func_record_removed_span(IrFunc *f, BlockId block, Span loc, u8 flags);
void ir_func_record_removed_region(IrFunc *f, BlockId block, Span loc,
                                   u32 region, u8 flags);

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
/* Like ir_verify, and additionally writes a one-line summary of the
 * FIRST failure ("check N in @func: ...") into `why` — the driver
 * appends it to CGF_DUMP_BAD_IR dumps as a trailing comment. */
bool ir_verify_report(DiagCtx *dc, const IrModule *m, char *why,
                      size_t why_cap);

/* --- the volatile law (Sprint 20) -----------------------------------------
 * Each C-level volatile access is exactly one flagged load/store, and no
 * pass may introduce, remove, merge, split, or reorder volatile ops
 * relative to EACH OTHER. The count is the structural tripwire: Sprint
 * 30's pass manager snapshots per-function counts before each pass and
 * calls the check after — a mismatch under CGF_VERIFY_AFTER_EACH is an
 * ICE naming the pass. */
u32 ir_count_volatile_ops(const IrFunc *f);
/* How a module symbol binds, which is what decides GOT/PLT routing under
 * position-independent code. Both backends need it and both were about to
 * grow their own copy. */
typedef struct IrSymBinding {
    bool defined_here; /* this module carries the body or the storage */
    bool external;     /* visible outside the module (may be preempted) */
} IrSymBinding;

IrSymBinding ir_sym_binding(const IrModule *m, u32 sym_index);
bool ir_sym_is_tls(const IrModule *m, u32 sym_index);

/* Snapshot every function's count into out[m->nfuncs]. */
void ir_snapshot_volatile(const IrModule *m, u32 *out);
/* True iff current counts match `before`; on false, *bad_func gets the
 * first offending function index. */
bool ir_volatile_counts_match(const IrModule *m, const u32 *before,
                              u32 *bad_func);
/* Strong form used by the pass manager: arena-stable identities for volatile
 * and seq_cst instructions catch reorder and remove+replace, not only count
 * drift.  Interprocedural passes use the two explicit topology policies below
 * rather than silently disabling the tripwire. */
typedef struct IrVolatileSnapshot {
    const char *func_name;
    const IrInst **ops;
    u32 nops;
} IrVolatileSnapshot;
/* `out` has m->nfuncs + 1 entries; the final func_name is NULL.  Stable
 * function identity lets an interprocedural pass compact nonvolatile dead
 * functions without comparing a shifted survivor against the wrong row. */
void ir_snapshot_volatile_order(Arena *arena, const IrModule *m,
                                IrVolatileSnapshot *out);
bool ir_volatile_order_matches(const IrModule *m,
                               const IrVolatileSnapshot *before, u32 *bad_func);
bool ir_pinned_delete_funcs_matches(const IrModule *m,
                                    const IrVolatileSnapshot *before,
                                    u32 *bad_func);
bool ir_pinned_inline_matches(const IrModule *m,
                              const IrVolatileSnapshot *before, u32 *bad_func);

#endif
