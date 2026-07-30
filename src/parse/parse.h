#ifndef CGF_PARSE_H
#define CGF_PARSE_H

#include "lex/lex.h"
#include "parse/ast.h"
#include "pp/pp.h"

/* THE typedef ambiguity is resolved AT PARSE TIME by an in-parser scope
 * stack mapping identifier -> typedef-ness (chibicc's shape: a linked list
 * of scopes whose entries carry a type_def marker). `T * p;` is a
 * declaration iff T is a visible typedef; otherwise it is multiplication.
 *
 * Declaration point matters (C11 6.2.1p7): a declarator's name enters
 * scope when ITS declarator completes, not at the `;`. That is why
 * `typedef int T; { T T; T x; }` declares a VARIABLE T (the specifier `T`
 * still saw the typedef), and the following `T x;` is an error. */

typedef struct ScopeEntry {
    const char *name; /* interned */
    bool is_typedef;
    struct ScopeEntry *next;
} ScopeEntry;

typedef struct ParseScope {
    ScopeEntry *ordinary; /* ordinary identifiers (vars, typedefs, enums) */
    ScopeEntry *tags;     /* struct/union/enum tags — separate namespace */
    struct ParseScope *parent;
} ParseScope;

/* Labels live in their OWN namespace and have FUNCTION scope, not block
 * scope: `foo: foo = 1; goto foo;` is legal, and a `goto` may precede its
 * label. So they cannot ride the ordinary scope stack — uses and
 * definitions are collected per function and reconciled at the closing
 * brace, which is the first moment "undefined label" is knowable. */
typedef struct LabelEntry {
    const char *name; /* interned */
    bool defined;
    Span first_use; /* for the undefined-label diagnostic */
    struct LabelEntry *next;
} LabelEntry;

typedef struct Parser {
    const Token *toks;
    u32 ntoks;
    u32 pos;
    Preprocessor *pp; /* for pp_diag_at: front-end diagnostics inherit
                         macro-expansion backtraces */
    DiagCtx *dc;
    Arena *arena;
    ParseScope *scope;
    const LangOpts *lang;
    u32 nerrors;

    u32 scope_depth;   /* 0 = file scope; drives compound-literal storage */
    u32 bracket_depth; /* against CGF_MAX_BRACKET_DEPTH */
    /* Panic mode: set when a construct is poisoned, cleared by parse_sync.
     * While set, parse_error counts instead of emitting — everything in the
     * window is a consequence of the error already reported. */
    bool recovering;
    /* Names diagnosed once by the unknown-type heuristic. They are also
     * declared as typedefs in FILE scope, so later uses simply parse; this
     * set exists so the same name is never diagnosed twice. */
    ScopeEntry *unknown_types;
    u32 unevaluated;    /* >0 inside sizeof/_Alignof/_Generic controllers */
    u32 switch_depth;   /* `case`/`default` outside a switch is an error */
    u32 loop_depth;     /* `continue` outside a loop is an error */
    u32 break_depth;    /* loops AND switches both accept `break` */
    LabelEntry *labels; /* reset per function definition */
    bool in_func_body;
} Parser;

void parse_init(Parser *p, const TokenList *tl, Preprocessor *pp, DiagCtx *dc,
                Arena *arena, const LangOpts *lang);

void parse_scope_enter(Parser *p);
void parse_scope_leave(Parser *p);
void parse_scope_declare(Parser *p, const char *name, bool is_typedef);
bool parse_is_typedef_name(Parser *p, const char *name);

/* C11 5.2.4.1 requires 63 levels of parenthesized expressions; this bound
 * exists only so pathological input reports an error instead of running
 * recursive descent out of stack. */
#define CGF_MAX_BRACKET_DEPTH 256

/* Panic-mode recovery (src/parse/recover.c). See that file for the
 * PROGRESS and SCOPE BALANCE invariants — both are load-bearing. */
typedef enum SyncSet {
    SYNC_DECL,   /* restart at a declaration */
    SYNC_STMT,   /* restart at a statement or declaration */
    SYNC_MEMBER, /* inside a struct/union body */
    SYNC_PAREN   /* stop at the closer of the enclosing bracket */
} SyncSet;

void parse_sync(Parser *p, SyncSet set);
AstNode *parse_error_node(Parser *p, Span sp);
/* Propagates poison from a child at CONSTRUCTION time, so no pass ever has
 * to re-walk looking for AST_ERROR. Returns `n`. */
AstNode *parse_poison_from(AstNode *n, const AstNode *child);
bool parse_depth_enter(Parser *p, const Token *at);
void parse_depth_leave(Parser *p);
/* Expects a closing bracket, attaching a "to match this '('" note at the
 * opener when it is missing. */
void parse_expect_close(Parser *p, PpPunct closer, Span opener,
                        const char *what);
/* Reports a missing token at the position just PAST the previous token —
 * where the fix belongs — rather than at the unexpected one. */
void parse_error_after_prev(Parser *p, PpPunct expected, const char *what);

AstNode *parse_translation_unit(Parser *p);
AstNode *parse_declaration(Parser *p, bool allow_func_def);

/* The C11 6.5 expression grammar. `parse_expr` includes the comma
 * operator; `parse_assign_expr` does NOT, which is why argument lists and
 * initializer elements call the latter — `f(a, b)` has two arguments, not
 * one comma expression. `parse_cond_expr` is the constant-expression
 * entry point (6.6). */
AstNode *parse_expr(Parser *p);
AstNode *parse_assign_expr(Parser *p);
AstNode *parse_cond_expr(Parser *p);

/* Statements. `parse_compound_stmt` does NOT push a scope — the caller
 * decides, because a function body's outermost block SHARES the parameter
 * scope (6.2.1p4) while every other block gets its own. */
AstNode *parse_stmt(Parser *p);
AstNode *parse_compound_stmt(Parser *p);
AstNode *parse_func_body(Parser *p);

/* A type-name (6.7.7): specifier list + abstract declarator. Shared by
 * casts, sizeof, _Alignof, compound literals, and _Generic associations. */
AstType *parse_type_name(Parser *p);
/* True if the current token opens a type-name — the cast-vs-parenthesized
 * -expression decision, and the one place the typedef table is consulted
 * from the expression grammar. */
bool parse_at_type_name(Parser *p);
AstNode *parse_braced_initializer(Parser *p);

/* Token helpers shared with the (Sprint 10) statement parser. */
const Token *parse_peek(Parser *p);
const Token *parse_peek_n(Parser *p, u32 n);
bool parse_at_punct(Parser *p, PpPunct punct);
bool parse_at_kw(Parser *p, Keyword kw);
bool parse_eat_punct(Parser *p, PpPunct punct);
bool parse_eat_kw(Parser *p, Keyword kw);
void parse_expect_punct(Parser *p, PpPunct punct, const char *what);
void parse_error(Parser *p, const Token *at, const char *fmt, ...);
/* True if the current token can begin a declaration specifier list (the
 * decision that separates declarations from expression statements). */
bool parse_at_decl_specs(Parser *p);
/* True for the `__builtin_` reserved prefix: those names are the
 * compiler's own, and every one of them defers to Sprint 28. */
bool parse_is_builtin_name(const char *name);
/* True if the current identifier should be treated as an unknown TYPE name
 * rather than an expression — see the heuristic's comment in decl.c. */
bool parse_at_unknown_type(Parser *p);

#endif
