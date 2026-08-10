#include <stdarg.h>
#include <string.h>

#include "parse/parse.h"
#include "util/dlev.h"
#include "warn/warn.h"

/* Declarations: specifier soup, the recursive declarator grammar,
 * struct/union/enum, and initializer SYNTAX. Nothing here type-checks —
 * Sprint 12 onward owns meaning. */

VEC_DECL(NodeVec, AstNode *);
VEC_DECL(ParamVec, AstParam);

/* --- token helpers ------------------------------------------------------ */

const Token *parse_peek(Parser *p)
{
    return &p->toks[p->pos < p->ntoks ? p->pos : p->ntoks - 1];
}

const Token *parse_peek_n(Parser *p, u32 n)
{
    u32 i = p->pos + n;

    return &p->toks[i < p->ntoks ? i : p->ntoks - 1];
}

bool parse_at_punct(Parser *p, PpPunct punct)
{
    const Token *t = parse_peek(p);

    return t->kind == TOK_PUNCT && t->punct == punct;
}

bool parse_at_kw(Parser *p, Keyword kw)
{
    const Token *t = parse_peek(p);

    return t->kind == TOK_KEYWORD && t->kw == kw;
}

bool parse_eat_punct(Parser *p, PpPunct punct)
{
    if (!parse_at_punct(p, punct))
        return false;
    p->pos++;
    return true;
}

bool parse_eat_kw(Parser *p, Keyword kw)
{
    if (!parse_at_kw(p, kw))
        return false;
    p->pos++;
    return true;
}

void parse_error(Parser *p, const Token *at, const char *fmt, ...)
{
    va_list ap;
    char msg[512];

    /* Panic mode: between poisoning a construct and synchronizing, every
     * further complaint is a consequence of the one already reported.
     * Counting instead of emitting is what makes "one mistake, one
     * diagnostic" true, and diag_suppressed_count is how tests tell that
     * apart from an error that never happened. */
    if (p->recovering) {
        p->nerrors++;
        diag_note_suppressed(p->dc);
        return;
    }

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    p->nerrors++;
    diag_emit(p->dc, DIAG_ERROR, at->span, "%s", msg);
}

/* The span just PAST the previous token: where a missing ';' or ')' should
 * be inserted. clang points here; gcc 8 points at the following token and
 * says "before '}' token", which sends the reader to the wrong line when
 * the next token is on the next line. We follow clang deliberately. */
static Span span_after_prev(Parser *p)
{
    const Token *prev;
    Span sp;

    if (p->pos == 0)
        return parse_peek(p)->span;
    prev = &p->toks[p->pos - 1];
    /* Clamped in diag, which is where the file lengths live: a token
     * spelled across line splices ends on a later physical line than its
     * span names, so col + len can overrun. */
    sp = diag_point_after(p->dc, prev->span);
    return sp;
}

void parse_error_after_prev(Parser *p, PpPunct expected, const char *what)
{
    Span sp = span_after_prev(p);
    Span fix = sp;
    const char *name = ast_punct_name((u16)expected);

    if (p->recovering) {
        p->nerrors++;
        diag_note_suppressed(p->dc);
        return;
    }
    p->nerrors++;
    /* Keep the primary caret visible, but describe the edit as a zero-width
     * insertion. GCC's parseable-fixits format distinguishes those ranges. */
    fix.len = 0;
    diag_emit_fixit(p->dc, DIAG_ERROR, sp, fix, name, "expected '%s'%s%s", name,
                    what ? " " : "", what ? what : "");
}

static const char *tok_desc(const Token *t)
{
    switch ((TokenKind)t->kind) {
    case TOK_EOF:
        return "end of file";
    default:
        return t->spelling;
    }
}

void parse_expect_punct(Parser *p, PpPunct punct, const char *what)
{
    if (parse_eat_punct(p, punct))
        return;
    /* Closers and ';' are MISSING-token errors: the useful position is
     * where the token belongs, not where the parser noticed. Everything
     * else names what was actually found, which is the useful half there. */
    if (punct == PUNCT_SEMI || punct == PUNCT_RPAREN ||
        punct == PUNCT_RBRACKET || punct == PUNCT_RBRACE) {
        parse_error_after_prev(p, punct, what);
        return;
    }
    parse_error(p, parse_peek(p), "expected '%s'%s%s but found '%s'",
                ast_punct_name((u16)punct), what ? " " : "", what ? what : "",
                tok_desc(parse_peek(p)));
}

/* --- scope stack -------------------------------------------------------- */

void parse_scope_enter(Parser *p)
{
    ParseScope *s =
        arena_alloc(p->arena, sizeof(ParseScope), _Alignof(ParseScope));

    memset(s, 0, sizeof(*s));
    s->parent = p->scope;
    p->scope = s;
}

void parse_scope_leave(Parser *p)
{
    if (!p->scope)
        CGF_ICE("parse_scope_leave: scope underflow");
    p->scope = p->scope->parent;
}

void parse_scope_declare(Parser *p, const char *name, bool is_typedef)
{
    ScopeEntry *e;

    if (!name || !p->scope)
        return;
    e = arena_alloc(p->arena, sizeof(ScopeEntry), _Alignof(ScopeEntry));
    e->name = name;
    e->is_typedef = is_typedef;
    /* Prepended, so a redeclaration in the SAME scope shadows the earlier
     * one — which is what makes `typedef int T; { T T; }` work. */
    e->next = p->scope->ordinary;
    p->scope->ordinary = e;
}

bool parse_is_typedef_name(Parser *p, const char *name)
{
    ParseScope *s;

    for (s = p->scope; s; s = s->parent) {
        ScopeEntry *e;
        for (e = s->ordinary; e; e = e->next)
            if (e->name == name) /* interned: pointer compare */
                return e->is_typedef;
    }
    return false;
}

static void scope_declare_tag(Parser *p, const char *name)
{
    ScopeEntry *e;

    if (!name || !p->scope)
        return;
    e = arena_alloc(p->arena, sizeof(ScopeEntry), _Alignof(ScopeEntry));
    e->name = name;
    e->is_typedef = false;
    e->next = p->scope->tags;
    p->scope->tags = e;
}

/* --- declaration specifiers -------------------------------------------- */

typedef struct {
    int n_void, n_char, n_short, n_int, n_long, n_float, n_double;
    int n_signed, n_unsigned, n_bool;
    int n_other; /* struct/union/enum/typedef-name/_Atomic(T) */
    u32 storage;
    u32 quals;
    u32 func_specs;
    AstBaseType other_base;
    const char *typedef_name;
    AstNode *record;
    AstType *atomic_inner; /* `_Atomic(type-name)`: the full inner chain */
    bool atomic_specifier; /* `_Atomic(T)` rather than bare `_Atomic` */
    /* ABT_TYPEOF: exactly one is set, decided by one token of lookahead. */
    AstNode *typeof_expr;
    AstType *typeof_type;
    /* _Alignas, as written; sema checks the constraints. */
    AstNode *alignas_expr;
    AstType *alignas_type;
    CgfAttr *cgf_attrs;
    GnuDeclAttrs gnu; /* implemented GNU attributes in specifier position */
    bool has_alignas;
    bool saw_any;
    bool saw_non_storage;
    bool bad;
} SpecSoup;

/* THE SOUP'S TYPE-IDENTITY FIELDS, IN ONE PLACE. Six different sites build
 * an ATY_BASE out of a SpecSoup -- a declaration, a parameter, a member, a
 * type-name, a bare `struct S;`, a typedef -- and each one used to copy the
 * fields by hand. Adding `typeof` meant adding two lines to all six, and
 * patching only the first is exactly why `typeof(int) b = 1;` resolved to
 * TY_ERROR while `sizeof(typeof(a))` worked: the declaration path had the
 * operand and the others did not.
 *
 * Same shape as gnu_attrs_any_symbol_property, add_dir and
 * ir_arg_carry_provenance: a list that must name every field forgets one.
 * The NEXT specifier that carries data forgets none of them or all of them.
 *
 * `quals` is deliberately NOT here -- the AST_EMPTY_DECL site does not
 * take them, and that is a real difference rather than an oversight. */
static void soup_fill_identity(AstType *bt, const SpecSoup *s)
{
    bt->typedef_name = s->typedef_name;
    bt->record = s->record;
    bt->atomic_specifier = s->atomic_specifier;
    bt->atomic_inner = s->atomic_inner;
    bt->typeof_expr = s->typeof_expr;
    bt->typeof_type = s->typeof_type;
}

static bool kw_is_qualifier(Keyword kw)
{
    return kw == KW_CONST || kw == KW_ALT_CONST || kw == KW_ALT_CONST2 ||
           kw == KW_VOLATILE || kw == KW_ALT_VOLATILE ||
           kw == KW_ALT_VOLATILE2 || kw == KW_RESTRICT ||
           kw == KW_ALT_RESTRICT || kw == KW_ALT_RESTRICT2;
}

static u32 qual_bit(Keyword kw)
{
    if (kw == KW_CONST || kw == KW_ALT_CONST || kw == KW_ALT_CONST2)
        return AST_QUAL_CONST;
    if (kw == KW_VOLATILE || kw == KW_ALT_VOLATILE || kw == KW_ALT_VOLATILE2)
        return AST_QUAL_VOLATILE;
    if (kw == KW_RESTRICT || kw == KW_ALT_RESTRICT || kw == KW_ALT_RESTRICT2)
        return AST_QUAL_RESTRICT;
    return 0;
}

/* `__asm__("name")` after a declarator renames the SYMBOL. It sits where an
 * attribute sits and is read the same way, but it is not an attribute: it
 * takes one string and replaces the linker name outright.
 *
 * Apple's headers depend on it -- `__DARWIN_ALIAS` renames `fopen` and friends
 * -- and it cannot be faked, which is why hosted macOS waits on it. The name
 * is emitted VERBATIM: `_fopen$UNIX2003` is a real one, `$` and all. */
static void parse_asm_label(Parser *p, GnuDeclAttrs *gnu)
{
    const Token *kw = parse_peek(p);
    const Token *arg;

    p->pos++; /* asm / __asm__ / __asm */
    if (!parse_eat_punct(p, PUNCT_LPAREN)) {
        parse_error(p, kw, "expected '(' after '%s'", kw->spelling);
        return;
    }
    arg = parse_peek(p);
    if (arg->kind != TOK_STRING || !arg->str.bytes) {
        parse_error(p, arg, "an asm label takes a string naming the symbol");
    } else {
        gnu->asm_name = arena_strdup(p->arena, (const char *)arg->str.bytes);
        p->pos++;
    }
    while (!parse_at_punct(p, PUNCT_RPAREN) && parse_peek(p)->kind != TOK_EOF)
        p->pos++;
    parse_expect_punct(p, PUNCT_RPAREN, "after the asm label");
}

/* True at an asm label in DECLARATOR-suffix position. The same keywords open
 * an inline-asm STATEMENT, which is a different construct entirely; only the
 * position tells them apart. */
static bool parse_at_asm_label(Parser *p)
{
    return parse_at_kw(p, KW_ASM) || parse_at_kw(p, KW_ALT_ASM) ||
           parse_at_kw(p, KW_ALT_ASM2);
}

static AstNode *parse_record_specifier(Parser *p, bool is_union);
static AstNode *parse_enum_specifier(Parser *p);
static AstType *parse_declarator(Parser *p, AstType *base, const char **name,
                                 bool abstract_ok);
static AstNode *parse_initializer(Parser *p);

/* Reduces the collected multiset to a canonical base type (C11 6.7.2p2).
 * Order never matters; only the multiset does. */
static AstBaseType soup_resolve(Parser *p, SpecSoup *s, const Token *at)
{
    int sign = s->n_signed - s->n_unsigned;

    if (s->n_other) {
        if (s->n_void || s->n_char || s->n_short || s->n_int || s->n_long ||
            s->n_float || s->n_double || s->n_signed || s->n_unsigned ||
            s->n_bool) {
            parse_error(p, at,
                        "cannot combine '%s' with another type "
                        "specifier",
                        s->other_base == ABT_TYPEDEF ? s->typedef_name
                                                     : "type specifier");
            s->bad = true;
        }
        return s->other_base;
    }
    if (s->n_signed && s->n_unsigned) {
        parse_error(p, at, "cannot combine 'signed' with 'unsigned'");
        s->bad = true;
        return ABT_INT;
    }
    if (s->n_long > 2) {
        parse_error(p, at, "'long long long' is too long for cgfried");
        s->bad = true;
        return ABT_LLONG;
    }
    if (s->n_void) {
        if (s->n_void > 1 || s->n_char || s->n_short || s->n_int || s->n_long ||
            s->n_float || s->n_double || sign)
            goto conflict;
        return ABT_VOID;
    }
    if (s->n_bool) {
        if (s->n_bool > 1 || s->n_char || s->n_short || s->n_int || s->n_long ||
            s->n_float || s->n_double || sign)
            goto conflict;
        return ABT_BOOL;
    }
    if (s->n_float) {
        if (s->n_float > 1 || s->n_char || s->n_short || s->n_int ||
            s->n_long || s->n_double || sign)
            goto conflict;
        return ABT_FLOAT;
    }
    if (s->n_double) {
        if (s->n_double > 1 || s->n_char || s->n_short || s->n_int || sign)
            goto conflict;
        if (s->n_long == 1)
            return ABT_LDOUBLE;
        if (s->n_long)
            goto conflict;
        return ABT_DOUBLE;
    }
    if (s->n_char) {
        if (s->n_char > 1 || s->n_short || s->n_int || s->n_long)
            goto conflict;
        /* PLAIN char is a THIRD type, distinct from signed/unsigned char. */
        if (s->n_signed)
            return ABT_SCHAR;
        if (s->n_unsigned)
            return ABT_UCHAR;
        return ABT_CHAR;
    }
    if (s->n_short) {
        if (s->n_short > 1 || s->n_long)
            goto conflict;
        return s->n_unsigned ? ABT_USHORT : ABT_SHORT;
    }
    if (s->n_long == 2)
        return s->n_unsigned ? ABT_ULLONG : ABT_LLONG;
    if (s->n_long == 1)
        return s->n_unsigned ? ABT_ULONG : ABT_LONG;
    if (s->n_int || s->n_signed || s->n_unsigned)
        return s->n_unsigned ? ABT_UINT : ABT_INT;
    return ABT_NONE; /* implicit int; the caller diagnoses */

conflict:
    parse_error(p, at, "conflicting or duplicate type specifiers");
    s->bad = true;
    return ABT_INT;
}

static void add_storage(Parser *p, SpecSoup *s, u32 bit, const Token *at,
                        const char *name)
{
    u32 already = s->storage & ~(u32)AST_SC_THREAD_LOCAL;

    if (s->saw_non_storage)
        warn_at_ex(p->lang->warnings, WARN_OLD_STYLE_DECLARATION, at->span,
                   WARN_SUPPRESS_IN_MACRO,
                   "'%s' is not at beginning of declaration", name);

    /* At most one storage class, EXCEPT _Thread_local, which may pair
     * with static or extern (6.7.1p2). */
    if (bit == AST_SC_THREAD_LOCAL) {
        s->storage |= bit;
        return;
    }
    if (already) {
        parse_error(p, at,
                    "multiple storage classes in declaration "
                    "specifiers ('%s')",
                    name);
        s->bad = true;
    }
    s->storage |= bit;
}

/* Returns false if no specifier was consumed. */
static bool parse_decl_specs(Parser *p, SpecSoup *s)
{
    const Token *first = parse_peek(p);

    memset(s, 0, sizeof(*s));
    for (;;) {
        const Token *t = parse_peek(p);

        if (t->kind == TOK_KEYWORD) {
            Keyword kw = (Keyword)t->kw;

            if (kw_is_qualifier(kw)) {
                u32 q = qual_bit(kw);
                if ((s->quals & q) && !std_is_c99_or_later(p->lang->std))
                    parse_error(p, t,
                                "duplicate '%s' qualifier is a C99 "
                                "feature",
                                t->spelling);
                s->quals |= q;
                s->saw_any = true;
                s->saw_non_storage = true;
                p->pos++;
                continue;
            }
            if (kw != KW_TYPEDEF && kw != KW_EXTERN && kw != KW_STATIC &&
                kw != KW_AUTO && kw != KW_REGISTER && kw != KW_THREAD_LOCAL &&
                kw != KW_ALT_THREAD)
                s->saw_non_storage = true;
            switch (kw) {
            case KW_TYPEDEF:
                add_storage(p, s, AST_SC_TYPEDEF, t, "typedef");
                goto consumed;
            case KW_EXTERN:
                add_storage(p, s, AST_SC_EXTERN, t, "extern");
                goto consumed;
            case KW_STATIC:
                add_storage(p, s, AST_SC_STATIC, t, "static");
                goto consumed;
            case KW_AUTO:
                add_storage(p, s, AST_SC_AUTO, t, "auto");
                goto consumed;
            case KW_REGISTER:
                add_storage(p, s, AST_SC_REGISTER, t, "register");
                goto consumed;
            case KW_ALT_THREAD:
            case KW_THREAD_LOCAL:
                add_storage(p, s, AST_SC_THREAD_LOCAL, t, "_Thread_local");
                goto consumed;
            case KW_INLINE:
            case KW_ALT_INLINE:
            case KW_ALT_INLINE2:
                s->func_specs |= AST_FS_INLINE;
                goto consumed;
            case KW_NORETURN:
                s->func_specs |= AST_FS_NORETURN;
                goto consumed;
            case KW_VOID:
                s->n_void++;
                goto consumed;
            case KW_CHAR:
                s->n_char++;
                goto consumed;
            case KW_SHORT:
                s->n_short++;
                goto consumed;
            case KW_INT:
                s->n_int++;
                goto consumed;
            case KW_LONG:
                s->n_long++;
                goto consumed;
            case KW_FLOAT:
                s->n_float++;
                goto consumed;
            case KW_DOUBLE:
                s->n_double++;
                goto consumed;
            case KW_SIGNED:
            case KW_ALT_SIGNED:
                s->n_signed++;
                goto consumed;
            case KW_UNSIGNED:
                s->n_unsigned++;
                goto consumed;
            case KW_BOOL:
                s->n_bool++;
                goto consumed;
            case KW_STRUCT:
            case KW_UNION: {
                bool is_union = kw == KW_UNION;
                p->pos++;
                s->n_other++;
                s->other_base = ABT_RECORD;
                s->record = parse_record_specifier(p, is_union);
                s->saw_any = true;
                continue;
            }
            case KW_ENUM:
                p->pos++;
                s->n_other++;
                s->other_base = ABT_ENUM;
                s->record = parse_enum_specifier(p);
                s->saw_any = true;
                continue;
            case KW_ATOMIC:
                /* _Atomic(T) is a specifier; bare _Atomic is a
                 * qualifier. Sema owns the semantics. */
                p->pos++;
                s->saw_any = true;
                if (parse_at_punct(p, PUNCT_LPAREN)) {
                    /* The operand is a full TYPE-NAME: `_Atomic(int *)` is
                     * legal, so the abstract-declarator machinery applies,
                     * not just the specifier soup. Whether the named type
                     * is allowed (no arrays, no functions, 6.7.2.4p3) is
                     * sema's question — it needs the resolved chain. */
                    p->pos++;
                    s->atomic_inner = parse_type_name(p);
                    parse_expect_punct(p, PUNCT_RPAREN, "after _Atomic(type)");
                    s->n_other++;
                    s->other_base = ABT_INT; /* replaced by sema via
                                                atomic_inner */
                    s->atomic_specifier = true;
                }
                s->quals |= AST_QUAL_ATOMIC;
                continue;
            case KW_COMPLEX:
            case KW_IMAGINARY:
                parse_error(p, t,
                            "'%s' is out of scope for v0.1.0: cgfried "
                            "defines __STDC_NO_COMPLEX__",
                            t->spelling);
                s->bad = true;
                goto consumed;
            case KW_TYPEOF:
            case KW_ALT_TYPEOF:
            case KW_ALT_TYPEOF2:
                /* `typeof (` then EITHER a type-name OR an expression, and
                 * the one-token lookahead that separates a cast from a call
                 * separates them here too -- parse_at_type_name is the same
                 * predicate, so the two constructs cannot drift apart.
                 *
                 * The expression is parsed UNEVALUATED: `typeof(f())` does
                 * not call f. Same flag _Generic and sizeof use, and for the
                 * same reason -- lowering must never see the side effects of
                 * an operand the language says is not evaluated. */
                p->pos++;
                s->n_other++;
                s->other_base = ABT_TYPEOF;
                s->saw_any = true;
                parse_expect_punct(p, PUNCT_LPAREN, "after 'typeof'");
                if (parse_at_type_name(p)) {
                    s->typeof_type = parse_type_name(p);
                } else {
                    p->unevaluated++;
                    s->typeof_expr = parse_expr(p);
                    p->unevaluated--;
                }
                parse_expect_close(p, PUNCT_RPAREN, t->span,
                                   "after the typeof operand");
                continue;
            case KW_AUTO_TYPE:
                /* The type is the INITIALIZER's, so there is nothing to
                 * record here beyond the fact -- sema resolves it once the
                 * initializer has been typed. */
                p->pos++;
                s->n_other++;
                s->other_base = ABT_AUTO_TYPE;
                s->saw_any = true;
                continue;
            case KW_FLOAT128:
            case KW_ALT_FLOAT128:
                /* A standalone specifier: it combines with nothing, so it
                 * rides `n_other` and the existing conflict machinery
                 * rejects `long _Float128` and friends for free. */
                s->n_other++;
                s->other_base = ABT_FLOAT128;
                goto consumed;
            case KW_ALT_BUILTIN_VA_LIST:
                /* Lexed as a KEYWORD by the Sprint 8 table, so it never
                 * reaches the identifier path below — our own shipped
                 * <stdarg.h> lands here. A compiler-provided type
                 * specifier since Sprint 19 (pulled forward from the
                 * Sprint 28 builtin family: varargs lowering needed it). */
                s->n_other++;
                s->other_base = ABT_VA_LIST;
                goto consumed;
            case KW_ATTRIBUTE:
            case KW_ATTRIBUTE2: {
                /* `packed` is collected into a fresh set rather than straight
                 * into s->gnu, because WHERE it appeared decides what it packs
                 * and this is the only place that still knows. Following a
                 * record DEFINITION it packs that record; anywhere else it
                 * stays on the declaration, where a member claims it and every
                 * other declaration warns that it was ignored. */
                GnuDeclAttrs here = {0};

                s->cgf_attrs = parse_cgf_attrs_concat(
                    p, s->cgf_attrs, parse_cgf_attributes(p, &here));
                if (here.packed && s->record && s->record->is_definition) {
                    s->record->packed = true;
                    here.packed = false;
                }
                if ((here.aligned_expr || here.aligned_bare) && s->record &&
                    s->record->is_definition) {
                    /* Measured: `struct S {...} aligned(16) v;` aligns the
                     * RECORD (and v inherits it), while `struct S w
                     * aligned(16);` on an already-defined tag aligns only w. */
                    s->record->record_aligned_expr = here.aligned_expr;
                    s->record->record_aligned_bare = here.aligned_bare;
                    here.aligned_expr = NULL;
                    here.aligned_bare = false;
                }
                gnu_attrs_merge(&s->gnu, &here);
                s->saw_any = true;
                continue;
            }
            case KW_ALIGNAS: {
                /* `_Alignas(N)` or `_Alignas(type-name)`. It is an
                 * alignment SPECIFIER, so it may appear anywhere in the
                 * specifier list; which of the two forms was written is
                 * decided by the same typedef-table lookup that separates
                 * a cast from a parenthesized expression. */
                p->pos++;
                parse_expect_punct(p, PUNCT_LPAREN, "after '_Alignas'");
                if (parse_at_type_name(p))
                    s->alignas_type = parse_type_name(p);
                else
                    s->alignas_expr = parse_cond_expr(p);
                parse_expect_punct(p, PUNCT_RPAREN,
                                   "after the _Alignas "
                                   "operand");
                s->has_alignas = true;
                s->saw_any = true;
                continue;
            }
            case KW_EXTENSION:
                p->pos++; /* __extension__ just suppresses pedwarns */
                continue;
            default:
                goto done;
            }
        consumed:
            s->saw_any = true;
            p->pos++;
            continue;
        }

        if (t->kind == TOK_IDENT) {
            /* A typedef name is a type specifier ONLY if no other type
             * specifier was seen: in `unsigned T x;` the T is the
             * DECLARATOR name, not a specifier. */
            if (s->n_other || s->n_void || s->n_char || s->n_short ||
                s->n_int || s->n_long || s->n_float || s->n_double ||
                s->n_signed || s->n_unsigned || s->n_bool)
                goto done;
            if (!parse_is_typedef_name(p, t->spelling)) {
                /* `__builtin_va_list` and friends are compiler-provided
                 * TYPES, not typedefs anyone declared, so they reach here
                 * as plain identifiers. Name the sprint that makes them
                 * real rather than emitting a generic "expected a
                 * declarator" — our own <stdarg.h> lands on this path. */
                if (parse_is_builtin_name(t->spelling)) {
                    parse_error(p, t,
                                "'%s' is not a builtin this compiler "
                                "implements (see src/builtins.def)",
                                t->spelling);
                    s->bad = true;
                    s->saw_any = true;
                    p->pos++;
                    goto done;
                }
                goto done;
            }
            s->n_other++;
            s->other_base = ABT_TYPEDEF;
            s->typedef_name = t->spelling;
            s->saw_any = true;
            p->pos++;
            continue;
        }
        goto done;
    }
done:
    (void)first;
    return s->saw_any;
}

/* `__builtin_` is a reserved prefix the compiler owns. Recognizing it by
 * spelling — rather than by a table of the ones we implement — is what
 * lets EVERY builtin, type or function, report the same honest deferral
 * instead of decaying into a syntax error or an implicit declaration. */
bool parse_is_builtin_name(const char *name)
{
    return name && strncmp(name, "__builtin_", 10) == 0;
}

/* True for the builtins we actually IMPLEMENT (src/builtins.def, the
 * same table sema types them from). The parser needs the question
 * without depending on sema — the include arrow runs parse <- sema —
 * so both layers read the .def rather than sharing a function. */
bool parse_is_known_builtin(const char *name)
{
    static const char *const known[] = {
#define B(sfx, NAME, n, k) "__builtin_" #sfx,
#include "builtins.def"
#undef B
    };
    size_t i;

    if (!name)
        return false;
    for (i = 0; i < CGF_ARRAY_LEN(known); i++)
        if (strcmp(known[i], name) == 0)
            return true;
    return false;
}

bool parse_at_decl_specs(Parser *p)
{
    const Token *t = parse_peek(p);

    if (t->kind == TOK_IDENT) {
        /* The builtins we IMPLEMENT are expressions, not types, so a
         * statement starting with one must not be read as a declaration
         * (Sprint 28: before the table existed, `__builtin_memset(p, 0,
         * n);` parsed as a declaration of `p`). Unimplemented
         * __builtin_ names still route to the declaration path, where
         * the honest deferral lives. */
        if (parse_is_builtin_name(t->spelling) &&
            (parse_is_known_builtin(t->spelling) ||
             strcmp(t->spelling, "__builtin_va_arg") == 0 ||
             strcmp(t->spelling, "__builtin_offsetof") == 0))
            return false;
        return parse_is_typedef_name(p, t->spelling) ||
               parse_is_builtin_name(t->spelling);
    }
    if (t->kind != TOK_KEYWORD)
        return false;
    switch ((Keyword)t->kw) {
    case KW_TYPEDEF:
    case KW_EXTERN:
    case KW_STATIC:
    case KW_AUTO:
    case KW_REGISTER:
    case KW_ALT_THREAD:
    case KW_THREAD_LOCAL:
    case KW_INLINE:
    case KW_ALT_INLINE:
    case KW_ALT_INLINE2:
    case KW_NORETURN:
    case KW_VOID:
    case KW_CHAR:
    case KW_SHORT:
    case KW_INT:
    case KW_LONG:
    case KW_FLOAT:
    case KW_DOUBLE:
    case KW_FLOAT128:
    case KW_ALT_FLOAT128:
    case KW_SIGNED:
    case KW_ALT_SIGNED:
    case KW_UNSIGNED:
    case KW_BOOL:
    case KW_STRUCT:
    case KW_UNION:
    case KW_ENUM:
    case KW_ATOMIC:
    case KW_CONST:
    case KW_ALT_CONST:
    case KW_ALT_CONST2:
    case KW_VOLATILE:
    case KW_ALT_VOLATILE:
    case KW_ALT_VOLATILE2:
    case KW_RESTRICT:
    case KW_ALT_RESTRICT:
    case KW_ALT_RESTRICT2:
    case KW_COMPLEX:
    case KW_IMAGINARY:
    case KW_TYPEOF:
    case KW_ALT_TYPEOF:
    case KW_ALT_TYPEOF2:
    case KW_AUTO_TYPE:
    case KW_ALT_BUILTIN_VA_LIST:
    case KW_ATTRIBUTE:
    case KW_ATTRIBUTE2:
    case KW_EXTENSION:
    case KW_STATIC_ASSERT:
    case KW_ALIGNAS:
        return true;
    default:
        return false;
    }
}

/* --- declarators -------------------------------------------------------- */

/* Parses `[...]` and `(...)` suffixes left to right, chaining each onto
 * the front of `inner` — which is what makes `f[3](void)` read as "array
 * of func" and not the reverse. */
static AstType *parse_type_suffixes(Parser *p, AstType *inner);

static AstType *parse_param_list(Parser *p, AstType *ret)
{
    AstType *fn = ast_type_new(p->arena, ATY_FUNC, parse_peek(p)->span);
    ParamVec params = {NULL, 0, 0};

    fn->next = ret;
    /* Parameters get their own scope, SHARED with the body's outermost
     * block (6.2.1p4) — the function-definition path deliberately does
     * not push a second scope at `{`. */
    parse_scope_enter(p);

    if (parse_eat_punct(p, PUNCT_RPAREN)) {
        /* `f()` is UNSPECIFIED parameters (K&R), never `(void)`. */
        fn->has_no_params = true;
        goto out;
    }
    /* `(void)` — exactly one `void` and nothing else — means NO params. */
    if (parse_at_kw(p, KW_VOID) && parse_peek_n(p, 1)->kind == TOK_PUNCT &&
        parse_peek_n(p, 1)->punct == PUNCT_RPAREN) {
        p->pos += 2;
        goto out;
    }
    /* A bare identifier that is NOT a typedef name starts a K&R list;
     * `int f(x)` with x a typedef is a PROTOTYPE with an unnamed param. */
    if (parse_peek(p)->kind == TOK_IDENT &&
        !parse_is_typedef_name(p, parse_peek(p)->spelling)) {
        fn->is_kr_list = true;
        for (;;) {
            const Token *id = parse_peek(p);
            AstParam prm;

            if (id->kind != TOK_IDENT) {
                parse_error(p, id,
                            "expected parameter name in identifier "
                            "list");
                break;
            }
            memset(&prm, 0, sizeof(prm));
            prm.name = id->spelling;
            prm.span = id->span;
            prm.type = NULL; /* type comes from the K&R declaration list */
            ParamVec_push(&params, prm);
            parse_scope_declare(p, id->spelling, false);
            p->pos++;
            if (!parse_eat_punct(p, PUNCT_COMMA))
                break;
        }
        parse_expect_punct(p, PUNCT_RPAREN, "after parameter list");
        goto out;
    }

    for (;;) {
        SpecSoup s;
        AstType *base;
        AstParam prm;
        const Token *start = parse_peek(p);

        if (parse_eat_punct(p, PUNCT_ELLIPSIS)) {
            fn->is_variadic = true;
            break;
        }
        if (!parse_decl_specs(p, &s)) {
            parse_error(p, start,
                        "expected a parameter declaration but "
                        "found '%s'",
                        tok_desc(start));
            break;
        }
        /* 6.7.1p6: `register` is the only storage class permitted in a
         * parameter declaration.  AstParam intentionally carries no
         * storage-duration state, so reject the invalid forms before the
         * information is discarded. */
        if (s.storage & ~(u32)AST_SC_REGISTER)
            parse_error(p, start,
                        "invalid storage class in function parameter");
        base = ast_type_new(p->arena, ATY_BASE, start->span);
        base->base = soup_resolve(p, &s, start);
        soup_fill_identity(base, &s);
        base->quals = s.quals;
        /* 6.7.5p2: an alignment specifier may not appear on a parameter.
         * Checked HERE rather than in sema because a parameter is an
         * AstParam, not an AstNode, so there is nowhere to carry the
         * request forward — and nothing downstream should have to. */
        if (s.has_alignas)
            parse_error(p, start,
                        "'_Alignas' cannot appear on a function parameter");

        memset(&prm, 0, sizeof(prm));
        prm.span = start->span;
        prm.type = parse_declarator(p, base, &prm.name, true);
        prm.cgf_attrs = parse_cgf_attrs_concat(p, s.cgf_attrs, NULL);
        while (parse_at_kw(p, KW_ATTRIBUTE) || parse_at_kw(p, KW_ATTRIBUTE2)) {
            /* A scratch sink: an implemented attribute on a PARAMETER has
             * nowhere to go, and letting it reach the specifier soup would
             * leak one parameter's `weak` onto the whole declaration. It is
             * still SAID rather than dropped -- gcc warns here, and a silent
             * drop is the failure mode the tier table exists to prevent. */
            GnuDeclAttrs param_gnu = {0};
            const Token *at = parse_peek(p);

            prm.cgf_attrs = parse_cgf_attrs_concat(
                p, prm.cgf_attrs, parse_cgf_attributes(p, &param_gnu));
            /* Any symbol property, not a hand-listed three: see
             * gnu_attrs_any_symbol_property. gcc splits this finer — `used`,
             * `constructor`, `alias` and `cleanup` warn while `aligned` and
             * `section` are errors — but a parameter cannot carry any of them
             * either way, so one warning covers the position and is the
             * accepting direction of the two. */
            if (gnu_attrs_any_symbol_property(&param_gnu))
                warn_at(p->lang->warnings, WARN_ATTRIBUTES, at->span,
                        "attribute ignored on a function parameter");
            /* A TYPE property cannot take the same exit. gcc gives the
             * parameter the mode's width, so dropping it would hand the
             * callee an `int` where the caller passed a `long` -- a
             * silent ABI mismatch, with only a warning to show for it.
             * AstParam carries no GnuDeclAttrs to plumb it through, so
             * this is an honest error rather than a wrong answer. */
            if (gnu_attrs_any_type_property(&param_gnu))
                parse_error(p, at,
                            "the 'mode' attribute is not supported on a "
                            "function parameter: it would change the "
                            "parameter's width and therefore the calling "
                            "convention (docs/gnu-extensions.md)");
        }
        if (prm.name)
            parse_scope_declare(p, prm.name, false);
        ParamVec_push(&params, prm);
        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
    }
    parse_expect_punct(p, PUNCT_RPAREN, "after parameter list");

out:
    fn->nparams = (u32)params.len;
    if (params.len) {
        fn->params = arena_alloc(p->arena, params.len * sizeof(AstParam),
                                 _Alignof(AstParam));
        memcpy(fn->params, params.data, params.len * sizeof(AstParam));
    }
    ParamVec_free(&params);
    parse_scope_leave(p);
    return fn;
}

static AstType *parse_array_suffix(Parser *p, AstType *elem)
{
    AstType *arr = ast_type_new(p->arena, ATY_ARRAY, parse_peek(p)->span);

    arr->next = elem;
    /* Parameter-array forms: `static`, qualifiers, and `*` may appear
     * inside the brackets. Constraints are Sprint 16's. */
    for (;;) {
        if (parse_eat_kw(p, KW_STATIC)) {
            arr->array_static = true;
            continue;
        }
        if (parse_peek(p)->kind == TOK_KEYWORD &&
            kw_is_qualifier((Keyword)parse_peek(p)->kw)) {
            arr->array_quals |= qual_bit((Keyword)parse_peek(p)->kw);
            p->pos++;
            continue;
        }
        break;
    }
    if (parse_at_punct(p, PUNCT_STAR) &&
        parse_peek_n(p, 1)->kind == TOK_PUNCT &&
        parse_peek_n(p, 1)->punct == PUNCT_RBRACKET) {
        arr->array_star = true;
        p->pos++;
    } else if (!parse_at_punct(p, PUNCT_RBRACKET)) {
        /* The SIZE EXPRESSION is stored verbatim — Sprint 15 evaluates
         * it, and VLA-ness is Sprint 16's question. */
        arr->array_size = parse_assign_expr(p);
    }
    parse_expect_punct(p, PUNCT_RBRACKET, "after array bound");
    return arr;
}

/* Suffixes bind LEFT TO RIGHT, so the FIRST one is outermost: `a[2][3]`
 * is an array of 2 arrays of 3 ints, not the reverse. Wrapping each new
 * suffix around the previous result gets this backwards — invisibly so
 * when every bound is present (`int a[3][4]` reads the same either way),
 * which is why the bug survived Sprint 9's declarator torture and only
 * surfaced when sema checked `int a[][3]` for a complete element type.
 *
 * So: parse the suffixes in order, then thread them head-to-tail with the
 * base type last. */
static AstType *parse_type_suffixes(Parser *p, AstType *inner)
{
    AstType *head = NULL;
    AstType *tail = NULL;

    for (;;) {
        AstType *suffix;

        if (parse_at_punct(p, PUNCT_LBRACKET)) {
            p->pos++;
            suffix = parse_array_suffix(p, NULL);
        } else if (parse_at_punct(p, PUNCT_LPAREN)) {
            p->pos++;
            suffix = parse_param_list(p, NULL);
        } else {
            break;
        }
        if (tail)
            tail->next = suffix;
        else
            head = suffix;
        tail = suffix;
    }
    if (!head)
        return inner;
    tail->next = inner;
    return head;
}

/* THE declarator recursion.
 *
 *   int (*(*f[3])(void))[5]
 *     core: '(' -> recurse: core '(' -> recurse: core f; suffix [3]
 *                                     => f: array 3 of <inner>
 *           the '*' inside the first paren applies next
 *                                     => ... of ptr to <...>
 *           suffix (void)             => ... to func(void) ret <...>
 *           the outer '*'             => ... ret ptr to <...>
 *   outer suffix [5], base int        => ... to array 5 of int
 *
 * Implementation note: the parenthesized group is parsed with a PLACEHOLDER
 * base, then the suffixes AFTER the group are parsed and spliced onto the
 * placeholder's tail. That is one pass with a fixup, rather than chibicc's
 * re-walk of the saved token cursor — same result, no rescanning, and the
 * splice point is explicit. */
static AstType *parse_declarator(Parser *p, AstType *base, const char **name,
                                 bool abstract_ok)
{
    AstType *ptrs = NULL;

    if (name)
        *name = NULL;

    /* 1. Pointer prefix (with qualifier lists), innermost applied LAST. */
    while (parse_at_punct(p, PUNCT_STAR)) {
        AstType *ptr = ast_type_new(p->arena, ATY_PTR, parse_peek(p)->span);
        p->pos++;
        while (parse_peek(p)->kind == TOK_KEYWORD &&
               (kw_is_qualifier((Keyword)parse_peek(p)->kw) ||
                parse_peek(p)->kw == KW_ATOMIC)) {
            if (parse_peek(p)->kw == KW_ATOMIC)
                ptr->ptr_quals |= AST_QUAL_ATOMIC;
            else
                ptr->ptr_quals |= qual_bit((Keyword)parse_peek(p)->kw);
            p->pos++;
        }
        /* Prepend: the LAST '*' parsed binds closest to the base. */
        ptr->next = ptrs;
        ptrs = ptr;
    }

    /* 2. Direct-declarator core. */
    if (parse_at_punct(p, PUNCT_LPAREN)) {
        /* '(' is GROUPING only if what follows cannot start a parameter
         * list. `int (*)(void)` groups; `int (int)` and `int ()` are
         * parameter lists of an id-less declarator. */
        const Token *nx = parse_peek_n(p, 1);
        bool is_group = !(nx->kind == TOK_PUNCT && nx->punct == PUNCT_RPAREN);

        if (is_group && nx->kind == TOK_IDENT &&
            parse_is_typedef_name(p, nx->spelling))
            is_group = false; /* (T) is a parameter list */
        if (is_group && nx->kind == TOK_KEYWORD) {
            /* A specifier keyword after '(' means parameters — unless it
             * is a qualifier applying to a pointer inside the group. */
            u32 save = p->pos;
            p->pos++;
            is_group = !parse_at_decl_specs(p);
            p->pos = save;
        }

        if (is_group) {
            AstType *placeholder;
            AstType *group;
            AstType *after;

            p->pos++; /* '(' */
            placeholder = ast_type_new(p->arena, ATY_BASE, parse_peek(p)->span);
            placeholder->base = ABT_NONE;
            group = parse_declarator(p, placeholder, name, abstract_ok);
            parse_expect_punct(p, PUNCT_RPAREN, "after declarator group");

            /* Suffixes AFTER the group derive the type the group points
             * at; splice them in place of the placeholder. */
            after = parse_type_suffixes(p, ast_type_chain(ptrs, base));
            {
                AstType *q = group;
                AstType *prev = NULL;
                while (q && q != placeholder) {
                    prev = q;
                    q = q->next;
                }
                if (prev)
                    prev->next = after;
                else
                    group = after;
            }
            return group;
        }
    }

    if (parse_peek(p)->kind == TOK_IDENT) {
        if (name)
            *name = parse_peek(p)->spelling;
        p->pos++;
    } else if (!abstract_ok) {
        parse_error(p, parse_peek(p), "expected a declarator but found '%s'",
                    tok_desc(parse_peek(p)));
    }

    /* 3. Suffixes, then the pointer prefix, then the base. */
    return parse_type_suffixes(p, ast_type_chain(ptrs, base));
}

/* --- struct / union / enum ---------------------------------------------- */

static AstNode *parse_member_decl(Parser *p);

static AstNode *parse_record_specifier(Parser *p, bool is_union)
{
    AstNode *rec = ast_new(p->arena, AST_RECORD_DECL, parse_peek(p)->span);
    NodeVec members = {NULL, 0, 0};

    rec->is_union = is_union;
    /* `struct __attr__((packed)) S { ... };` -- the attribute sits between the
     * keyword and the tag and binds to the record. Measured against gcc, as
     * is the negative: a LEADING attribute, before the keyword, is ignored. */
    while (parse_at_kw(p, KW_ATTRIBUTE) || parse_at_kw(p, KW_ATTRIBUTE2)) {
        GnuDeclAttrs inner = {0};

        parse_cgf_attributes(p, &inner);
        if (inner.packed)
            rec->packed = true;
        if (inner.aligned_expr || inner.aligned_bare) {
            rec->record_aligned_expr = inner.aligned_expr;
            rec->record_aligned_bare = inner.aligned_bare;
        }
    }
    if (parse_peek(p)->kind == TOK_IDENT ||
        (parse_peek(p)->kind == TOK_KEYWORD &&
         !parse_at_punct(p, PUNCT_LBRACE) && false)) {
        rec->tag = parse_peek(p)->spelling;
        scope_declare_tag(p, rec->tag);
        p->pos++;
    }
    if (!parse_eat_punct(p, PUNCT_LBRACE)) {
        if (!rec->tag)
            parse_error(p, parse_peek(p), "expected a tag or '{' after '%s'",
                        is_union ? "union" : "struct");
        return rec; /* forward reference: `struct S;` / `struct S *p;` */
    }
    rec->is_definition = true;
    parse_scope_enter(p);
    while (!parse_at_punct(p, PUNCT_RBRACE) && parse_peek(p)->kind != TOK_EOF) {
        AstNode *m = parse_member_decl(p);
        if (m)
            NodeVec_push(&members, m);
        else
            break;
    }
    parse_scope_leave(p);
    parse_expect_punct(p, PUNCT_RBRACE, "at end of member list");
    rec->nmembers = (u32)members.len;
    if (members.len) {
        rec->members = arena_alloc(p->arena, members.len * sizeof(AstNode *),
                                   _Alignof(AstNode *));
        memcpy(rec->members, members.data, members.len * sizeof(AstNode *));
    }
    NodeVec_free(&members);
    return rec;
}

static AstNode *parse_static_assert(Parser *p)
{
    AstNode *n = ast_new(p->arena, AST_STATIC_ASSERT, parse_peek(p)->span);

    p->pos++; /* _Static_assert */
    parse_expect_punct(p, PUNCT_LPAREN, "after '_Static_assert'");
    n->assert_expr = parse_cond_expr(p);
    if (parse_eat_punct(p, PUNCT_COMMA)) {
        if (parse_peek(p)->kind != TOK_STRING) {
            parse_error(p, parse_peek(p), "expected a string literal message");
        } else {
            n->assert_msg = parse_peek(p);
            p->pos++;
        }
    } else {
        /* The message-less form is C23. */
        parse_error(p, parse_peek(p),
                    "_Static_assert without a message is a C23 feature");
    }
    parse_expect_punct(p, PUNCT_RPAREN, "after _Static_assert");
    parse_expect_punct(p, PUNCT_SEMI, "after _Static_assert");
    return n;
}

/* One member declaration: specifiers + declarator list with bitfields,
 * or a C11 anonymous struct/union member. */
static AstNode *parse_member_decl(Parser *p)
{
    SpecSoup s;
    AstNode *first = NULL;
    const Token *start = parse_peek(p);
    AstBaseType base_kind;

    if (parse_at_kw(p, KW_STATIC_ASSERT))
        return parse_static_assert(p);

    if (!parse_decl_specs(p, &s)) {
        parse_error(p, start, "expected a member declaration but found '%s'",
                    tok_desc(start));
        p->pos++;
        return NULL;
    }
    base_kind = soup_resolve(p, &s, start);

    if (parse_eat_punct(p, PUNCT_SEMI)) {
        /* No declarator. An UNTAGGED struct/union specifier here is a C11
         * anonymous member; a TAGGED one is the MS/Plan9 extension. */
        AstNode *n = ast_new(p->arena, AST_DECL, start->span);
        AstType *bt = ast_type_new(p->arena, ATY_BASE, start->span);

        bt->base = base_kind;
        soup_fill_identity(bt, &s);
        bt->quals = s.quals;
        n->type = bt;
        n->cgf_attrs = parse_cgf_attrs_concat(p, s.cgf_attrs, NULL);
        gnu_attrs_merge(&n->gnu, &s.gnu);
        if (base_kind == ABT_RECORD && s.record && !s.record->tag) {
            n->is_anon_member = true;
            if (!std_is_c11_or_later(p->lang->std))
                warn_at(p->lang->warnings, WARN_C11_EXTENSIONS, start->span,
                        "anonymous struct/union members are a C11 "
                        "feature");
        } else if (base_kind == ABT_RECORD && s.record && s.record->tag &&
                   s.record->is_definition) {
            parse_error(p, start,
                        "a tagged struct/union member without a declarator "
                        "is a Microsoft extension (lands in Sprint 55)");
        } else {
            warn_at(p->lang->warnings, WARN_EMPTY_DECLARATION, start->span,
                    "declaration does not declare anything");
        }
        return n;
    }

    for (;;) {
        AstNode *n = ast_new(p->arena, AST_DECL, parse_peek(p)->span);
        AstType *bt = ast_type_new(p->arena, ATY_BASE, start->span);

        bt->base = base_kind;
        soup_fill_identity(bt, &s);
        bt->quals = s.quals;

        n->has_alignas = s.has_alignas;
        n->alignas_expr = s.alignas_expr;
        n->alignas_type = s.alignas_type;
        if (!parse_at_punct(p, PUNCT_COLON))
            n->type = parse_declarator(p, bt, &n->name, true);
        else
            n->type = bt; /* unnamed bitfield */
        n->cgf_attrs = parse_cgf_attrs_concat(p, s.cgf_attrs, NULL);
        gnu_attrs_merge(&n->gnu, &s.gnu);
        while (parse_at_kw(p, KW_ATTRIBUTE) || parse_at_kw(p, KW_ATTRIBUTE2))
            n->cgf_attrs = parse_cgf_attrs_concat(
                p, n->cgf_attrs, parse_cgf_attributes(p, &n->gnu));

        if (parse_eat_punct(p, PUNCT_COLON)) {
            /* The WIDTH is an expression; Sprint 14/15 check it. */
            n->is_bitfield = true;
            n->bitfield_width = parse_cond_expr(p);
        }
        if (!first)
            first = n;
        else {
            /* Sibling declarators chain through `items` so the caller sees
             * them all. */
            AstNode **grown =
                arena_alloc(p->arena, (first->nitems + 1) * sizeof(AstNode *),
                            _Alignof(AstNode *));
            if (first->nitems)
                memcpy(grown, first->items, first->nitems * sizeof(AstNode *));
            grown[first->nitems] = n;
            first->items = grown;
            first->nitems++;
        }
        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
    }
    parse_expect_punct(p, PUNCT_SEMI, "after member declaration");
    return first;
}

static AstNode *parse_enum_specifier(Parser *p)
{
    AstNode *en = ast_new(p->arena, AST_ENUM_DECL, parse_peek(p)->span);
    NodeVec items = {NULL, 0, 0};

    if (parse_peek(p)->kind == TOK_IDENT) {
        en->tag = parse_peek(p)->spelling;
        scope_declare_tag(p, en->tag);
        p->pos++;
    }
    if (!parse_eat_punct(p, PUNCT_LBRACE)) {
        if (!en->tag)
            parse_error(p, parse_peek(p), "expected a tag or '{' after 'enum'");
        return en;
    }
    en->is_definition = true;
    for (;;) {
        AstNode *item;
        const Token *id = parse_peek(p);

        if (parse_at_punct(p, PUNCT_RBRACE))
            break;
        if (id->kind != TOK_IDENT) {
            parse_error(p, id, "expected an enumerator name");
            break;
        }
        item = ast_new(p->arena, AST_ENUMERATOR, id->span);
        item->name = id->spelling;
        p->pos++;
        /* An enumerator may carry attributes, and the position is AFTER the
         * name: gcc rejects a LEADING attribute with "expected identifier"
         * and accepts a trailing one (`EV attr((X)) = 1`). Parsing them
         * here is what lets `deprecated` reach an enumerator at all. */
        if (parse_at_kw(p, KW_ATTRIBUTE))
            (void)parse_cgf_attributes(p, &item->gnu);
        if (parse_eat_punct(p, PUNCT_ASSIGN))
            item->init = parse_cond_expr(p); /* the VALUE EXPRESSION */
        /* Enumerators enter the ORDINARY namespace — which is how they
         * un-typedef a name for later lookups. */
        parse_scope_declare(p, item->name, false);
        NodeVec_push(&items, item);
        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
        /* A trailing comma before '}' is legal in c99+. */
        if (parse_at_punct(p, PUNCT_RBRACE)) {
            if (!std_is_c99_or_later(p->lang->std))
                warn_at(p->lang->warnings, WARN_C90_C99_COMPAT,
                        parse_peek(p)->span,
                        "a trailing comma in an enumerator list is "
                        "a C99 feature");
            break;
        }
    }
    parse_expect_punct(p, PUNCT_RBRACE, "at end of enumerator list");
    en->nmembers = (u32)items.len;
    if (items.len) {
        en->members = arena_alloc(p->arena, items.len * sizeof(AstNode *),
                                  _Alignof(AstNode *));
        memcpy(en->members, items.data, items.len * sizeof(AstNode *));
    }
    NodeVec_free(&items);
    return en;
}

/* --- initializers (syntax only) ----------------------------------------- */

static AstNode *parse_initializer(Parser *p)
{
    AstNode *n;
    NodeVec items = {NULL, 0, 0};

    if (!parse_at_punct(p, PUNCT_LBRACE))
        return parse_assign_expr(p);

    n = ast_new(p->arena, AST_INIT_LIST, parse_peek(p)->span);
    p->pos++;
    if (parse_at_punct(p, PUNCT_RBRACE)) {
        /* Empty braces are C23. gcc accepts them as an extension and only
         * errors under -pedantic-errors, so we PEDWARN and accept —
         * verified against gcc -std=c17 (Sprint 37 makes the flag real). */
        warn_at(p->lang->warnings, WARN_C23_EXTENSIONS, parse_peek(p)->span,
                "ISO C forbids empty initializer braces before C23");
        p->pos++;
        return n;
    }
    for (;;) {
        AstNode *item;
        NodeVec desigs = {NULL, 0, 0};

        /* Designator chain: [expr] and .field, repeatable ([2].x[1]). */
        while (parse_at_punct(p, PUNCT_LBRACKET) ||
               parse_at_punct(p, PUNCT_DOT)) {
            AstNode *d = ast_new(p->arena, AST_DESIGNATOR, parse_peek(p)->span);
            if (parse_eat_punct(p, PUNCT_LBRACKET)) {
                d->desig_index = parse_cond_expr(p);
                if (parse_at_punct(p, PUNCT_ELLIPSIS)) {
                    parse_error(p, parse_peek(p),
                                "GNU range designators are not yet supported "
                                "(land in Sprint 55)");
                    p->pos++;
                    (void)parse_cond_expr(p);
                }
                parse_expect_punct(p, PUNCT_RBRACKET, "after designator");
            } else {
                p->pos++; /* '.' */
                if (parse_peek(p)->kind != TOK_IDENT) {
                    parse_error(p, parse_peek(p),
                                "expected a field name after '.'");
                } else {
                    d->desig_is_field = true;
                    d->desig_field = parse_peek(p)->spelling;
                    p->pos++;
                }
            }
            NodeVec_push(&desigs, d);
        }
        if (desigs.len)
            parse_expect_punct(p, PUNCT_ASSIGN, "after designator list");

        item = parse_initializer(p);
        if (item && desigs.len) {
            item->ndesignators = (u32)desigs.len;
            item->designators = arena_alloc(
                p->arena, desigs.len * sizeof(AstNode *), _Alignof(AstNode *));
            memcpy(item->designators, desigs.data,
                   desigs.len * sizeof(AstNode *));
        }
        NodeVec_free(&desigs);
        if (item)
            NodeVec_push(&items, item);

        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
        if (parse_at_punct(p, PUNCT_RBRACE))
            break; /* trailing comma */
    }
    parse_expect_punct(p, PUNCT_RBRACE, "at end of initializer list");
    n->nitems = (u32)items.len;
    if (items.len) {
        n->items = arena_alloc(p->arena, items.len * sizeof(AstNode *),
                               _Alignof(AstNode *));
        memcpy(n->items, items.data, items.len * sizeof(AstNode *));
    }
    NodeVec_free(&items);
    return n;
}

AstNode *parse_braced_initializer(Parser *p)
{
    return parse_initializer(p);
}

/* --- type-names (6.7.7) -------------------------------------------------- */

/* The cast-vs-parenthesized-expression decision. The caller has already
 * consumed the '('; a type-name starts with a specifier keyword or with a
 * VISIBLE TYPEDEF NAME, and that second half is the whole ambiguity:
 * `(x)(y)` is a call when x is an ordinary identifier and a cast when it
 * is a typedef. */
bool parse_at_type_name(Parser *p)
{
    return parse_at_decl_specs(p);
}

AstType *parse_type_name(Parser *p)
{
    SpecSoup s;
    const Token *start = parse_peek(p);
    AstType *base;
    const char *name = NULL;
    AstType *ty;

    if (!parse_decl_specs(p, &s)) {
        parse_error(p, start, "expected a type name but found '%s'",
                    tok_desc(start));
        base = ast_type_new(p->arena, ATY_BASE, start->span);
        base->base = ABT_INT;
        return base;
    }
    base = ast_type_new(p->arena, ATY_BASE, start->span);
    base->base = soup_resolve(p, &s, start);
    soup_fill_identity(base, &s);
    base->quals = s.quals;
    if (s.storage)
        parse_error(p, start, "a type name cannot have a storage class");
    /* Abstract declarator: the name is optional, and a type-name that DOES
     * name something is a constraint violation, not a parse failure. */
    ty = parse_declarator(p, base, &name, true);
    if (name)
        parse_error(p, start, "a type name cannot declare '%s'", name);
    return ty;
}

/* --- declarations ------------------------------------------------------- */

static bool type_is_function(const AstType *t)
{
    return t && t->kind == ATY_FUNC;
}

/* THE unknown-type heuristic — the single biggest source of cascade in a C
 * parser. One missing header turns `u32 x;` into "expected a declaration",
 * then the declarator, the initializer, and every later use of `u32` all
 * report too. gcc and clang both special-case it and so do we: diagnose
 * ONCE, then treat the name as a typedef for an error type so the rest of
 * the file parses normally.
 *
 * The name is recorded in TWO places on purpose: `unknown_types` so it is
 * never diagnosed twice, and the FILE scope as a typedef so every later
 * use — including inside functions declared earlier — simply resolves. */
static bool is_known_unknown_type(Parser *p, const char *name)
{
    ScopeEntry *e;

    for (e = p->unknown_types; e; e = e->next)
        if (e->name == name) /* interned: pointer compare */
            return true;
    return false;
}

static void declare_unknown_type(Parser *p, const Token *id)
{
    ScopeEntry *e;
    ParseScope *file_scope;

    e = arena_alloc(p->arena, sizeof(ScopeEntry), _Alignof(ScopeEntry));
    e->name = id->spelling;
    e->is_typedef = true;
    e->next = p->unknown_types;
    p->unknown_types = e;

    /* Register in FILE scope, not the current one: the error type must
     * outlive the block that first mentioned it, or a second function
     * using the same name re-diagnoses. */
    for (file_scope = p->scope; file_scope->parent;
         file_scope = file_scope->parent)
        ;
    e = arena_alloc(p->arena, sizeof(ScopeEntry), _Alignof(ScopeEntry));
    e->name = id->spelling;
    e->is_typedef = true;
    e->next = file_scope->ordinary;
    file_scope->ordinary = e;
}

/* Does an identifier here start a declaration whose type name we do not
 * know? At FILE scope everything is a declaration, so any identifier in
 * specifier position qualifies. At BLOCK scope it must be the unambiguous
 * `ident ident` shape: `x * y;` is multiplication when x is a variable,
 * and guessing otherwise would break valid code — sema diagnoses the
 * undeclared `x` there instead. */
bool parse_at_unknown_type(Parser *p)
{
    const Token *t = parse_peek(p);

    if (t->kind != TOK_IDENT || parse_is_typedef_name(p, t->spelling))
        return false;
    if (p->scope_depth == 0)
        return true;
    return parse_peek_n(p, 1)->kind == TOK_IDENT;
}

/* Diagnoses (once) and registers. Returns the token consumed. */
/* Scans VISIBLE TYPEDEF NAMES for a close spelling. Restricting the
 * search to typedefs is not an optimization — it is the correctness
 * requirement: suggesting a variable or a struct tag where a type name
 * belongs would send the reader somewhere the name cannot legally go.
 * Nearest wins; ties break toward the earliest declaration. */
static const char *suggest_type_name(Parser *p, const char *typo)
{
    size_t tlen = strlen(typo);
    const char *best = NULL;
    unsigned best_d = 0;
    ParseScope *sc;

    for (sc = p->scope; sc; sc = sc->parent) {
        ScopeEntry *e;

        for (e = sc->ordinary; e; e = e->next) {
            unsigned d;

            if (!e->is_typedef || !e->name)
                continue;
            if (!dlev_is_suggestion(typo, tlen, e->name, strlen(e->name), &d))
                continue;
            /* `<=` breaks ties toward the EARLIEST declaration: the
             * chain runs newest-first, so the last match at a given
             * distance is the oldest. Two candidates one edit away is
             * common (`gj` reaches both `gi` and `ga`), and picking by
             * declaration order is both deterministic and the answer a
             * reader expects. */
            if (!best || d <= best_d) {
                best = e->name;
                best_d = d;
            }
        }
    }
    return best;
}

static void take_unknown_type(Parser *p, SpecSoup *s)
{
    const Token *id = parse_peek(p);

    if (!is_known_unknown_type(p, id->spelling)) {
        const char *did_you_mean = suggest_type_name(p, id->spelling);

        if (did_you_mean)
            parse_error(p, id, "unknown type name '%s'; did you mean '%s'?",
                        id->spelling, did_you_mean);
        else
            parse_error(p, id, "unknown type name '%s'", id->spelling);
        declare_unknown_type(p, id);
    }
    /* Recover AS IF it were a typedef: the declarator, the initializer and
     * every later use then parse normally, which is the entire payoff. */
    s->n_other++;
    s->other_base = ABT_TYPEDEF;
    s->typedef_name = id->spelling;
    s->saw_any = true;
    p->pos++;
}

static bool looks_like_implicit_int_decl(Parser *p)
{
    const Token *t = parse_peek(p);
    const Token *n = parse_peek_n(p, 1);

    if (t->kind == TOK_PUNCT &&
        (t->punct == PUNCT_STAR || t->punct == PUNCT_LPAREN))
        return true;
    if (t->kind != TOK_IDENT || n->kind != TOK_PUNCT)
        return false;
    return n->punct == PUNCT_LPAREN || n->punct == PUNCT_LBRACKET ||
           n->punct == PUNCT_ASSIGN || n->punct == PUNCT_COMMA ||
           n->punct == PUNCT_SEMI;
}

static void warn_implicit_int(Parser *p, Span sp, const char *name)
{
    if (std_is_c99_or_later(p->lang->std))
        warn_pedwarn_at(p->lang->warnings, WARN_IMPLICIT_INT, sp,
                        "type defaults to 'int' in declaration of '%s'",
                        name ? name : "<anonymous>");
    else if (warn_explicitly_enabled(p->lang->warnings, WARN_IMPLICIT_INT, sp))
        warn_at(p->lang->warnings, WARN_IMPLICIT_INT, sp,
                "type defaults to 'int' in declaration of '%s'",
                name ? name : "<anonymous>");
}

AstNode *parse_declaration(Parser *p, bool allow_func_def)
{
    SpecSoup s;
    const Token *start = parse_peek(p);
    AstBaseType base_kind;
    AstNode *first = NULL;
    NodeVec siblings = {NULL, 0, 0};
    bool implicit_int = false;

    if (parse_at_kw(p, KW_STATIC_ASSERT))
        return parse_static_assert(p);
    if (parse_eat_punct(p, PUNCT_SEMI))
        return ast_new(p->arena, AST_EMPTY_DECL, start->span);

    if (!parse_decl_specs(p, &s)) {
        if (looks_like_implicit_int_decl(p)) {
            memset(&s, 0, sizeof(s));
        } else if (parse_at_unknown_type(p)) {
            memset(&s, 0, sizeof(s));
            take_unknown_type(p, &s);
        } else {
            AstNode *bad;

            parse_error(p, start, "expected a declaration but found '%s'",
                        tok_desc(start));
            bad = parse_error_node(p, start->span);
            parse_sync(p, SYNC_DECL);
            return bad;
        }
    }
    base_kind = soup_resolve(p, &s, start);
    if (base_kind == ABT_NONE) {
        implicit_int = true;
        base_kind = ABT_INT;
    }

    if (parse_eat_punct(p, PUNCT_SEMI)) {
        AstNode *n = ast_new(p->arena, AST_EMPTY_DECL, start->span);
        n->type = ast_type_new(p->arena, ATY_BASE, start->span);
        n->type->base = base_kind;
        soup_fill_identity(n->type, &s);
        n->cgf_attrs = parse_cgf_attrs_concat(p, s.cgf_attrs, NULL);
        gnu_attrs_merge(&n->gnu, &s.gnu);
        /* No declarator followed, so a tag here is a FORWARD DECLARATION
         * rather than a use — and 6.7.2.3p7 makes that introduce a new tag
         * in this scope even when an outer one is visible. */
        if (s.record && !s.record->is_definition)
            s.record->is_forward_decl = true;
        if (base_kind != ABT_RECORD && base_kind != ABT_ENUM)
            warn_at(p->lang->warnings, WARN_EMPTY_DECLARATION, start->span,
                    "declaration does not declare anything");
        return n;
    }

    for (;;) {
        AstNode *n = ast_new(p->arena, AST_DECL, parse_peek(p)->span);
        AstType *bt = ast_type_new(p->arena, ATY_BASE, start->span);

        bt->base = base_kind;
        soup_fill_identity(bt, &s);
        bt->quals = s.quals;
        n->storage = s.storage;
        n->func_specs = s.func_specs;
        n->has_alignas = s.has_alignas;
        n->alignas_expr = s.alignas_expr;
        n->alignas_type = s.alignas_type;
        n->type = parse_declarator(p, bt, &n->name, false);
        n->cgf_attrs = parse_cgf_attrs_concat(p, s.cgf_attrs, NULL);
        gnu_attrs_merge(&n->gnu, &s.gnu);
        /* The asm label sits between the declarator and any attributes, and
         * gcc accepts attributes on either side of it, so both are read in a
         * loop rather than in a fixed order. */
        while (parse_at_kw(p, KW_ATTRIBUTE) || parse_at_kw(p, KW_ATTRIBUTE2) ||
               parse_at_asm_label(p)) {
            if (parse_at_asm_label(p))
                parse_asm_label(p, &n->gnu);
            else
                n->cgf_attrs = parse_cgf_attrs_concat(
                    p, n->cgf_attrs, parse_cgf_attributes(p, &n->gnu));
        }
        if (implicit_int)
            warn_implicit_int(p, n->span, n->name);

        /* DECLARATION POINT (6.2.1p7): the name enters scope as soon as
         * ITS declarator completes — before any initializer, and before
         * the next declarator. This is what makes `typedef int T; T T;`
         * declare a variable T while the specifier still meant the
         * typedef, and what makes the following `T x;` an error. */
        parse_scope_declare(p, n->name, (s.storage & AST_SC_TYPEDEF) != 0);

        /* A function type followed by a brace where a definition is NOT
         * allowed is a NESTED FUNCTION, which gcc implements with an
         * executable trampoline on the stack. Refused, and named: without
         * this the declarator simply ran out and reported "expected ';'
         * after declaration", which tells the reader nothing about why. */
        if (!allow_func_def && type_is_function(n->type) &&
            parse_at_punct(p, PUNCT_LBRACE)) {
            parse_error(p, parse_peek(p),
                        "nested functions are not supported: gcc implements "
                        "them with an executable trampoline on the stack, "
                        "which is outside the v0.1.0 scope contract "
                        "(docs/gnu-extensions.md)");
            n->poisoned = true;
        }

        /* A function DEFINITION: `{` (or a K&R declaration list) follows. */
        if (allow_func_def && !first && type_is_function(n->type) &&
            (parse_at_punct(p, PUNCT_LBRACE) || parse_at_decl_specs(p))) {
            NodeVec krs = {NULL, 0, 0};
            u32 pi;

            n->kind = AST_FUNC_DEF;
            /* parse_param_list already closed the prototype scope (it must
             * — a prototype's parameter names die at the ')'). A
             * DEFINITION needs them alive again, and 6.2.1p4 puts them in
             * the SAME scope as the body's outermost block, so we reopen
             * one scope here and re-declare the parameters into it rather
             * than trying to keep the prototype's scope alive across two
             * functions. The K&R declaration list belongs in this scope
             * too — it declares those same parameters. */
            parse_scope_enter(p);
            p->scope_depth++;
            for (pi = 0; pi < n->type->nparams; pi++)
                if (n->type->params[pi].name)
                    parse_scope_declare(p, n->type->params[pi].name, false);

            while (!parse_at_punct(p, PUNCT_LBRACE) &&
                   parse_peek(p)->kind != TOK_EOF) {
                u32 kr_before = p->pos;
                AstNode *kd = parse_declaration(p, false);

                if (kd)
                    NodeVec_push(&krs, kd);
                /* Terminate on lack of PROGRESS, never on a NULL return:
                 * parse_declaration now yields a poisoned error node where
                 * it used to yield NULL, and a loop that trusted NULL span
                 * forever on a GNU-attribute pair that consumed nothing
                 * (see tests/programs/parse/err_attr_hang.c). Every loop
                 * over a recovering production needs its own cursor
                 * guard. */
                if (p->pos == kr_before)
                    break;
            }
            n->nkr_decls = (u32)krs.len;
            if (krs.len) {
                if (!n->type->is_kr_list)
                    parse_error(p, start,
                                "declaration list is only allowed with a "
                                "K&R identifier list");
                n->kr_decls = arena_alloc(p->arena, krs.len * sizeof(AstNode *),
                                          _Alignof(AstNode *));
                memcpy(n->kr_decls, krs.data, krs.len * sizeof(AstNode *));
            }
            NodeVec_free(&krs);
            if (parse_at_punct(p, PUNCT_LBRACE))
                n->body = parse_func_body(p);
            else
                parse_error(p, parse_peek(p), "expected a function body");
            p->scope_depth--;
            parse_scope_leave(p);
            return n;
        }

        if (n->type && n->type->is_kr_list) {
            /* An identifier list is only legal in a DEFINITION. */
            parse_error(p, start,
                        "parameter names (without types) are only allowed "
                        "in a function definition");
        }
        if (parse_eat_punct(p, PUNCT_ASSIGN))
            n->init = parse_initializer(p);

        if (!first)
            first = n;
        else
            NodeVec_push(&siblings, n);
        if (!parse_eat_punct(p, PUNCT_COMMA))
            break;
    }
    parse_expect_punct(p, PUNCT_SEMI, "after declaration");

    if (first && siblings.len) {
        first->nitems = (u32)siblings.len;
        first->items = arena_alloc(p->arena, siblings.len * sizeof(AstNode *),
                                   _Alignof(AstNode *));
        memcpy(first->items, siblings.data, siblings.len * sizeof(AstNode *));
    }
    NodeVec_free(&siblings);
    return first;
}

void parse_init(Parser *p, const TokenList *tl, Preprocessor *pp, DiagCtx *dc,
                Arena *arena, const LangOpts *lang)
{
    memset(p, 0, sizeof(*p));
    p->toks = tl->toks;
    p->ntoks = tl->n;
    p->pp = pp;
    p->dc = dc;
    p->arena = arena;
    p->lang = lang;
    parse_scope_enter(p); /* file scope */
}

AstNode *parse_translation_unit(Parser *p)
{
    AstNode *tu = ast_new(p->arena, AST_TRANSLATION_UNIT, parse_peek(p)->span);
    NodeVec decls = {NULL, 0, 0};

    while (parse_peek(p)->kind != TOK_EOF) {
        u32 before = p->pos;
        AstNode *d;

        /* The cap latched: stop producing work nobody will read. The
         * driver turns the latch into exit 1, so nothing here exits. */
        if (diag_error_limit_reached(p->dc))
            break;
        /* Bound the panic window to one top-level declaration. */
        p->recovering = false;
        /* File-scope basic asm: `asm("...");` between declarations. It has
         * no operands by definition -- there is nothing at file scope for
         * an operand to name -- so the body parser's basic path handles it
         * and anything else reports there. gcc emits the string verbatim
         * into the output, which is how musl's crt files place their entry
         * stubs. */
        if (parse_at_kw(p, KW_ASM) || parse_at_kw(p, KW_ALT_ASM) ||
            parse_at_kw(p, KW_ALT_ASM2)) {
            AstNode *a = ast_new(p->arena, AST_STMT_ASM, parse_peek(p)->span);

            p->pos++;
            if (parse_asm_body(p, a)) {
                parse_expect_punct(p, PUNCT_SEMI, "after file-scope asm");
                if (!a->asm_basic)
                    parse_error(p, parse_peek(p),
                                "file-scope asm takes no operands");
                NodeVec_push(&decls, a);
            } else {
                p->pos++;
            }
            continue;
        }
        d = parse_declaration(p, true);

        if (d)
            NodeVec_push(&decls, d);
        if (p->pos == before) {
            /* No progress: skip a token so a malformed input cannot spin.
             * Real recovery is Sprint 11's. */
            p->pos++;
        }
    }
    tu->ndecls = (u32)decls.len;
    if (decls.len) {
        tu->decls = arena_alloc(p->arena, decls.len * sizeof(AstNode *),
                                _Alignof(AstNode *));
        memcpy(tu->decls, decls.data, decls.len * sizeof(AstNode *));
    }
    NodeVec_free(&decls);
    return tu;
}
