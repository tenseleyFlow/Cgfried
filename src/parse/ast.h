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
    AST_STMT_CASE,   /* case lhs : body */
    AST_STMT_DEFAULT,
    AST_STMT_DECL, /* a declaration used as a block item; lhs is the decl */

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
    /* A compound literal's storage duration follows its SCOPE, not a
     * keyword: static at file scope, automatic (and block-lifetime!) at
     * block scope. Sprint 19 lowers it; Sprint 42 diagnoses the escaping
     * `&(struct S){0}` case. Recorded here because the parser is the only
     * pass that still knows which scope it was written in. */
    bool is_static_storage;

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
const char *ast_punct_name(u16 punct);

/* Renders an expression FULLY PARENTHESIZED. This is the precedence
 * matrix's oracle: `a+b*c` must print as `(a + (b * c))`, so a wrong
 * binding power cannot hide behind a flat dump. */
void ast_expr_render(const AstNode *e, Buf *out);

#endif
