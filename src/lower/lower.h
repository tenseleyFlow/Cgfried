#ifndef CGF_LOWER_H
#define CGF_LOWER_H

#include "ir/ir.h"
#include "parse/ast.h"
#include "sema/sema.h"
#include "util/strmap.h"

/* AST -> IR lowering (Sprint 18). Consumes the TYPED tree: every implicit
 * conversion is already a materialized AST_EXPR_CAST (Sprint 13), every
 * offset and bitfield position is already computed (Sprint 14), and every
 * static initializer already has exact bytes (Sprint 15). Lowering never
 * re-derives a language rule; when it needs a type-level fact (the UAC
 * type for a compound assignment, signedness, a layout) it calls the
 * sema/layout helpers rather than reimplementing them.
 *
 * EVALUATION-ORDER LAW. C leaves evaluation order within expressions
 * unspecified; cgf fixes STRICT LEFT-TO-RIGHT — for binary operands, call
 * arguments, and initializer-list element side effects. Deterministic
 * builds require one order, and any order is conformant; chasing gcc's
 * would mean chasing an unspecified moving target (gcc itself pushes call
 * args right-to-left at -O0 on x86-64). Corpus tests must never encode
 * gcc's order. The divergence fixture:
 *
 *     int i = 0;
 *     int f(void){ return ++i; }  int g(void){ return i *= 10; }
 *     f() + g()   ->  1 + 10 = 11 under cgf, always.
 *     h(f(), g()) ->  f() first under cgf, always; gcc may do g() first.
 *
 * Pinned by tests/programs/lower/eval_order.c (staged for execution at
 * Sprint 25; its IR golden asserts the call order NOW).
 *
 * AGGREGATE VALUES. Aggregates are not SSA values (Sprint 17 law), so an
 * aggregate-typed rvalue is represented by the ADDRESS of the object (or
 * of a materialized temporary) — lower_rvalue returns a ptr ValueId for
 * them, and the consumers (assignment, calls, returns, ?:) memcpy through
 * it. Struct assignment is ONE memcpy of sizeof(T) bytes, never a
 * memberwise copy: unions would lose the notion of the live member and
 * padding bytes would diverge from gcc's images. */

/* An lvalue: an address plus how to access the object there. Ordinary
 * bitfields carry (unit, shift, width). A packed bitfield is byte-addressed:
 * it may straddle its declared unit and a 64-bit field beginning at bit 7
 * occupies nine bytes, so no single scalar load can represent it. Everything
 * else loads/stores the unit directly. The address is an OPERAND, not a
 * ValueId: a global's address is an IROP_SYMBOL and never becomes an
 * instruction. */
typedef struct Lvalue {
    IrOperand addr;  /* always ptr-typed */
    IrType unit;     /* type of the underlying load/store unit */
    EffTypeId etype; /* C effective type for alias analysis */
    u8 bit_shift;    /* bitfield only: bit position within the unit */
    u8 bit_width;    /* bitfield only: 0 means "not a bitfield" */
    bool is_bitfield;
    bool packed_bitfield;
    bool is_volatile;
    bool is_atomic; /* _Atomic: loads/stores carry seq_cst (Sprint 20) */
    bool is_signed; /* of the FIELD representation: drives re-narrowing */
    u32 align;
} Lvalue;

/* One `cleanup(func)` variable, recorded when its declaration lowers.
 * PREPENDED to its scope's list, so walking the list forward yields REVERSE
 * declaration order — which is the order the calls must run in. */
typedef struct ScopeCleanup {
    ValueId slot;      /* the variable's alloca: the `&var` argument */
    struct Symbol *fn; /* the function to call */
    Span span;         /* the declaration, for the call's source location */
    struct ScopeCleanup *next;
} ScopeCleanup;

/* One lexical scope. Two independent scope-exit obligations ride on it and
 * they behave DIFFERENTLY, which is why this is one record with two lists
 * rather than two stacks:
 *
 *  - VLA storage. The stacksave token is 0 until the scope's first VLA
 *    declares. An exit restores the OUTERMOST live token among the scopes it
 *    leaves, because one restore subsumes every inner one. `return` restores
 *    NOTHING — the epilogue's frame teardown subsumes it — and longjmp needs
 *    nothing, since the stack unwinds wholesale and tokens die with frames.
 *
 *  - `cleanup` calls. Every scope being left runs EVERY one of its variables,
 *    innermost scope first and reverse declaration order within a scope; no
 *    call subsumes another. `return` runs all of them, which is exactly where
 *    the two rules diverge.
 *
 * Where a scope owes both, the CALLS COME FIRST: a cleanup function receives
 * a pointer to its variable, and for a VLA that pointer is into the storage
 * the restore is about to release. */
typedef struct LexScope {
    ValueId token;
    const AstNode *compound;
    ScopeCleanup *cleanups;
    struct LexScope *prev;
} LexScope;

/* The chain of compounds enclosing a label, innermost first, built by the
 * label pre-pass. Nodes are shared structurally: every label inside one
 * compound points at the same node, so the whole function costs one node per
 * nesting level rather than one per label.
 *
 * A goto needs the whole CHAIN, not just the innermost entry, because C
 * permits jumping INTO a cleanup scope. The scopes actually left are those
 * between the goto and the innermost compound COMMON to both sides, and
 * finding a common ancestor takes both chains. */
typedef struct LabelScope {
    const AstNode *compound;
    struct LabelScope *prev;
} LabelScope;

typedef struct LoopCtx {
    BlockId break_target;
    BlockId continue_target;     /* BLOCK_INVALID in a switch entry */
    struct LexScope *scope_mark; /* scopes above this survive break/continue */
    struct LoopCtx *prev;
} LoopCtx;

/* One switch statement's label map, collected by the pre-pass. Case
 * blocks must exist before the body lowers — Duff's device falls INTO
 * `case` labels from inside statements that precede them textually. */
typedef struct SwitchCase {
    const AstNode *stmt; /* the AST_STMT_CASE / AST_STMT_DEFAULT node */
    BlockId block;
    i64 value; /* case value (range LOW end); unused for default */
    i64 hi;    /* GNU `case lo ... hi:` high end, inclusive; else == value */
    bool is_range;
    bool is_default;
    struct SwitchCase *next;
} SwitchCase;

typedef struct SwitchCtx {
    SwitchCase *cases;
    struct SwitchCtx *prev;
} SwitchCtx;

/* One already-evaluated anonymous argument of a GNU forwarding wrapper.
 * Aggregates use the ordinary lowering convention: VALUE is their address;
 * scalar VALUEs are SSA operands. The destination call reclassifies TYPE
 * against its own live ABI budget, so forwarding after different named
 * arguments cannot inherit the wrapper call's register placement. */
typedef struct VaPackArg {
    IrOperand value;
    Type *type;
    u8 access_flags;
} VaPackArg;

typedef struct VaPackContext {
    Symbol *wrapper;
    VaPackArg *args;
    u32 nargs;
    Symbol **params;
    bool *param_constant;
    u32 nparams;
    LexScope *scope_mark;
    BlockId return_target;
    ValueId return_slot;
    Type *return_type;
} VaPackContext;

/* An i/n/N asm operand is target-valid only when its instruction survives
 * source-level constant control flow.  Lowering records the check without
 * diagnosing immediately, then asm.c validates it against the completed CFG.
 * `block` is filled after all operand expressions have lowered, because a
 * later conditional operand may move the asm itself into a join block. */
typedef struct DeferredAsmImmediate {
    AstNode *expr;
    const char *constraint;
    Span span;
    BlockId block;
    struct DeferredAsmImmediate *next;
} DeferredAsmImmediate;

/* A configuration-derived __builtin_constant_p branch is known while
 * lowering, but flow warnings still need to know why its untaken arm vanished.
 * Record the candidate arm until all forward goto/case entries are present. */
typedef struct DeferredConfigRemoval {
    BlockId block;
    struct DeferredConfigRemoval *next;
} DeferredConfigRemoval;

typedef struct Lower {
    Arena *arena;
    DiagCtx *dc;
    Sema *sema; /* for layout_of / conv_*_type / target; NEVER for new
                   diagnostics — sema already ran to completion */
    IrModule *m;

    /* per function */
    IrFunc *fn;
    IrBuilder b;
    bool terminated; /* current block already ended in a terminator */
    Strmap locals;   /* Symbol* (pointer bytes) -> alloca/param ptr id */
    Strmap labels;   /* label name -> BlockId (function scope, pre-pass) */
    LoopCtx *loops;
    SwitchCtx *switches;
    LexScope *scopes;   /* innermost first */
    Strmap vla_sizes;   /* Type* -> ValueId of the cached byte size */
    Strmap label_vla;   /* label name -> private VlaLabelState chain */
    Strmap label_scope; /* label name -> innermost enclosing compound (AST) */
    ValueId sret;       /* hidden aggregate-return pointer, or 0 */
    const char *fname;
    /* Sprint 19 ABI state for the CURRENT function. */
    struct AbiRet *cur_abi_ret; /* how `return e` leaves the function */
    u32 named_gp;               /* gp registers consumed by named params */
    u32 named_fp;               /* fp registers consumed by named params */
    Type *cur_functype;         /* the C function type being lowered */
    Type *cur_return_type;      /* source-level result, incl. true void */
    Symbol *initializing_sym;   /* exact direct -Winit-self provenance */
    u32 dead_region;            /* current contiguous unreachable source */
    u32 next_dead_region;       /* stable diagnostic region numbering */
    DeferredAsmImmediate *deferred_asm_immediates;
    DeferredAsmImmediate *deferred_asm_immediates_tail;
    DeferredConfigRemoval *deferred_config_removals;
    DeferredConfigRemoval *deferred_config_removals_tail;
    u8 auto_var_init;         /* LowerAutoVarInit; emission mitigation */
    bool safe_pointer_checks; /* preserve raw pointer-index operands */
    /* Non-NULL only while a GNU variadic wrapper body is being specialized
     * into its caller. No pack marker is ever admitted to public IR. */
    VaPackContext *va_pack;

    /* module-wide */
    Strmap globals; /* Symbol* -> arena-owned u32 (sym index + 1) */
    /* A symbol-table entry can exist before storage does: a relocation in
     * an earlier initializer interns the referenced name.  Keep emission
     * state separate or that forward reference suppresses the definition. */
    Strmap emitted_globals; /* Symbol* -> arena-owned nonzero marker */
    Strmap func_ids;    /* Symbol* -> arena-owned u32 (IrFunc index + 1), for
                           the functions THIS module emits */
    Strmap string_pool; /* content -> arena-owned u32 sym index + 1 */
    /* Function-name objects are NOT ordinary string literals: C11 gives
     * `__func__` one distinct static const array object per function, while
     * GNU `__FUNCTION__` and `__PRETTY_FUNCTION__` alias that same object. */
    Strmap func_name_objects; /* function name -> u32 sym index + 1 */
    u32 nstrings;             /* string-literal globals emitted, for naming */
    u32 nlocal_static;        /* block-scope statics, for name mangling */
    u32 ntemps;               /* aggregate temporaries (naming only) */
    bool include_inline_defs; /* analysis module, never object emission */
    bool verify_each;         /* CGF_VERIFY_AFTER_EACH=1: verify per function */
    bool failed;              /* a deferral hard-error fired */
} Lower;

typedef enum LowerAutoVarInit {
    LOWER_AUTO_VAR_INIT_NONE,
    LOWER_AUTO_VAR_INIT_ZERO,
    LOWER_AUTO_VAR_INIT_PATTERN,
} LowerAutoVarInit;

typedef struct LowerOptions {
    LowerAutoVarInit auto_var_init;
    bool safe_pointer_checks;
} LowerOptions;

/* Strmap's payload is intentionally generic, but lowering's numeric maps
 * must not encode integers as fabricated pointers. */
u32 *lower_u32map_get(const Strmap *map, const char *key, size_t key_len);
void lower_u32map_put(Lower *lo, Strmap *map, const char *key, size_t key_len,
                      u32 value);

/* Lowers a whole translation unit. Returns NULL after reporting if a
 * deferred construct was reached (the diagnostic names its sprint). */
IrModule *lower_translation_unit(Arena *arena, DiagCtx *dc, Sema *sema,
                                 AstNode *tu);
/* Emission-only mitigations are explicit so warning/flow analysis continues
 * to see the original, unmitigated program.  A NULL options pointer is the
 * same as lower_translation_unit(). */
IrModule *lower_translation_unit_with_options(Arena *arena, DiagCtx *dc,
                                              Sema *sema, AstNode *tu,
                                              const LowerOptions *options);
/* Flow analysis needs bodies that the C inline-emission rules deliberately
 * omit from object modules. This analysis-only surface keeps codegen's
 * Sprint 16 linkage contract unchanged. */
IrModule *lower_translation_unit_for_flow(Arena *arena, DiagCtx *dc, Sema *sema,
                                          AstNode *tu);

/* --- expressions (src/lower/expr.c) -------------------------------------- */

/* Expression results are OPERANDS: constants stay constants (this IR has
 * no "materialize" instruction) and computed values arrive as IROP_VALUE.
 * An aggregate-typed rvalue is the ADDRESS of the object/temporary. */
Lvalue lower_lvalue(Lower *lo, AstNode *e); /* address; no load */

/* src/lower/asm.c */
void lower_asm(Lower *lo, AstNode *s);
bool lower_asm_clobber_reg(const char *name, u8 *out);
IrOperand lower_rvalue(Lower *lo, AstNode *e);
/* Stores through an lvalue; returns the RESULT VALUE of an assignment,
 * which for a bitfield is the stored value re-narrowed (masked and, for
 * signed fields, sign-extended) — never the raw RHS. */
IrOperand lower_store(Lower *lo, Lvalue lv, IrOperand v);
IrOperand lower_load(Lower *lo, Lvalue lv);
/* Scalar conversion between two C types, emitting the right instruction
 * (or nothing). The one place the cast matrix lives. */
IrOperand lower_scalar_convert(Lower *lo, IrOperand v, Type *from, Type *to);
/* Condition value: i32, 0/1 where the source op already produces one
 * (comparisons, !, &&, ||); otherwise `expr != 0` — folded so `if (a<b)`
 * emits ONE icmp, never icmp -> icmp-ne -> condbr. */
IrOperand lower_cond(Lower *lo, AstNode *e);

/* --- statements (src/lower/stmt.c) ---------------------------------------- */

void lower_stmt(Lower *lo, AstNode *s);
/* Validate the i/n/N operands delayed until the function CFG is complete. */
void lower_asm_validate_deferred_immediates(Lower *lo);
/* Preserve configuration provenance for a known, ultimately unreachable arm. */
void lower_record_deferred_config_removals(Lower *lo);
/* A GNU statement expression `({ ... })`. Lives in stmt.c because it needs
 * LexScope and scope_exit_here; called from lower_rvalue, because the thing
 * it produces is a VALUE. */
IrOperand lower_stmt_expr(Lower *lo, AstNode *e);
/* Runtime initializer walk for automatic objects: zero-fills aggregates,
 * then evaluates and stores each element in SOURCE ORDER (the §1 law).
 * Mirrors constexpr.c's fill() cursor semantics exactly — including the
 * no-brace-elision limitation, which is the compiler's current shape. */
void lower_local_init(Lower *lo, IrOperand base, Type *t, AstNode *init);

/* --- shared helpers (src/lower/lower.c) ----------------------------------- */

IrType lower_irtype(Lower *lo, const Type *t); /* scalars only */
EffTypeId lower_efftype(Lower *lo, const Type *t);
bool lower_is_aggregate(const Type *t);
u8 lower_aggregate_access_flags(const AstNode *e);
void lower_memcpy_aggregate(Lower *lo, IrOperand dst, IrOperand src, Type *t,
                            u32 align, u8 flags);
/* Fresh block; the label is arena-formatted for deterministic names. */
BlockId lower_new_block(Lower *lo, const char *prefix);
/* Moves the builder; resets the terminated flag. */
void lower_at(Lower *lo, BlockId b);
/* The symbol's address as an operand: locals yield their alloca value,
 * everything else an IROP_SYMBOL. */
IrOperand lower_sym_addr(Lower *lo, Symbol *sym);
/* Module symbol index for a file-scope symbol (interned on first use). */
u32 lower_global_sym(Lower *lo, Symbol *sym);
/* True (and *index set) iff this module emits a definition of sym. */
bool lower_internal_func(Lower *lo, Symbol *sym, u32 *index);
/* Binds a block-scope object to its alloca / a local static to its
 * mangled global's symbol index; lower_sym_addr consults both. */
void lower_bind_local(Lower *lo, Symbol *sym, ValueId slot);
ValueId lower_local_slot(Lower *lo, Symbol *sym);
void lower_bind_static(Lower *lo, Symbol *sym, u32 sym_index);
/* An i64 constant operand (offsets, sizes). */
IrOperand lower_i64(i64 v);
/* String literal -> pooled internal global (content-deduped); returns
 * the module symbol index. */
u32 lower_string_lit(Lower *lo, const AstNode *e);
/* C11 __func__ / GNU function-name aliases -> one static const array. */
u32 lower_func_name_object(Lower *lo, const AstNode *e);
/* Anonymous object (string or file-scope compound literal) -> symbol. */
u32 lower_anon_sym(Lower *lo, const AstNode *e);
/* Aggregate temporary: entry-independent alloca in the CURRENT block
 * (legal anywhere per Sprint 17). */
ValueId lower_temp(Lower *lo, Type *t);
/* Every deferred path routes through here; the message NAMES the sprint. */
void lower_unimplemented(Lower *lo, Span span, const char *what, int sprint);
/* Allocate fixed-size automatic objects in the entry block before statement
 * lowering. A goto may legally enter their lexical block after the
 * declaration, so every later load must still be dominated by its alloca. */
void lower_prebind_locals(Lower *lo, AstNode *body);
/* The byte size of a type as an i64 operand: a constant for fixed-size
 * types, the cached (evaluated-once) value for a declared runtime-sized
 * type, or a fresh computation for an undeclared one (sizeof(int[n]) —
 * C17 says the size expression evaluates there). GNU records containing
 * VLA members are runtime-sized too. */
IrOperand lower_type_size(Lower *lo, Type *t);
/* Evaluate and cache every runtime-sized layer named by a declaration. The
 * walk passes through pointer layers because pointer-to-VLA declarations
 * still evaluate their bounds even though the declared object is
 * pointer-sized. */
void lower_prime_runtime_sizes(Lower *lo, Type *t);

/* --- SysV ABI plans (src/lower/abi.c) ------------------------------------- */

/* How one C argument travels. EIGHTBYTES replaces the Sprint 18 abstract
 * ptr-to-copy with 1-2 bit-carrying scalars; BYVAL keeps the pointer but
 * annotates it.
 *
 * BYVAL means DIFFERENT THINGS per ABI, and conflating them is the #1
 * cross-ABI porting bug (Sprint 48):
 *   SysV x86-64 — codegen copies the POINTEE onto the stack; the pointer
 *     itself never travels at runtime and the argument consumes no
 *     register.
 *   AAPCS64     — the caller allocates the copy and passes its ADDRESS as
 *     an ordinary argument, so it consumes exactly ONE GPR and no stack
 *     bytes at all.
 * The plan is the same shape; the backend reads its own ABI's meaning.
 *
 * HFA is AAPCS64-only: 1-4 leaves of the SAME floating type travelling in
 * CONSECUTIVE v-regs. It is kept distinct from EIGHTBYTES because the
 * backend must be able to apply the NAF rule (once any FP argument is
 * stacked, the remaining v-regs are dead for later FP arguments), which
 * needs to know the leaves belong to one aggregate.
 *
 * STACK is the register bank running out: an aggregate that would otherwise
 * travel in registers but cannot fit ENTIRELY in what remains. It is passed
 * by value on the stack, which is what SysV BYVAL already means there and
 * what AAPCS64 BYVAL emphatically does not -- hence a distinct kind rather
 * than a reuse. abi_arg_place is the only thing that produces it. */
typedef enum {
    ABI_ARG_SCALAR,
    ABI_ARG_EIGHTBYTES,
    ABI_ARG_BYVAL,
    ABI_ARG_HFA,
    ABI_ARG_STACK
} AbiArgKind;

/* An HFA has at most four register leaves. Once an AAPCS64 HFA is forced to
 * the stack it is re-planned as eightbyte byte carriers; four binary128
 * leaves therefore need eight stack leaves. SysV never sets n > 2. */
#define ABI_MAX_HFA_LEAVES 4
#define ABI_MAX_STACK_LEAVES (ABI_MAX_HFA_LEAVES * 2)

typedef struct AbiArg {
    u8 kind;          /* AbiArgKind */
    u8 n;             /* EIGHTBYTES: 1-2; HFA: 1-4; STACK: 1-8 */
    u8 even_gp;       /* Linux AAPCS64: first leaf starts at even xN */
    u8 stack_align16; /* AAPCS64: first stack leaf aligns NSAA to 16 */
    IrType t[ABI_MAX_STACK_LEAVES]; /* eightbyte / HFA-leaf IR types */
    u32 size;
    u32 align;
} AbiArg;

typedef enum {
    ABI_RET_VOID,
    ABI_RET_SCALAR, /* the IR return type says it all (incl. f80/x87) */
    ABI_RET_SMALL,  /* aggregate returned as one wire scalar: i64/f64/f80 */
    ABI_RET_PAIR,   /* two eightbytes: sret-shaped IR + register truth */
    ABI_RET_SRET,   /* MEMORY: hidden pointer (SysV rdi echoed in rax;
                       AAPCS64 x8, NOT guaranteed preserved at return) */
    ABI_RET_HFA     /* AAPCS64: 1-4 FP leaves returned in v0-v3 */
} AbiRetKind;

typedef struct AbiRet {
    u8 kind;        /* AbiRetKind */
    IrType small_t; /* SMALL: i64, f64, or f80 */
    u8 ir_abi;      /* IrAbiRet for PAIR/SRET (goes on IrFunc.abi_ret) */
    u8 arg_annot;   /* IR_ARG_* kind for the hidden pointer argument */
    u32 size;
    u32 align;
    u32 n; /* HFA: leaf count 1-4 (goes on IrFunc.abi_ret_n) */
} AbiRet;

void abi_classify_arg(Lower *lo, Type *t, AbiArg *out);
void abi_classify_ret(Lower *lo, Type *t, AbiRet *out);

/* The running argument-register budget for ONE call or definition. Both psABIs
 * make placement depend on what earlier arguments already consumed, so
 * classification alone cannot decide where an argument goes -- see
 * abi_arg_place. */
typedef struct AbiBudget {
    u32 gp; /* general registers committed so far (6 on SysV, 8 on AAPCS64) */
    u32 fp; /* SSE/SIMD registers committed so far (8 on both) */
} AbiBudget;

/* Seed a budget from the return convention: a hidden MEMORY-return pointer is
 * a real runtime argument and spends a register before the first declared
 * one. */
void abi_budget_init(Lower *lo, AbiBudget *b, const AbiRet *ret);

/* Is the selected target's argument convention AAPCS64? Exported because the
 * two argument walks and the classifier all need it, and a third private copy
 * of the target switch is a drift hazard. */
bool abi_is_aapcs64(Lower *lo);

/* Decide where one classified argument actually goes, given what is left, and
 * charge it to the budget. May REWRITE a->kind: an aggregate that cannot be
 * placed entirely in registers is passed entirely in memory instead. Every
 * walk over an argument list must call this, or the two halves of a call
 * disagree.
 *
 * `anon` says this is an argument past the callee's prototype. It is knowable
 * ONLY at a call site -- the IR records that a callee is variadic, never where
 * its prototype stopped -- so it arrives as a flag rather than as a property
 * of the type, and the DEFINITION walk always passes false. That asymmetry is
 * deliberate: a definition has no anonymous parameters, so the two walks stay
 * identical for everything else, which is what keeps the halves of a call in
 * agreement. Only Apple treats an anonymous argument differently (ABI-004);
 * AAPCS64 and SysV place it exactly as a named one. */
void abi_arg_place(Lower *lo, AbiArg *a, AbiBudget *b, bool anon);

/* An object's alignment is its type's, raised by any _Alignas on the
 * declaration. One helper so the global, static-local and automatic
 * paths cannot drift -- they did: every one of them ignored _Alignas. */
u32 lower_object_align(const struct Symbol *sym, u64 natural);
/* The automatic-storage form propagates the complete validated requirement;
 * both backends align fixed and dynamic object addresses inside their frames.
 */
u32 lower_auto_align(const struct Symbol *sym, u64 natural);

/* The linker name for a symbol: its `__asm__("...")` label if it has one,
 * otherwise its C identifier. Diagnostics keep the identifier. */
const char *lower_link_name(const struct Symbol *sym);

#endif
