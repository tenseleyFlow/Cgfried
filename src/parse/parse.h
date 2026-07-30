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
} Parser;

void parse_init(Parser *p, const TokenList *tl, Preprocessor *pp, DiagCtx *dc,
                Arena *arena, const LangOpts *lang);

void parse_scope_enter(Parser *p);
void parse_scope_leave(Parser *p);
void parse_scope_declare(Parser *p, const char *name, bool is_typedef);
bool parse_is_typedef_name(Parser *p, const char *name);

AstNode *parse_translation_unit(Parser *p);
AstNode *parse_declaration(Parser *p, bool allow_func_def);

/* Sprint 10 replaces this with the full expression grammar. Today it
 * parses the constant-expression subset that declarations need: enum
 * values, bitfield widths, array sizes, initializers. */
AstNode *parse_assign_expr(Parser *p);
AstNode *parse_cond_expr(Parser *p);

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

#endif
