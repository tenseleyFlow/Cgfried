#ifndef CGF_AST_H
#define CGF_AST_H

#include "attr.h"
#include "diag.h"
#include "lex/lex.h"
#include "util/arena.h"
#include "util/base.h"

/* The AST is SYNTAX. AstType mirrors what the declarator spelled, not what
 * sema will conclude: Sprint 12 builds real Types from these. Nothing here
 * stores a computed size, offset, or constant VALUE — enum values, bitfield
 * widths, and array sizes are kept as expressions for Sprint 15 to
 * evaluate. Every node carries a Span. */

typedef struct AstNode AstNode;
typedef struct AstType AstType;
/* Sema annotates the tree in place rather than building a parallel one.
 * Forward-declared so ast.h stays free of any sema dependency — the
 * arrow points one way only. */
struct Type;
struct Symbol;

typedef enum AstKind {
    AST_DECL,        /* one declarator + optional initializer */
    AST_FUNC_DEF,    /* declarator + body (+ K&R decl list) */
    AST_RECORD_DECL, /* struct/union definition or forward ref */
    AST_ENUM_DECL,
    AST_ENUMERATOR, /* name + optional VALUE expression; no type of its
                       own until sema assigns the enum's compatible type */
    AST_STATIC_ASSERT,
    AST_INIT_LIST,
    AST_DESIGNATOR,
    AST_TRANSLATION_UNIT,
    AST_EMPTY_DECL, /* `struct S;` / stray `;` — declares no object */

    /* Expressions — the full C11 6.5 grammar. */
    AST_EXPR_INT,
    AST_EXPR_FLOAT,
    AST_EXPR_CHAR,
    AST_EXPR_STRING,
    AST_EXPR_IDENT,
    AST_EXPR_UNARY,   /* prefix op; `is_postfix` marks x++ / x-- */
    AST_EXPR_BINARY,  /* also the assignment ops and `,` */
    AST_EXPR_COND,    /* lhs ? mid : rhs */
    AST_EXPR_CALL,    /* lhs(args...) */
    AST_EXPR_INDEX,   /* lhs[rhs] */
    AST_EXPR_MEMBER,  /* lhs.name / lhs->name (`is_arrow`) */
    AST_EXPR_CAST,    /* (type)lhs */
    AST_EXPR_SIZEOF,  /* sizeof type / sizeof lhs */
    AST_EXPR_ALIGNOF, /* _Alignof(type) — the expr form is GNU */
    AST_EXPR_GENERIC, /* _Generic(lhs, items...) */
    AST_GENERIC_ASSOC,
    AST_EXPR_COMPOUND_LIT, /* (type){ init } */
    AST_EXPR_PAREN,
    AST_EXPR_VA_ARG, /* __builtin_va_arg(lhs, type) — the one call-like
                        form whose second argument is a TYPE (Sprint 19) */
    /* GNU argument packs are not ordinary values. The pack form is a
     * call-argument PLACEHOLDER that lowering expands before ABI placement;
     * the length form is an int value supplied by that same expansion. */
    AST_EXPR_VA_ARG_PACK,     /* __builtin_va_arg_pack() */
    AST_EXPR_VA_ARG_PACK_LEN, /* __builtin_va_arg_pack_len() */
    AST_EXPR_OFFSETOF,        /* __builtin_offsetof(type, designator): `lhs` is
                                 the designator chain built over an
                                 AST_EXPR_OFFSETOF_BASE placeholder (Sprint 28) */
    AST_EXPR_OFFSETOF_BASE,   /* the anchor a designator chain bottoms out
                                 on; never evaluated, never typed */
    /* `__builtin_types_compatible_p(T1, T2)`: TWO type names and no
     * expression at all, so it needs its own form exactly as va_arg and
     * offsetof do. Folds to an int 0/1 in sema; usable as an array bound. */
    AST_EXPR_TYPES_COMPATIBLE, /* type = T1, type2 = T2 */
    /* `__builtin_choose_expr(cond, a, b)`. The condition is an INTEGER
     * CONSTANT EXPRESSION and the result is the SELECTED arm -- type and
     * value both. The unselected arm is NOT evaluated but IS type-checked;
     * gcc rejects a bad member access, an undeclared identifier and a
     * wrong-arity call there, all measured. The sprint file said
     * "untype-checked beyond parse" and was wrong. */
    AST_EXPR_CHOOSE_EXPR, /* lhs = cond, mid = a, rhs = b */
    AST_EXPR_STMT,        /* GNU `({ ... })`: lhs is an AST_STMT_COMPOUND.
                             Its VALUE is the last item if that item is an
                             AST_STMT_EXPR, and `void` otherwise -- a
                             trailing declaration or a trailing `if` both
                             make it void, which gcc reports as "void value
                             not ignored". Measured, not assumed. */

    /* Statements (C11 6.8). */
    AST_STMT_COMPOUND, /* { items... } */
    AST_STMT_EXPR,     /* lhs ; */
    AST_STMT_NULL,     /* bare ; */
    AST_STMT_IF,       /* lhs ? body : rhs */
    AST_STMT_SWITCH,   /* switch (lhs) body */
    AST_STMT_WHILE,    /* while (lhs) body */
    AST_STMT_DO,       /* do body while (lhs) */
    AST_STMT_FOR,      /* for (lhs; mid; rhs) body */
    AST_STMT_GOTO,     /* goto name */
    AST_STMT_CONTINUE,
    AST_STMT_BREAK,
    AST_STMT_RETURN, /* return lhs (lhs may be NULL) */
    AST_STMT_LABEL,  /* name : body */
    AST_STMT_CASE,   /* case lhs : body, or GNU `case lhs ... rhs :` */
    AST_STMT_DEFAULT,
    AST_STMT_DECL, /* a declaration used as a block item; lhs is the decl */
    AST_STMT_ASM,  /* asm [quals] ( template [: out [: in [: clobbers]]] ) */

    AST_ERROR, /* poison; Sprint 11 owns recovery */
} AstKind;

/* One extended-asm operand. The constraint is kept as SPELLED and decoded
 * per target, because the letters are target vocabulary: `r` means the same
 * thing everywhere but `d` is rdx on x86-64 and a d-register on arm64. */
typedef struct AsmOperand {
    const char *constraint; /* the raw string, minus nothing */
    const char *name;       /* [symbolic] name, or NULL */
    struct AstNode *expr;   /* the C operand; an lvalue for outputs */
    Span span;
} AsmOperand;

/* Storage classes and function specifiers, as SPELLED. */
#define AST_SC_TYPEDEF 0x01
#define AST_SC_EXTERN 0x02
#define AST_SC_STATIC 0x04
#define AST_SC_AUTO 0x08
#define AST_SC_REGISTER 0x10
#define AST_SC_THREAD_LOCAL 0x20

#define AST_FS_INLINE 0x01
#define AST_FS_NORETURN 0x02

#define AST_QUAL_CONST 0x01
#define AST_QUAL_VOLATILE 0x02
#define AST_QUAL_RESTRICT 0x04
#define AST_QUAL_ATOMIC 0x08

/* The canonical type named by a specifier multiset (C11 6.7.2p2). */
typedef enum AstBaseType {
    ABT_NONE,    /* no type specifier: implicit int */
    ABT_VA_LIST, /* __builtin_va_list — sema synthesizes the SysV record
                    array type (Sprint 19) */
    ABT_VOID,
    ABT_CHAR,
    ABT_SCHAR,
    ABT_UCHAR,
    ABT_SHORT,
    ABT_USHORT,
    ABT_INT,
    ABT_UINT,
    ABT_LONG,
    ABT_ULONG,
    ABT_LLONG,
    ABT_ULLONG,
    ABT_FLOAT,
    ABT_DOUBLE,
    ABT_LDOUBLE,
    ABT_FLOAT32,
    ABT_FLOAT64,
    ABT_FLOAT32X,
    ABT_FLOAT64X,
    ABT_FLOAT128, /* _Float128 / __float128: IEEE binary128 on every target */
    ABT_BOOL,
    ABT_RECORD,  /* struct/union: `record` points at the AST_RECORD_DECL */
    ABT_ENUM,    /* `record` points at the AST_ENUM_DECL */
    ABT_TYPEDEF, /* `typedef_name` names it */
    /* GNU `typeof(x)`. Exactly ONE of typeof_expr / typeof_type is set:
     * the operand is either an EXPRESSION or a TYPE NAME, and the same
     * one-token lookahead that separates a cast from a call separates
     * them here. The expression is parsed UNEVALUATED -- `typeof(f())`
     * does not call f, measured. */
    ABT_TYPEOF,
    /* GNU `__auto_type`: the type comes from the INITIALIZER, so it is
     * resolved in sema where the initializer has been typed. Unlike
     * typeof it applies lvalue conversion, so `const int c;
     * __auto_type k = c;` gives a MUTABLE int -- measured, and the
     * opposite of what typeof does with the same operand. */
    ABT_AUTO_TYPE,
    ABT_ERROR, /* poisoned type-name: sema maps directly to TY_ERROR */
} AstBaseType;

typedef enum AstTypeKind {
    ATY_BASE, /* the specifier-soup result */
    ATY_PTR,
    ATY_ARRAY,
    ATY_FUNC,
} AstTypeKind;

typedef struct AstParam {
    AstType *type;
    const char *name; /* NULL for unnamed prototype params */
    Span span;
    CgfAttr *cgf_attrs; /* invalid on parameters; retained for sema's error */
} AstParam;

struct AstType {
    AstTypeKind kind;
    Span span;
    AstType *next; /* the type this one derives FROM (inside-out chain) */

    /* ATY_BASE */
    AstBaseType base;
    /* `_Atomic(type-name)` — the SPECIFIER form. The inner type-name is a
     * full abstract declarator (`_Atomic(int *)` is legal), and 6.7.2.4p3
     * forbids an array or function type INSIDE it — which only sema can
     * check, after the chain is resolved. Bare `_Atomic` (the qualifier
     * form) leaves this NULL and just sets AST_QUAL_ATOMIC. */
    AstType *atomic_inner;
    bool atomic_specifier;
    const char *typedef_name; /* ABT_TYPEDEF */
    /* ABT_TYPEOF: exactly one of these is non-NULL. */
    AstNode *typeof_expr;
    AstType *typeof_type;
    AstNode *record; /* ABT_RECORD / ABT_ENUM */
    u32 quals;

    /* ATY_PTR */
    u32 ptr_quals;
    /* GNU `aligned` written in the pointer qualifier list belongs to this
     * exact pointer layer: `int * aligned(16) *p` over-aligns `*p`, not p.
     * Keep the unevaluated expression until sema has a target and the shared
     * constant-expression evaluator. */
    AstNode *ptr_aligned_expr;
    bool ptr_aligned_bare;
    bool ptr_aligned_conflict;

    /* ATY_ARRAY: the SIZE EXPRESSION, never a computed value. */
    AstNode *array_size;
    bool array_static; /* `int p[static 3]` */
    bool array_star;   /* `int p[*]` (VM prototype) */
    u32 array_quals;   /* qualifiers inside the brackets */

    /* ATY_FUNC */
    AstParam *params;
    u32 nparams;
    bool is_variadic;
    bool is_kr_list;    /* identifier list, not a prototype */
    bool has_no_params; /* `f()` — unspecified, NOT (void) */
};

struct AstNode {
    AstKind kind;
    Span span;
    bool poisoned;
    /* AST_EXPR_TYPES_COMPATIBLE / AST_EXPR_CHOOSE_EXPR: sema's answer. */
    bool types_compatible;
    bool choose_taken;
    /* AST_EXPR_COND: the GNU `a ?: b` form, whose middle operand IS the
     * condition -- evaluated EXACTLY ONCE (measured both ways with a call
     * counter). `mid` stays NULL, and this flag says that is deliberate
     * rather than a poisoned parse, so every consumer can tell them apart. */
    bool cond_omits_mid;

    /* AST_DECL / AST_FUNC_DEF */
    const char *name;
    AstType *type;
    /* AST_EXPR_TYPES_COMPATIBLE: the SECOND type name. `type` holds the
     * first, so the pair rides the node the same way va_arg's does. */
    AstType *type2;
    u32 storage;        /* AST_SC_* */
    u32 func_specs;     /* AST_FS_* */
    AstNode *init;      /* initializer, or NULL */
    CgfAttr *cgf_attrs; /* ownership attributes, immutable/source-ordered */
    /* Implemented GNU attributes, merged from BOTH positions: a prefix
     * attribute binds to every declarator of the declaration, a suffix one
     * only to its own. */
    GnuDeclAttrs gnu;
    /* _Alignas: either an expression (`_Alignas(16)`) or a type-name
     * (`_Alignas(double)`). Only one is set. Constraints are sema's — the
     * parser records what was written. */
    AstNode *alignas_expr;
    AstType *alignas_type;
    struct Type *sem_alignas_type; /* resolved while declaration scope lives */
    bool has_alignas;
    AstNode *body;      /* AST_FUNC_DEF: compound statement (Sprint 10) */
    AstNode **kr_decls; /* AST_FUNC_DEF: K&R declaration list */
    u32 nkr_decls;

    /* AST_RECORD_DECL / AST_ENUM_DECL */
    bool is_union;
    bool is_definition;
    /* `packed` bound to THIS record definition. Only the parser can decide
     * that: gcc packs a record for a trailing attribute (`struct S {...}
     * __attr__((packed));`) and for one between the keyword and the tag, but
     * silently ignores a LEADING one, and by the time sema sees a GnuDeclAttrs
     * the three positions are indistinguishable. */
    bool packed;
    /* `may_alias` bound to this record definition. Like `packed`, position
     * matters: between `struct` and the tag, or after the closing brace,
     * changes the record type; a leading attribute before `struct` does not. */
    bool may_alias;
    /* `aligned(N)` bound to THIS record definition, by the same positional
     * rule as `packed` and measured the same way: a trailing attribute and one
     * between the keyword and the tag both align the record, a LEADING one gcc
     * ignores. Unlike packed, an attribute that lands on the DECLARATION
     * instead is meaningful there -- it aligns the object -- so nothing is
     * warned about, it simply stays where it is. */
    AstNode *record_aligned_expr;
    bool record_aligned_bare;
    /* `mode(M)` bound to an enum DEFINITION. An attribute after the closing
     * brace or between `enum` and its tag changes the tag's representation;
     * a leading or declarator-suffix attribute changes only that declaration.
     * The parser is the last layer that can preserve this GCC distinction. */
    u8 record_mode;    /* GnuMode; AST_ENUM_DECL only */
    const char *tag;   /* NULL when anonymous */
    AstNode **members; /* AST_DECL / AST_STATIC_ASSERT / nested records */
    u32 nmembers;
    AstNode *bitfield_width; /* on a member AST_DECL: the width EXPRESSION */
    bool is_bitfield;
    bool is_anon_member; /* C11 anonymous struct/union member */
    /* `struct S;` on its own ALWAYS introduces a new tag in the current
     * scope (6.7.2.3p7), while a USE like `struct S *p;` refers to a
     * visible one. Only the parser can tell them apart — by whether a
     * declarator followed — so it records the answer here. */
    bool is_forward_decl;

    /* AST_STATIC_ASSERT */
    AstNode *assert_expr;
    const Token *assert_msg;

    /* AST_INIT_LIST / AST_DESIGNATOR */
    AstNode **items;
    u32 nitems;
    AstNode **designators; /* on an item: its designator chain */
    u32 ndesignators;
    bool desig_is_field;
    const char *desig_field;
    AstNode *desig_index;
    /* GNU `[first ... last]` keeps both expressions until sema proves they
     * are nonnegative integer constant expressions.  Sema then expands the
     * inclusive range into ordinary concrete designators so every existing
     * current-object, override, static-image, and lowering path stays shared.
     * The cached value also keeps later consumers from folding a designator
     * more than once. */
    AstNode *desig_range_end;
    i64 desig_index_value;
    i64 desig_range_end_value;
    bool desig_bounds_checked;
    bool desig_bounds_valid;
    /* Expanded range items are shallow copies of one typed initializer.
     * Lowering keys runtime-value materialization on this origin so GNU's
     * side-effect-once rule survives the expansion. */
    AstNode *init_range_origin;

    /* Expressions */
    const Token *tok; /* literal/identifier token */
    u16 op;           /* PpPunct for unary/binary */
    AstNode *lhs, *rhs, *mid;
    AstNode **args;
    u32 nargs;
    bool is_postfix; /* AST_EXPR_UNARY: x++ rather than ++x */
    bool is_arrow;   /* AST_EXPR_MEMBER: -> rather than . */
    /* AST_EXPR_SIZEOF / _ALIGNOF / _CAST / _COMPOUND_LIT / AST_GENERIC_ASSOC
     * put the type-name in `type`. sizeof with a NULL type is the
     * expression form. */
    bool unevaluated; /* operand of sizeof/_Alignof, or a _Generic
                       * controlling expression: sema and lowering must
                       * never emit code for it (6.5.1.1p2, 6.5.3.4p2) */
    /* GNU __alignof__(expr) observes alignment attached to a declaration or
     * member, not merely the expression's Type. Sema records the exact
     * lvalue alignment when it differs from or refines the type -- including
     * a packed member LOWERING it; zero means "use the Type". Parenthesis
     * preserves it, while value-producing operators intentionally do not. */
    u64 sem_lvalue_align;
    bool sem_is_bitfield; /* resolved member expression; not addressable */
    /* AST_EXPR_STRING synthesized for C11 `__func__` or GNU
     * `__FUNCTION__` / `__PRETTY_FUNCTION__`. Its element type is const char,
     * unlike an ordinary string literal's char, while lowering can share the
     * literal path. */
    bool is_func_name;
    /* A compound literal's storage duration follows its SCOPE, not a
     * keyword: static at file scope, automatic (and block-lifetime!) at
     * block scope. Sprint 19 lowers it; Sprint 42 diagnoses the escaping
     * `&(struct S){0}` case. Recorded here because the parser is the only
     * pass that still knows which scope it was written in. */
    bool is_static_storage;
    /* `__extension__` suppresses pedantic diagnostics over its declaration
     * or expression operand. Most such warnings are emitted while parsing,
     * but compound-literal array initialization is recognized only in sema,
     * so preserve the parser's otherwise-transient suppression here. */
    bool suppress_pedantic;

    /* --- filled in by SEMA (Sprint 13 onward), NULL/false before it runs.
     * Implicit conversions are MATERIALIZED as AST_EXPR_CAST nodes with
     * `implicit` set, never left for a later pass to re-derive: IR
     * lowering and -Wconversion both read the tree, and a conversion that
     * exists only as a rule gets applied twice or not at all. */
    struct Type *sem_type;
    struct Symbol *sym; /* AST_EXPR_IDENT: the declaration it resolved to */
    bool is_lvalue;
    bool implicit; /* AST_EXPR_CAST: inserted by sema, not written */
    /* Constant evaluation is intentionally reusable (sema validation,
     * initializer images, then lowering). Range warnings belong to the
     * source literal and must be emitted only once across those clients. */
    bool fp_range_diagnosed;
    /* AST_FUNC_DEF: the parameter symbols, in declaration order (NULL for
     * an unnamed slot). Sema declares them into the function scope and
     * that scope is popped when the body ends — this array is how Sprint
     * 18's lowering still reaches them to bind IR parameters. */
    struct Symbol **param_syms;
    /* The corresponding parameter declaration types BEFORE 6.7.6.3p7/p8
     * array/function adjustment. Entry-time VLA bound evaluation needs the
     * discarded outer array layer; body uses still read param_syms' adjusted
     * pointer type. */
    struct Type **param_decl_types;
    u32 nparam_syms;

    /* AST_TRANSLATION_UNIT */
    AstNode **decls;
    u32 ndecls;

    /* AST_STMT_ASM. `asm_basic` is the whole difference between the two
     * dialects and it is decided by PUNCTUATION, not by operand counts: a
     * template with no colon after it is BASIC and its `%` characters pass
     * through verbatim, while `asm("..." : : )` with three empty lists is
     * EXTENDED and processes them. Measured against gcc -- `asm("%%eax")`
     * emits `%%eax` and `asm("%%eax" : : )` emits `%eax`. */
    const char *asm_tmpl;
    AsmOperand *asm_ops; /* outputs first, then inputs: gcc's %0 numbering */
    u32 asm_nops;
    u32 asm_noutputs;
    const char **asm_clobbers;
    u32 asm_nclobbers;
    bool asm_volatile;
    bool asm_basic;
};

AstNode *ast_new(Arena *a, AstKind k, Span sp);
AstType *ast_type_new(Arena *a, AstTypeKind k, Span sp);
/* Appends `tail` at the END of the chain rooted at `head` (declarators are
 * built inside-out, so the base type arrives last). */
AstType *ast_type_chain(AstType *head, AstType *tail);

/* Human-readable declarator rendering for --dump-ast goldens:
 * "array 3 of ptr to func(void) ret ptr to array 5 of int". */
void ast_type_render(const AstType *t, Buf *out);
const char *ast_base_type_name(AstBaseType b);
const char *ast_punct_name(u16 punct);

/* Renders an expression FULLY PARENTHESIZED. This is the precedence
 * matrix's oracle: `a+b*c` must print as `(a + (b * c))`, so a wrong
 * binding power cannot hide behind a flat dump. */
void ast_expr_render(const AstNode *e, Buf *out);

/* Rendering a node's SEMANTIC type needs sema's vocabulary, which ast.c
 * must not depend on — so sema installs a renderer and ast.c calls back.
 * One-way include arrow preserved. */
typedef void (*AstSemTypeRenderer)(const AstNode *n, Buf *out);
void ast_set_sem_type_renderer(AstSemTypeRenderer fn);
void ast_sem_type_render(const AstNode *n, Buf *out);

#endif
