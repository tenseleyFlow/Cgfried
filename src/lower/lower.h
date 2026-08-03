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

/* An lvalue: an address plus how to access the object there. Bitfields
 * carry (unit, shift, width); everything else loads/stores the unit
 * directly. The address is an OPERAND, not a ValueId: a global's address
 * is an IROP_SYMBOL and never becomes an instruction. */
typedef struct Lvalue {
    IrOperand addr;  /* always ptr-typed */
    IrType unit;     /* type of the underlying load/store unit */
    EffTypeId etype; /* C effective type for alias analysis */
    u8 bit_shift;    /* bitfield only: bit position within the unit */
    u8 bit_width;    /* bitfield only: 0 means "not a bitfield" */
    bool is_bitfield;
    bool is_volatile;
    bool is_atomic; /* _Atomic: loads/stores carry seq_cst (Sprint 20) */
    bool is_signed; /* of the FIELD type: drives the re-narrowing */
    u32 align;
} Lvalue;

/* One VLA-bearing lexical scope: the stacksave token (0 until the first
 * VLA declares) and the compound it belongs to. Exits restore the
 * OUTERMOST live token among the scopes they leave — restoring the
 * outermost subsumes every inner one. `return` restores nothing (the
 * epilogue's frame teardown subsumes it) and longjmp needs nothing (the
 * stack unwinds wholesale; tokens die with the frames). */
typedef struct VlaScope {
    ValueId token;
    const AstNode *compound;
    struct VlaScope *prev;
} VlaScope;

typedef struct LoopCtx {
    BlockId break_target;
    BlockId continue_target;   /* BLOCK_INVALID in a switch entry */
    struct VlaScope *vla_mark; /* scopes above this survive break/continue */
    struct LoopCtx *prev;
} LoopCtx;

/* One switch statement's label map, collected by the pre-pass. Case
 * blocks must exist before the body lowers — Duff's device falls INTO
 * `case` labels from inside statements that precede them textually. */
typedef struct SwitchCase {
    const AstNode *stmt; /* the AST_STMT_CASE / AST_STMT_DEFAULT node */
    BlockId block;
    i64 value; /* case value; unused for default */
    bool is_default;
    struct SwitchCase *next;
} SwitchCase;

typedef struct SwitchCtx {
    SwitchCase *cases;
    struct SwitchCtx *prev;
} SwitchCtx;

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
    VlaScope *vla_scopes; /* innermost first */
    Strmap vla_sizes;     /* Type* -> ValueId of the cached byte size */
    Strmap label_vla;     /* label name -> innermost VLA compound (AST) */
    ValueId sret;         /* hidden aggregate-return pointer, or 0 */
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
    u8 auto_var_init;           /* LowerAutoVarInit; emission mitigation */

    /* module-wide */
    Strmap globals;           /* Symbol* -> (uintptr_t)(module sym index + 1) */
    Strmap func_ids;          /* Symbol* -> (uintptr_t)(IrFunc index + 1), for
                                 the functions THIS module emits */
    Strmap string_pool;       /* content -> sym index + 1 (init.c's dedup) */
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
} LowerOptions;

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
/* Runtime initializer walk for automatic objects: zero-fills aggregates,
 * then evaluates and stores each element in SOURCE ORDER (the §1 law).
 * Mirrors constexpr.c's fill() cursor semantics exactly — including the
 * no-brace-elision limitation, which is the compiler's current shape. */
void lower_local_init(Lower *lo, IrOperand base, Type *t, AstNode *init);

/* --- shared helpers (src/lower/lower.c) ----------------------------------- */

IrType lower_irtype(Lower *lo, const Type *t); /* scalars only */
EffTypeId lower_efftype(Lower *lo, const Type *t);
bool lower_is_aggregate(const Type *t);
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
/* The byte size of a type as an i64 operand: a constant for complete
 * types, the cached (evaluated-once) value for a declared VLA, or a
 * fresh computation for an undeclared VLA type (sizeof(int[n]) — C17
 * says the size expression evaluates there). */
IrOperand lower_type_size(Lower *lo, Type *t);

/* --- SysV ABI plans (src/lower/abi.c) ------------------------------------- */

/* How one C argument travels. EIGHTBYTES replaces the Sprint 18 abstract
 * ptr-to-copy with 1-2 bit-carrying scalars; BYVAL keeps the pointer but
 * annotates it (codegen copies the pointee onto the stack). */
typedef enum { ABI_ARG_SCALAR, ABI_ARG_EIGHTBYTES, ABI_ARG_BYVAL } AbiArgKind;

typedef struct AbiArg {
    u8 kind;     /* AbiArgKind */
    u8 n;        /* EIGHTBYTES: 1 or 2 */
    IrType t[2]; /* eightbyte IR types (i64 / f64) */
    u32 size;
    u32 align;
} AbiArg;

typedef enum {
    ABI_RET_VOID,
    ABI_RET_SCALAR, /* the IR return type says it all (incl. f80/x87) */
    ABI_RET_SMALL,  /* one eightbyte: bit-carrying i64 or f64 return */
    ABI_RET_PAIR,   /* two eightbytes: sret-shaped IR + register truth */
    ABI_RET_SRET    /* MEMORY: hidden pointer + rax echo */
} AbiRetKind;

typedef struct AbiRet {
    u8 kind;        /* AbiRetKind */
    IrType small_t; /* SMALL: i64 or f64 */
    u8 ir_abi;      /* IrAbiRet for PAIR/SRET (goes on IrFunc.abi_ret) */
    u8 arg_annot;   /* IR_ARG_* kind for the hidden pointer argument */
    u32 size;
    u32 align;
} AbiRet;

void abi_classify_arg(Lower *lo, Type *t, AbiArg *out);
void abi_classify_ret(Lower *lo, Type *t, AbiRet *out);
/* gp/fp register consumption of one classified argument (for va_start's
 * gp_offset/fp_offset constants and the 6/8 register caps). */
void abi_arg_regs(const AbiArg *a, u32 *gp, u32 *fp);

#endif
