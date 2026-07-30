#ifndef CGF_SEMA_H
#define CGF_SEMA_H

#include "diag.h"
#include "lex/lex.h"
#include "parse/ast.h"
#include "target.h"
#include "util/arena.h"
#include "util/base.h"
#include "util/intern.h"
#include "util/softfp.h"

/* The semantic core: the Type graph, the scope stack over C's four
 * namespaces, linkage, and redeclaration merging.
 *
 * NOTHING here needs a byte size. Sizes and layout are Sprint 14,
 * conversions Sprint 13, constant evaluation Sprint 15; every path that
 * would need one hard-errors naming its sprint rather than guessing. */

typedef enum {
    TY_VOID,
    TY_BOOL,
    TY_CHAR,
    TY_SCHAR,
    TY_UCHAR,
    TY_SHORT,
    TY_USHORT,
    TY_INT,
    TY_UINT,
    TY_LONG,
    TY_ULONG,
    TY_LLONG,
    TY_ULLONG,
    TY_FLOAT,
    TY_DOUBLE,
    TY_LDOUBLE,
    TY_PTR,
    TY_ARRAY,
    TY_FUNC,
    TY_STRUCT,
    TY_UNION,
    TY_ENUM,
    TY_ERROR /* poisoned: never diagnosed about again (Sprint 11 contract) */
} TypeKind;

#define CGF_QUAL_CONST 0x1u
#define CGF_QUAL_VOLATILE 0x2u
#define CGF_QUAL_RESTRICT 0x4u
#define CGF_QUAL_ATOMIC 0x8u

typedef struct Type Type;
typedef struct TagDecl TagDecl;
typedef struct Member Member;
typedef struct Symbol Symbol;

struct Member {
    const char *name; /* interned; NULL for an anonymous member */
    Type *type;
    AstNode *bitfield_width; /* the EXPRESSION; Sprint 15 evaluates it */
    bool is_bitfield;
    Span span;

    /* Filled by layout_record (Sprint 14). `offset` is in BYTES from the
     * start of the record; for a bitfield, `bit_offset` is the bit
     * position within the whole record and `bit_width` its declared
     * width. `container_size` is the declared type's size — AAPCS64
     * mandates that a volatile bitfield access use exactly the container
     * width, so Sprints 20 and 48 read it rather than re-deriving it. */
    u64 offset;
    u64 bit_offset;
    u32 bit_width;
    u64 container_size;
    u64 align_override; /* _Alignas on the member, 0 = none */
    bool laid_out;

    Member *next;
};

struct TagDecl {
    const char *name; /* interned; NULL when anonymous */
    /* Layout is memoized per tag: it is a pure function of the members
     * and the target, and a deep struct tree would otherwise be walked
     * once per sizeof. `laid_out_for` guards against a memo computed for
     * a DIFFERENT target leaking into a cross-compile. */
    bool laid_out;
    TargetKind laid_out_for;
    u64 size;
    u64 align;
    TypeKind kind; /* TY_STRUCT / TY_UNION / TY_ENUM */
    bool complete;
    Member *members;
    u32 nmembers;
    Type *enum_underlying; /* TY_ENUM only, chosen per gcc's ladder */
    u64 align_override;    /* _Alignas on the record, 0 = none */
    Span span;
    Type *type; /* the one Type node that names this tag */
};

struct Type {
    TypeKind kind;
    unsigned quals;

    Type *base; /* TY_PTR pointee / TY_ARRAY element / TY_FUNC return */

    /* TY_ARRAY. The SIZE is only present once Sprint 15 can evaluate the
     * bound; `has_size` false means an incomplete array type. */
    bool has_size;
    u64 size;
    bool is_vla;
    AstNode *size_expr;

    /* TY_FUNC. `has_proto` false is the K&R / `f()` unspecified form,
     * which is compatible with anything on the parameter side. */
    Type **params;
    u32 nparams;
    bool variadic;
    bool has_proto;

    TagDecl *tag; /* TY_STRUCT / TY_UNION / TY_ENUM */
};

/* C11 6.2.3: FOUR namespaces. Ordinary holds objects, functions, typedefs
 * and enum CONSTANTS in one shared space; tags hold struct, union AND enum
 * together (so `struct T` and `enum T` collide in one scope); members are
 * per-struct; labels are function-scoped and live on the parser side. */
typedef enum { NS_ORDINARY, NS_TAG, NS_LABEL } Namespace;

typedef enum { LINK_NONE, LINK_INTERNAL, LINK_EXTERNAL } Linkage;

typedef enum {
    SYM_VAR,
    SYM_FUNC,
    SYM_TYPEDEF,
    SYM_ENUM_CONST,
    SYM_TAG
} SymKind;

struct Symbol {
    const char *name; /* interned: compared by pointer */
    SymKind kind;
    Namespace ns;
    Type *type;
    Linkage linkage;
    Span span;

    bool tentative; /* file-scope object, no initializer (6.9.2p2) */
    bool defined;   /* has an initializer, or is a function definition */
    bool is_param;
    i64 enum_value; /* SYM_ENUM_CONST */
    TagDecl *tag;   /* SYM_TAG */

    Symbol *next; /* intrusive chain within one scope, newest first */
};

typedef enum {
    SCOPE_FILE,
    SCOPE_BLOCK,
    SCOPE_PROTO, /* a parameter list: everything declared here dies at ')' */
    SCOPE_FUNC
} ScopeKind;

typedef struct Scope Scope;
struct Scope {
    ScopeKind kind;
    /* Intrusive lists, newest first, so a shadowing declaration is simply
     * found first. Arena-allocated — src/sema/ makes no heap allocation of
     * its own, which is what keeps the whole pass leak-free by
     * construction rather than by discipline. */
    Symbol *ordinary;
    Symbol *tags;
    Scope *parent;
};

typedef struct Sema {
    Arena *arena;
    DiagCtx *dc;
    Interner *interner;
    const LangOpts *lang;
    TargetSpec target;
    Scope *scope;
    Scope *file_scope;
    u32 nerrors;
    bool dump; /* -fdump-sema */
} Sema;

/* --- types --------------------------------------------------------------- */

/* Interned: one canonical node per basic kind, so basics compare by
 * pointer. Derived types (pointer/array/function/tagged) are allocated per
 * use and compared STRUCTURALLY — globally uniquing them is a trap,
 * because the same tag completes differently per translation unit and
 * qualifier combinations multiply nodes regardless. */
Type *type_basic(TypeKind k);
Type *type_qualify(Arena *ar, const Type *t, unsigned quals);
Type *type_ptr(Arena *ar, Type *pointee);
Type *type_array(Arena *ar, Type *elem);
Type *type_func(Arena *ar, Type *ret);
Type *type_tag(Arena *ar, TagDecl *tag);

bool type_is_basic(const Type *t);
bool type_is_integer(const Type *t);
bool type_is_arithmetic(const Type *t);
bool type_is_complete(const Type *t);
/* C11 6.2.7 compatibility. Basic types are compatible only when their
 * kinds are IDENTICAL: `int` and `long` are incompatible even where they
 * have the same width, and `char`/`signed char`/`unsigned char` are three
 * distinct types (6.2.5p15) even where char is signed. */
bool type_compatible(const Type *a, const Type *b);
/* 6.2.7p3. `int a[]` + `int a[10]` -> `int[10]`; `int f()` + a prototype
 * -> the prototype. Requires the two to be compatible. */
Type *type_composite(Arena *ar, Type *a, Type *b);
char *type_to_str(Arena *ar, const Type *t);

/* Cross-TU struct compatibility is member-wise (6.2.7p1) and only
 * observable at link time; Sprint 57 owns it. */
bool type_compatible_cross_tu(Sema *s, const Type *a, const Type *b);

/* --- constant expressions (Sprint 15) ------------------------------------ */

/* Which softfloat format the target uses for a floating type. `long
 * double` is the cross-target trap: x87 80-bit on x86-64, IEEE binary128
 * on arm64-linux, plain double on arm64-macos. */
SfFormat constexpr_format_of(Sema *s, const Type *t);
/* Parses a C floating-constant SPELLING correctly-rounded into `f`. The
 * literal grammar lives on this side because softfp.c must stay
 * library-clean for Sprint 49's runtime. */
Sf constexpr_parse_float(const char *spelling, SfFormat f, SfStatus *st);
/* The value of a float literal token, with gcc's range diagnostics. */
Sf constexpr_float_literal(Sema *s, AstNode *e);

/* --- layout (Sprint 14) -------------------------------------------------- */

typedef struct {
    u64 size;
    u64 align;
} TypeLayout;

/* Size and alignment of a COMPLETE type. On an incomplete one this is a
 * programming error rather than a 0 size — callers check completeness and
 * diagnose at their own site, where they can say which construct demanded
 * the size. */
TypeLayout layout_of(Sema *s, Type *t);
bool layout_is_complete_for_size(const Type *t);
/* Lays out a record, filling every Member's offset/bit_offset/bit_width.
 * Idempotent and memoized on the TagDecl. */
void layout_record(Sema *s, Type *rec);
u64 layout_offsetof(Sema *s, Type *rec, const Member *m);

/* SysV x86-64 parameter classification (psABI 3.2.3). Consumed by Sprint
 * 19's call lowering and Sprint 23's x86 calls. */
typedef enum {
    ABI_NO_CLASS,
    ABI_INTEGER,
    ABI_SSE,
    ABI_SSEUP,
    ABI_X87,
    ABI_X87UP,
    ABI_COMPLEX_X87,
    ABI_MEMORY
} AbiClass;

/* Returns the eightbyte count (1 or 2), or -1 for MEMORY. */
int layout_classify_sysv(Sema *s, Type *t, AbiClass out[2]);

/* AAPCS64 homogeneous float aggregate: up to 4 members, all the same
 * floating type. The PREDICATE lands now so Sprint 19 can shape calls
 * target-parameterized once; the codegen that consumes it is Sprint 48. */
bool layout_is_hfa(Sema *s, Type *t, Type **base, int *count);

/* -fdump-layout: one line per record and member (offset, bit offset,
 * width, size, align). Sprint 19 reuses this format. */
void layout_dump(Sema *s, FILE *f);

/* --- conversions (6.3) --------------------------------------------------- */

/* Which construct is doing the assigning. It exists so ONE function
 * produces gcc's four different sentences ("assignment to X from Y",
 * "passing argument N of 'f'", ...) rather than four near-copies drifting
 * apart. */
typedef enum { ACTX_ASSIGN, ACTX_INIT, ACTX_ARG, ACTX_RETURN } AssignCtxKind;

typedef struct {
    AssignCtxKind kind;
    u32 arg_index;      /* ACTX_ARG: 1-based, as gcc numbers them */
    const char *callee; /* ACTX_ARG: the function's name, if known */
} AssignCtx;

/* Implicit conversions are MATERIALIZED as AST_EXPR_CAST nodes with
 * `implicit` set. Later passes read the tree; none re-derives the rules. */
AstNode *conv_cast(Sema *s, AstNode *e, Type *to);
AstNode *conv_lvalue(Sema *s, AstNode *e);  /* drops TOP-level quals */
AstNode *conv_decay(Sema *s, AstNode *e);   /* array->ptr, func->ptr */
AstNode *conv_promote(Sema *s, AstNode *e); /* integer promotions */
AstNode *conv_to_bool(Sema *s, AstNode *e); /* `!= 0`, never truncation */
Type *conv_uac(Sema *s, AstNode **a, AstNode **b);
/* The type-level halves, so the unit suite can build a truth table
 * without constructing expressions. */
Type *conv_promote_type(Sema *s, Type *t);
Type *conv_uac_type(Sema *s, Type *a, Type *b);
Type *conv_strip_quals(Sema *s, const Type *t);
int conv_rank(const Type *t);
bool conv_is_signed(Sema *s, const Type *t);
u32 conv_int_bits(Sema *s, const Type *t);
/* 6.3.2.3p3. `(char *)0` is a null POINTER but not a null pointer
 * CONSTANT — the difference decides `?:` typing and varargs sentinels. */
bool conv_is_npc(Sema *s, const AstNode *e);
bool conv_assignable(Sema *s, Type *lhs, AstNode **rhs_slot, AssignCtx ctx);

/* --- expressions --------------------------------------------------------- */

/* Types an expression in place: fills sem_type/is_lvalue and rewrites
 * children with the implicit conversions the operator demands. Returns
 * the (possibly wrapped) node. */
AstNode *sema_expr(Sema *s, AstNode *e);

/* --- scopes -------------------------------------------------------------- */

void sema_init(Sema *s, Arena *ar, DiagCtx *dc, Interner *in,
               const LangOpts *lang, TargetSpec target);
Scope *scope_push(Sema *s, ScopeKind k);
void scope_pop(Sema *s);
Symbol *scope_lookup(Scope *sc, const char *name, Namespace ns);
/* Lookup restricted to ONE scope — the redeclaration check, and what
 * separates "redeclared here" from "shadows an outer declaration". */
Symbol *scope_lookup_local(Scope *sc, const char *name, Namespace ns);
Symbol *scope_declare(Sema *s, Symbol *sym);
Symbol *sym_new(Sema *s, const char *name, SymKind kind, Namespace ns,
                Type *type, Span span);

/* --- the pass ------------------------------------------------------------ */

void sema_run(Sema *s, AstNode *tu);
/* -fdump-sema: file-scope symbols with their resolved types and linkage,
 * in DECLARATION order. */
void sema_dump(Sema *s, FILE *f);
/* Teaches ast.c to render semantic types; call once before any dump. */
void sema_install_renderer(void);
/* Converts an AST type chain to a semantic Type. Exposed for unit tests. */
Type *sema_type_from_ast(Sema *s, const AstType *at, Span span);

/* Every deferred path routes through here so the grep in the sprint's DoD
 * finds them all: the message ALWAYS names the sprint that lands it. */
void sema_unimplemented(Sema *s, Span span, const char *what, int sprint);

#endif
