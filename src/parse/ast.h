#ifndef CGF_AST_H
#define CGF_AST_H

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

    /* Expression kinds. Sprint 10 completes the grammar; the subset here
     * exists because constant contexts (enum values, bitfield widths,
     * array sizes) need SOMETHING to store. */
    AST_EXPR_INT,
    AST_EXPR_FLOAT,
    AST_EXPR_CHAR,
    AST_EXPR_STRING,
    AST_EXPR_IDENT,
    AST_EXPR_UNARY,
    AST_EXPR_BINARY,
    AST_EXPR_COND,
    AST_EXPR_CALL,
    AST_EXPR_PAREN,

    AST_ERROR, /* poison; Sprint 11 owns recovery */
} AstKind;

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
    ABT_NONE, /* no type specifier: implicit int */
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
    ABT_BOOL,
    ABT_RECORD,  /* struct/union: `record` points at the AST_RECORD_DECL */
    ABT_ENUM,    /* `record` points at the AST_ENUM_DECL */
    ABT_TYPEDEF, /* `typedef_name` names it */
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
} AstParam;

struct AstType {
    AstTypeKind kind;
    Span span;
    AstType *next; /* the type this one derives FROM (inside-out chain) */

    /* ATY_BASE */
    AstBaseType base;
    const char *typedef_name; /* ABT_TYPEDEF */
    AstNode *record;          /* ABT_RECORD / ABT_ENUM */
    u32 quals;

    /* ATY_PTR */
    u32 ptr_quals;

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

    /* AST_DECL / AST_FUNC_DEF */
    const char *name;
    AstType *type;
    u32 storage;        /* AST_SC_* */
    u32 func_specs;     /* AST_FS_* */
    AstNode *init;      /* initializer, or NULL */
    AstNode *body;      /* AST_FUNC_DEF: compound statement (Sprint 10) */
    AstNode **kr_decls; /* AST_FUNC_DEF: K&R declaration list */
    u32 nkr_decls;

    /* AST_RECORD_DECL / AST_ENUM_DECL */
    bool is_union;
    bool is_definition;
    const char *tag;   /* NULL when anonymous */
    AstNode **members; /* AST_DECL / AST_STATIC_ASSERT / nested records */
    u32 nmembers;
    AstNode *bitfield_width; /* on a member AST_DECL: the width EXPRESSION */
    bool is_bitfield;
    bool is_anon_member; /* C11 anonymous struct/union member */

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

    /* Expressions */
    const Token *tok; /* literal/identifier token */
    u16 op;           /* PpPunct for unary/binary */
    AstNode *lhs, *rhs, *mid;
    AstNode **args;
    u32 nargs;

    /* AST_TRANSLATION_UNIT */
    AstNode **decls;
    u32 ndecls;
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

#endif
