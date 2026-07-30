#ifndef CGF_SEMA_H
#define CGF_SEMA_H

#include "diag.h"
#include "lex/lex.h"
#include "parse/ast.h"
#include "target.h"
#include "util/arena.h"
#include "util/base.h"
#include "util/intern.h"

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
    Member *next;
};

struct TagDecl {
    const char *name; /* interned; NULL when anonymous */
    TypeKind kind;    /* TY_STRUCT / TY_UNION / TY_ENUM */
    bool complete;
    Member *members;
    u32 nmembers;
    Type *enum_underlying; /* TY_ENUM only, chosen per gcc's ladder */
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
/* Converts an AST type chain to a semantic Type. Exposed for unit tests. */
Type *sema_type_from_ast(Sema *s, const AstType *at, Span span);

/* Every deferred path routes through here so the grep in the sprint's DoD
 * finds them all: the message ALWAYS names the sprint that lands it. */
void sema_unimplemented(Sema *s, Span span, const char *what, int sprint);

#endif
