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
    IrOperand addr; /* always ptr-typed */
    IrType unit;    /* type of the underlying load/store unit */
    u8 bit_shift;   /* bitfield only: bit position within the unit */
    u8 bit_width;   /* bitfield only: 0 means "not a bitfield" */
    bool is_bitfield;
    bool is_volatile;
    bool is_signed; /* of the FIELD type: drives the re-narrowing */
    u32 align;
} Lvalue;

typedef struct LoopCtx {
    BlockId break_target;
    BlockId continue_target; /* BLOCK_INVALID in a switch entry */
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
    ValueId sret; /* hidden aggregate-return pointer, or 0 */
    const char *fname;

    /* module-wide */
    Strmap globals;    /* Symbol* -> (uintptr_t)(module sym index + 1) */
    Strmap func_ids;   /* Symbol* -> (uintptr_t)(IrFunc index + 1), for
                          the functions THIS module emits */
    u32 nstrings;      /* string-literal globals emitted, for naming */
    u32 nlocal_static; /* block-scope statics, for name mangling */
    u32 ntemps;        /* aggregate temporaries (naming only) */
    bool verify_each;  /* CGF_VERIFY_AFTER_EACH=1: verify per function */
    bool failed;       /* a deferral hard-error fired */
} Lower;

/* Lowers a whole translation unit. Returns NULL after reporting if a
 * deferred construct was reached (the diagnostic names its sprint). */
IrModule *lower_translation_unit(Arena *arena, DiagCtx *dc, Sema *sema,
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
bool lower_is_aggregate(const Type *t);
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
void lower_bind_static(Lower *lo, Symbol *sym, u32 sym_index);
/* An i64 constant operand (offsets, sizes). */
IrOperand lower_i64(i64 v);
/* String literal -> internal global; returns the module symbol index. */
u32 lower_string_lit(Lower *lo, const AstNode *e);
/* Aggregate temporary: entry-independent alloca in the CURRENT block
 * (legal anywhere per Sprint 17). */
ValueId lower_temp(Lower *lo, Type *t);
/* Every deferred path routes through here; the message NAMES the sprint. */
void lower_unimplemented(Lower *lo, Span span, const char *what, int sprint);

#endif
