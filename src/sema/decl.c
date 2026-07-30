#include <string.h>

#include "sema/sema.h"

/* Declarations: AST types to semantic Types, tag scoping, linkage,
 * redeclaration merging, and enum completion.
 *
 * Nothing here computes a size. Every path that would need one routes
 * through sema_unimplemented naming Sprint 14, so a missing capability is
 * always an error and never a wrong answer. */

static Type *type_from_ast(Sema *s, const AstType *at, Span span);
static u64 check_alignas(Sema *s, AstNode *d, Type *type);

/* --- the enumerator constant evaluator ----------------------------------- */

/* Enumerator values are needed HERE — the underlying type is chosen from
 * the value range and `previous + 1` needs the previous value — but the
 * general constant-expression evaluator is Sprint 15's. So this is a
 * deliberately minimal integer folder covering what enumerator lists
 * actually contain: literals, character constants, other enum constants,
 * and the arithmetic between them. Anything richer hard-errors naming
 * Sprint 15 rather than guessing a value. */
static bool enum_fold(Sema *s, AstNode *e, i64 *out);

static bool enum_fold_binary(Sema *s, AstNode *e, i64 *out)
{
    i64 l, r;

    if (!enum_fold(s, e->lhs, &l) || !enum_fold(s, e->rhs, &r))
        return false;
    switch (e->op) {
    case PUNCT_PLUS:
        *out = (i64)((u64)l + (u64)r);
        return true;
    case PUNCT_MINUS:
        *out = (i64)((u64)l - (u64)r);
        return true;
    case PUNCT_STAR:
        *out = (i64)((u64)l * (u64)r);
        return true;
    case PUNCT_SLASH:
    case PUNCT_PERCENT:
        if (r == 0) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, e->span,
                      "division by zero in an enumerator value");
            return false;
        }
        *out = e->op == PUNCT_SLASH ? l / r : l % r;
        return true;
    case PUNCT_SHL:
        *out = (i64)((u64)l << (r & 63));
        return true;
    case PUNCT_SHR:
        *out = l >> (r & 63);
        return true;
    case PUNCT_AMP:
        *out = l & r;
        return true;
    case PUNCT_PIPE:
        *out = l | r;
        return true;
    case PUNCT_CARET:
        *out = l ^ r;
        return true;
    case PUNCT_LT:
        *out = l < r;
        return true;
    case PUNCT_GT:
        *out = l > r;
        return true;
    case PUNCT_LE:
        *out = l <= r;
        return true;
    case PUNCT_GE:
        *out = l >= r;
        return true;
    case PUNCT_EQEQ:
        *out = l == r;
        return true;
    case PUNCT_NOTEQ:
        *out = l != r;
        return true;
    case PUNCT_AMPAMP:
        *out = l && r;
        return true;
    case PUNCT_PIPEPIPE:
        *out = l || r;
        return true;
    default:
        sema_unimplemented(s, e->span, "this operator in a constant expression",
                           15);
        return false;
    }
}

static bool enum_fold(Sema *s, AstNode *e, i64 *out)
{
    if (!e)
        return false;
    switch (e->kind) {
    case AST_ERROR:
        return false; /* already diagnosed: stay silent (Sprint 11) */
    case AST_EXPR_INT:
    case AST_EXPR_CHAR:
        *out = (i64)e->tok->int_val;
        return true;
    case AST_EXPR_PAREN:
        return enum_fold(s, e->lhs, out);
    case AST_EXPR_IDENT: {
        Symbol *sym = scope_lookup(s->scope, e->name, NS_ORDINARY);

        if (sym && sym->kind == SYM_ENUM_CONST) {
            *out = sym->enum_value;
            return true;
        }
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, e->span,
                  "'%s' is not a constant expression", e->name);
        return false;
    }
    case AST_EXPR_UNARY: {
        i64 v;

        if (e->is_postfix || !enum_fold(s, e->lhs, &v))
            return false;
        switch (e->op) {
        case PUNCT_PLUS:
            *out = v;
            return true;
        case PUNCT_MINUS:
            *out = (i64)(0ull - (u64)v);
            return true;
        case PUNCT_TILDE:
            *out = ~v;
            return true;
        case PUNCT_BANG:
            *out = !v;
            return true;
        default:
            sema_unimplemented(s, e->span,
                               "this operator in a constant expression", 15);
            return false;
        }
    }
    case AST_EXPR_BINARY:
        return enum_fold_binary(s, e, out);
    case AST_EXPR_COND: {
        i64 c;

        if (!enum_fold(s, e->lhs, &c))
            return false;
        return enum_fold(s, c ? e->mid : e->rhs, out);
    }
    case AST_EXPR_SIZEOF:
    case AST_EXPR_ALIGNOF: {
        /* Sprint 14 landed layout, so these fold. The operand type is
         * whichever of the two forms was written: a type-name, or the
         * type of an unevaluated expression. */
        Type *t = e->type ? sema_type_from_ast(s, e->type, e->span)
                          : (e->lhs ? e->lhs->sem_type : NULL);

        if (!t && e->lhs) {
            /* The operand was not typed yet (a constant context reached
             * before expression sema); type it now. */
            e->lhs = sema_expr(s, e->lhs);
            t = e->lhs->sem_type;
        }
        if (!t)
            return false;
        if (!layout_is_complete_for_size(t)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, e->span,
                      "invalid application of '%s' to incomplete type '%s'",
                      e->kind == AST_EXPR_SIZEOF ? "sizeof" : "_Alignof",
                      type_to_str(s->arena, t));
            return false;
        }
        *out = e->kind == AST_EXPR_SIZEOF ? (i64)layout_of(s, t).size
                                          : (i64)layout_of(s, t).align;
        return true;
    }
    default:
        sema_unimplemented(s, e->span, "this form of constant expression", 15);
        return false;
    }
}

/* --- tags ---------------------------------------------------------------- */

static TagDecl *tag_new(Sema *s, const char *name, TypeKind kind, Span span)
{
    TagDecl *tag = arena_alloc(s->arena, sizeof(TagDecl), _Alignof(TagDecl));

    memset(tag, 0, sizeof(*tag));
    tag->name = name;
    tag->kind = kind;
    tag->span = span;
    tag->type = type_tag(s->arena, tag);
    return tag;
}

static const char *tag_kw(TypeKind k)
{
    return k == TY_STRUCT ? "struct" : k == TY_UNION ? "union" : "enum";
}

/* Resolves the tag named by a record/enum AST node, applying 6.7.2.3's
 * scoping rules. `is_definition` means a body follows. */
static TagDecl *resolve_tag(Sema *s, const AstNode *rec, TypeKind kind,
                            Span span)
{
    const char *name = rec && rec->tag ? rec->tag : NULL;
    bool is_def = rec && rec->is_definition;
    Symbol *found;

    if (!name) {
        /* Anonymous: always a fresh tag, never looked up. */
        return tag_new(s, NULL, kind, span);
    }

    if (is_def) {
        /* A body COMPLETES a tag declared in the SAME scope, and declares
         * a fresh one in an inner scope — that is what makes an inner
         * `struct S { ... }` a different type from the outer S. */
        found = scope_lookup_local(s->scope, name, NS_TAG);
        if (found && found->tag && found->tag->kind == kind &&
            !found->tag->complete)
            return found->tag;
        if (found && found->tag && found->tag->kind == kind &&
            found->tag->complete) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span, "redefinition of '%s %s'",
                      tag_kw(kind), name);
            diag_emit(s->dc, DIAG_NOTE, found->tag->span,
                      "previous definition is here");
            return tag_new(s, name, kind, span);
        }
        if (found && found->tag && found->tag->kind != kind) {
            /* struct/union/enum share ONE namespace, so this collides. */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "'%s' defined as wrong kind of tag", name);
            diag_emit(s->dc, DIAG_NOTE, found->tag->span,
                      "previous declaration is a '%s'",
                      tag_kw(found->tag->kind));
            return tag_new(s, name, kind, span);
        }
        goto declare_new;
    }

    /* A bare `struct S;` ALWAYS introduces a new incomplete tag in the
     * current scope, hiding any outer one (6.7.2.3p7) — that is the idiom
     * for a scoped forward declaration. A USE (`struct S *p;`) instead
     * refers to a visible S if there is one. */
    if (rec && !rec->is_definition && rec->tag && rec->is_forward_decl) {
        found = scope_lookup_local(s->scope, name, NS_TAG);
        if (found && found->tag && found->tag->kind == kind)
            return found->tag; /* repeating it in the same scope is fine */
        goto declare_new;
    }

    found = scope_lookup(s->scope, name, NS_TAG);
    if (found && found->tag) {
        if (found->tag->kind != kind) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "'%s' defined as wrong kind of tag", name);
            diag_emit(s->dc, DIAG_NOTE, found->tag->span,
                      "previous declaration is a '%s'",
                      tag_kw(found->tag->kind));
            return tag_new(s, name, kind, span);
        }
        return found->tag;
    }

declare_new: {
    TagDecl *tag = tag_new(s, name, kind, span);
    Symbol *sym = sym_new(s, name, SYM_TAG, NS_TAG, tag->type, span);

    sym->tag = tag;
    scope_declare(s, sym);
    /* A tag first seen inside a parameter list dies at the ')', so no
     * caller can ever name it. gcc warns; matching the wording matters
     * because this is a real bug in real code. */
    if (s->scope->kind == SCOPE_PROTO)
        diag_emit(s->dc, DIAG_WARNING, span,
                  "'%s %s' declared inside parameter list will not be "
                  "visible outside of this definition or declaration",
                  tag_kw(kind), name);
    return tag;
}
}

static void add_member(Sema *s, TagDecl *tag, Member **last, const AstNode *m,
                       bool is_last_decl);

static void complete_struct(Sema *s, TagDecl *tag, const AstNode *rec)
{
    Member *last = NULL;
    u32 i;

    for (i = 0; i < rec->nmembers; i++) {
        const AstNode *m = rec->members[i];
        u32 j;

        if (!m || m->kind == AST_STATIC_ASSERT)
            continue;
        /* `int i, j, k;` is ONE declaration node whose siblings hang off
         * items[] — walking only the top node registered `i` and silently
         * dropped `j` and `k`. Found by the c-testsuite differential. */
        add_member(s, tag, &last, m, i + 1 == rec->nmembers && m->nitems == 0);
        for (j = 0; j < m->nitems; j++)
            add_member(s, tag, &last, m->items[j],
                       i + 1 == rec->nmembers && j + 1 == m->nitems);
    }
    tag->complete = true;
    {
        Member *mm;
        bool any_named = false;

        for (mm = tag->members; mm; mm = mm->next) {
            if (mm->name) {
                any_named = true;
                break;
            }
            /* An ANONYMOUS struct or union member has no name of its own
             * but contributes its members' names to the parent
             * (6.7.2.1p13), so it satisfies the non-empty requirement.
             * Only unnamed BITFIELDS contribute nothing — they are the
             * shape the rule is actually about. */
            if (!mm->is_bitfield && mm->type &&
                (mm->type->kind == TY_STRUCT || mm->type->kind == TY_UNION)) {
                any_named = true;
                break;
            }
        }
        /* 6.7.2.1 requires a non-empty member list, and an unnamed
         * bitfield does not make one: `struct { int :0; }` has no named
         * member and gcc gives it size ZERO as a GNU extension. Sizes of
         * zero break the "distinct objects have distinct addresses"
         * property that later passes assume, so this errors and names the
         * sprint that would relax it rather than inventing a size.
         * (`struct { int :5; }` is the same case — gcc's size 1 there is
         * incidental; both are the no-named-member extension.) */
        if (!any_named) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, rec->span,
                      "a struct or union must have at least one named member "
                      "(the GNU no-named-member extension lands in Sprint 55)");
        }
    }
}

static void add_member(Sema *s, TagDecl *tag, Member **last, const AstNode *m,
                       bool is_last_decl)
{
    {
        Member *mem;
        Type *mt;
        u64 member_align;

        if (!m)
            return;
        mt = type_from_ast(s, m->type, m->span);
        /* A member carries its own _Alignas constraints; the bitfield
         * case in particular has no address to align. The RESULT feeds
         * layout — an _Alignas on a member raises the record's alignment
         * too, so it cannot just be validated and dropped. */
        member_align = check_alignas(s, (AstNode *)m, mt);
        /* A member of incomplete type has no size, so the struct could
         * never be laid out — catchable now, without knowing any size.
         * The one exception is a flexible array member, which must be
         * LAST (its own constraints are Sprint 16's). */
        if (mt && mt->kind != TY_ERROR && !type_is_complete(mt) &&
            !(mt->kind == TY_ARRAY && is_last_decl)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, m->span,
                      "field '%s' has incomplete type '%s'",
                      m->name ? m->name : "<anonymous>",
                      type_to_str(s->arena, mt));
            mt = type_basic(TY_ERROR);
        }
        if (mt && mt->kind == TY_FUNC) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, m->span,
                      "field '%s' declared as a function",
                      m->name ? m->name : "<anonymous>");
            mt = type_basic(TY_ERROR);
        }

        /* Members have their OWN namespace, one per struct — so a member
         * may share a name with a variable, a tag, or a typedef, and only
         * a duplicate within this struct is an error. */
        if (m->name) {
            Member *e;

            for (e = tag->members; e; e = e->next)
                if (e->name == m->name) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, m->span,
                              "duplicate member '%s'", m->name);
                    diag_emit(s->dc, DIAG_NOTE, e->span,
                              "previous declaration is here");
                    break;
                }
        }

        mem = arena_alloc(s->arena, sizeof(Member), _Alignof(Member));
        memset(mem, 0, sizeof(*mem));
        mem->name = m->name;
        mem->type = mt;
        mem->is_bitfield = m->is_bitfield;
        mem->bitfield_width = m->bitfield_width;
        if (m->is_bitfield) {
            i64 wv = 0;

            if (m->bitfield_width && enum_fold(s, m->bitfield_width, &wv)) {
                u64 type_bits = layout_of(s, mt).size * 8;

                if (wv < 0) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, m->span,
                              "bit-field '%s' has a negative width",
                              m->name ? m->name : "<anonymous>");
                    wv = 0;
                } else if ((u64)wv > type_bits) {
                    /* A width wider than the DECLARED TYPE is a constraint
                     * violation; a width merely wider than the value range
                     * (int : 31) is fine. */
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, m->span,
                              "width of bit-field '%s' (%lld bits) exceeds "
                              "the width of its type (%llu bits)",
                              m->name ? m->name : "<anonymous>", (long long)wv,
                              (unsigned long long)type_bits);
                    wv = (i64)type_bits;
                }
                if (wv == 0 && m->name) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, m->span,
                              "named bit-field '%s' has zero width", m->name);
                    wv = 1;
                }
            }
            mem->bit_width = (u32)wv;
            if (!type_is_integer(mt) && mt->kind != TY_ERROR) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, m->span,
                          "bit-field '%s' has non-integral type '%s'",
                          m->name ? m->name : "<anonymous>",
                          type_to_str(s->arena, mt));
            }
        }
        mem->span = m->span;
        mem->align_override = member_align;
        if (*last)
            (*last)->next = mem;
        else
            tag->members = mem;
        *last = mem;
        tag->nmembers++;
    }
}

/* gcc's underlying-type ladder: the first of int, unsigned int, long,
 * unsigned long that represents every enumerator. A negative enumerator
 * forces a signed choice. The enum's underlying type is NOT the type of
 * its constants — those are `int` (6.7.2.2p3). */
static Type *enum_underlying(Sema *s, i64 lo, i64 hi, bool any_negative)
{
    IntWidths w = cgf_target_int_widths(s->target);
    i64 int_max = ((i64)1 << (w.int_bits - 1)) - 1;
    i64 int_min = -((i64)1 << (w.int_bits - 1));
    u64 uint_max = (w.int_bits >= 64) ? ~0ull : ((1ull << w.int_bits) - 1);
    i64 long_max = (w.long_bits >= 64) ? (i64)0x7fffffffffffffffLL
                                       : (((i64)1 << (w.long_bits - 1)) - 1);
    i64 long_min = (w.long_bits >= 64) ? (-(i64)0x7fffffffffffffffLL - 1)
                                       : -((i64)1 << (w.long_bits - 1));

    if (lo >= int_min && hi <= int_max)
        return type_basic(TY_INT);
    if (!any_negative && (u64)hi <= uint_max)
        return type_basic(TY_UINT);
    if (lo >= long_min && hi <= long_max)
        return type_basic(TY_LONG);
    return type_basic(TY_ULONG);
}

static void complete_enum(Sema *s, TagDecl *tag, const AstNode *rec)
{
    IntWidths w = cgf_target_int_widths(s->target);
    i64 int_max = ((i64)1 << (w.int_bits - 1)) - 1;
    i64 int_min = -((i64)1 << (w.int_bits - 1));
    i64 next = 0, lo = 0, hi = 0;
    bool any_negative = false;
    bool have_any = false;
    bool any_out_of_int = false;
    Symbol *before = s->scope->ordinary;
    Symbol *sym_it;
    u32 i;

    for (i = 0; i < rec->nmembers; i++) {
        const AstNode *m = rec->members[i];
        Symbol *prev;
        Symbol *sym;
        i64 value = next;

        if (!m)
            continue;
        if (m->init) {
            if (!enum_fold(s, m->init, &value))
                value = next; /* already diagnosed; keep going */
        }

        /* 6.7.2.2p2 makes an enumerator outside int's range a constraint
         * violation. gcc accepts it as an extension and gives the constant
         * the enum's type, so we pedwarn rather than reject — the value is
         * still usable and rejecting would break real code. */
        if (value > int_max || value < int_min) {
            any_out_of_int = true;
            /* gcc only warns under -pedantic here, so we do too — this is
             * a constraint the standard states and every real toolchain
             * relaxes. */
            if (s->lang->pedantic)
                diag_emit(s->dc, DIAG_WARNING, m->span,
                          "ISO C restricts enumerator values to range of "
                          "'int'");
        }

        prev = scope_lookup_local(s->scope, m->name, NS_ORDINARY);
        if (prev) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, m->span, "redeclaration of '%s'",
                      m->name);
            diag_emit(s->dc, DIAG_NOTE, prev->span,
                      "previous declaration is here");
        }
        /* Enum constants share the ORDINARY namespace with objects and
         * typedefs — that is why `enum { x }; int x;` collides. */
        sym = sym_new(s, m->name, SYM_ENUM_CONST, NS_ORDINARY,
                      type_basic(TY_INT), m->span);
        sym->enum_value = value;
        sym->defined = true;
        scope_declare(s, sym);

        if (!have_any) {
            lo = hi = value;
            have_any = true;
        } else {
            if (value < lo)
                lo = value;
            if (value > hi)
                hi = value;
        }
        if (value < 0)
            any_negative = true;

        /* The NEXT implicit value is previous + 1; overflowing there with
         * no explicit `=` has no defined value to give. */
        if (value == (i64)0x7fffffffffffffffLL) {
            if (i + 1 < rec->nmembers && rec->members[i + 1] &&
                !rec->members[i + 1]->init) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, rec->members[i + 1]->span,
                          "overflow in enumeration values");
            }
            next = 0;
        } else {
            next = value + 1;
        }
    }

    tag->enum_underlying = enum_underlying(s, lo, hi, any_negative);
    tag->complete = true;

    /* 6.7.2.2p3 gives enum CONSTANTS type `int` — but only the ones that
     * fit. gcc's extension for the rest is to give them the enum's own
     * type, which is observable: sizeof(B1) is 4 for an unsigned-int enum,
     * not 8. Retyping happens here because the underlying type is not
     * known until every enumerator has been seen. */
    if (any_out_of_int) {
        for (sym_it = s->scope->ordinary; sym_it && sym_it != before;
             sym_it = sym_it->next) {
            if (sym_it->kind != SYM_ENUM_CONST)
                continue;
            if (sym_it->enum_value > int_max || sym_it->enum_value < int_min)
                sym_it->type = tag->enum_underlying;
        }
    }
}

/* --- AST types to semantic types ----------------------------------------- */

static Type *base_type_from_ast(Sema *s, const AstType *at, Span span)
{
    switch (at->base) {
    case ABT_NONE:
    case ABT_INT:
        return type_basic(TY_INT);
    case ABT_VOID:
        return type_basic(TY_VOID);
    case ABT_CHAR:
        return type_basic(TY_CHAR);
    case ABT_SCHAR:
        return type_basic(TY_SCHAR);
    case ABT_UCHAR:
        return type_basic(TY_UCHAR);
    case ABT_SHORT:
        return type_basic(TY_SHORT);
    case ABT_USHORT:
        return type_basic(TY_USHORT);
    case ABT_UINT:
        return type_basic(TY_UINT);
    case ABT_LONG:
        return type_basic(TY_LONG);
    case ABT_ULONG:
        return type_basic(TY_ULONG);
    case ABT_LLONG:
        return type_basic(TY_LLONG);
    case ABT_ULLONG:
        return type_basic(TY_ULLONG);
    case ABT_FLOAT:
        return type_basic(TY_FLOAT);
    case ABT_DOUBLE:
        return type_basic(TY_DOUBLE);
    case ABT_LDOUBLE:
        return type_basic(TY_LDOUBLE);
    case ABT_BOOL:
        return type_basic(TY_BOOL);
    case ABT_RECORD:
    case ABT_ENUM: {
        TypeKind kind = at->base == ABT_ENUM                   ? TY_ENUM
                        : (at->record && at->record->is_union) ? TY_UNION
                                                               : TY_STRUCT;
        TagDecl *tag = resolve_tag(s, at->record, kind, span);

        if (at->record && at->record->is_definition && !tag->complete) {
            if (kind == TY_ENUM)
                complete_enum(s, tag, at->record);
            else
                complete_struct(s, tag, at->record);
        }
        return tag->type;
    }
    case ABT_TYPEDEF: {
        Symbol *sym =
            at->typedef_name
                ? scope_lookup(s->scope, at->typedef_name, NS_ORDINARY)
                : NULL;

        if (sym && sym->kind == SYM_TYPEDEF)
            return sym->type;
        /* The parser already diagnosed an unknown type name and recovered
         * as if it were a typedef; staying silent here honours Sprint 11's
         * one-mistake-one-diagnostic contract. */
        return type_basic(TY_ERROR);
    }
    }
    return type_basic(TY_ERROR);
}

static unsigned quals_from_ast(u32 q)
{
    unsigned out = 0;

    if (q & AST_QUAL_CONST)
        out |= CGF_QUAL_CONST;
    if (q & AST_QUAL_VOLATILE)
        out |= CGF_QUAL_VOLATILE;
    if (q & AST_QUAL_RESTRICT)
        out |= CGF_QUAL_RESTRICT;
    if (q & AST_QUAL_ATOMIC)
        out |= CGF_QUAL_ATOMIC;
    return out;
}

static Type *type_from_ast(Sema *s, const AstType *at, Span span)
{
    Type *inner;

    if (!at)
        return type_basic(TY_ERROR);

    switch (at->kind) {
    case ATY_BASE:
        return type_qualify(s->arena, base_type_from_ast(s, at, span),
                            quals_from_ast(at->quals));
    case ATY_PTR:
        inner = type_from_ast(s, at->next, span);
        return type_qualify(s->arena, type_ptr(s->arena, inner),
                            quals_from_ast(at->ptr_quals));
    case ATY_ARRAY: {
        Type *arr;

        inner = type_from_ast(s, at->next, span);
        if (inner && inner->kind == TY_FUNC) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span, "array of functions");
            inner = type_basic(TY_ERROR);
        } else if (inner && inner->kind != TY_ERROR &&
                   !type_is_complete(inner)) {
            /* An array of incomplete elements can never be laid out —
             * catchable without knowing a single size. */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "array has incomplete element type '%s'",
                      type_to_str(s->arena, inner));
            inner = type_basic(TY_ERROR);
        }
        arr = type_array(s->arena, inner);
        arr->size_expr = at->array_size;
        if (at->array_star)
            arr->is_vla = true;
        if (at->array_size) {
            i64 n;

            /* Reuse the enumerator folder: a literal bound is by far the
             * common case, and anything richer defers to Sprint 15 rather
             * than inventing a size. */
            if (enum_fold(s, at->array_size, &n)) {
                if (n < 0) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, span,
                              "array has a negative size");
                } else if (n == 0) {
                    diag_emit(s->dc, DIAG_WARNING, span,
                              "ISO C forbids zero-size arrays");
                    arr->has_size = true;
                    arr->size = 0;
                } else {
                    arr->has_size = true;
                    arr->size = (u64)n;
                }
            }
        }
        return type_qualify(s->arena, arr, quals_from_ast(at->array_quals));
    }
    case ATY_FUNC: {
        Type *fn;
        u32 i;

        inner = type_from_ast(s, at->next, span);
        if (inner && inner->kind == TY_FUNC) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "function returning a "
                      "function");
            inner = type_basic(TY_ERROR);
        } else if (inner && inner->kind == TY_ARRAY) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span, "function returning an array");
            inner = type_basic(TY_ERROR);
        }
        fn = type_func(s->arena, inner);
        fn->variadic = at->is_variadic;
        fn->has_proto = !at->has_no_params && !at->is_kr_list;
        fn->nparams = at->is_kr_list ? 0 : at->nparams;
        if (fn->nparams) {
            fn->params = arena_alloc(s->arena, fn->nparams * sizeof(Type *),
                                     _Alignof(Type *));
            /* Parameters are declared in a PROTOTYPE scope: anything a
             * parameter list introduces — including a struct tag — dies at
             * the ')'. */
            scope_push(s, SCOPE_PROTO);
            for (i = 0; i < fn->nparams; i++) {
                Type *pt =
                    type_from_ast(s, at->params[i].type, at->params[i].span);

                /* 6.7.6.3p7/p8: a parameter of array type is adjusted to
                 * pointer-to-element, and of function type to pointer-to
                 * -function. Doing it HERE means every later pass sees the
                 * adjusted type and no one re-derives the rule. */
                if (pt && pt->kind == TY_ARRAY)
                    pt = type_ptr(s->arena, pt->base);
                else if (pt && pt->kind == TY_FUNC)
                    pt = type_ptr(s->arena, pt);
                fn->params[i] = pt;
            }
            scope_pop(s);
        }
        return fn;
    }
    }
    return type_basic(TY_ERROR);
}

Type *sema_type_from_ast(Sema *s, const AstType *at, Span span)
{
    return type_from_ast(s, at, span);
}

/* --- linkage (6.2.2) ----------------------------------------------------- */

/* The table from the sprint file, reproduced in code. The only subtle row
 * is p4: a BLOCK-scope `extern` takes the linkage of a prior visible
 * declaration WITH LINKAGE if there is one, so
 *
 *     static int x;  void f(void) { extern int x; }
 *
 * gives the inner x INTERNAL linkage — the file-scope static wins. */
static Linkage linkage_for(Sema *s, const Symbol *prev, u32 storage,
                           bool is_func)
{
    bool file_scope = s->scope->kind == SCOPE_FILE;

    if (storage & AST_SC_TYPEDEF)
        return LINK_NONE;
    if (file_scope) {
        if (storage & AST_SC_STATIC)
            return LINK_INTERNAL;
        if (storage & AST_SC_EXTERN)
            return prev && prev->linkage != LINK_NONE ? prev->linkage
                                                      : LINK_EXTERNAL;
        return LINK_EXTERNAL; /* objects and functions alike */
    }
    /* Block scope. */
    if (storage & AST_SC_EXTERN)
        return prev && prev->linkage != LINK_NONE ? prev->linkage
                                                  : LINK_EXTERNAL;
    if (is_func)
        return LINK_EXTERNAL; /* a block-scope function declaration */
    return LINK_NONE;
}

/* --- redeclaration ------------------------------------------------------- */

static void merge_redeclaration(Sema *s, Symbol *prev, Symbol *cur, u32 storage)
{
    Type *composite;

    if (prev->kind != cur->kind &&
        (prev->kind == SYM_ENUM_CONST || cur->kind == SYM_ENUM_CONST)) {
        /* Enum constants share the ORDINARY namespace with objects and
         * typedefs and have NO linkage, so nothing may redeclare one. */
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, cur->span,
                  "redefinition of '%s' as a different kind of symbol",
                  cur->name);
        diag_emit(s->dc, DIAG_NOTE, prev->span, "previous declaration is here");
        return;
    }

    if (prev->kind == SYM_TYPEDEF || cur->kind == SYM_TYPEDEF) {
        if (prev->kind != cur->kind) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, cur->span,
                      "redefinition of '%s' as a different kind of symbol",
                      cur->name);
            diag_emit(s->dc, DIAG_NOTE, prev->span,
                      "previous declaration is here");
            return;
        }
        /* C11 allows redeclaring a typedef to the SAME type; C99 forbade
         * it outright, so that is a pedwarn rather than an error there. */
        if (!type_compatible(prev->type, cur->type)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, cur->span,
                      "typedef redefinition with different types ('%s' vs "
                      "'%s')",
                      type_to_str(s->arena, cur->type),
                      type_to_str(s->arena, prev->type));
            diag_emit(s->dc, DIAG_NOTE, prev->span,
                      "previous declaration is here");
        } else if (!std_is_c11_or_later(s->lang->std)) {
            diag_emit(s->dc, DIAG_WARNING, cur->span,
                      "redefinition of typedef '%s' is a C11 feature",
                      cur->name);
        }
        return;
    }

    if (prev->linkage == LINK_NONE && cur->linkage == LINK_NONE) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, cur->span,
                  "redeclaration of '%s' with "
                  "no linkage",
                  cur->name);
        diag_emit(s->dc, DIAG_NOTE, prev->span, "previous declaration is here");
        return;
    }

    /* 6.2.2p7: one identifier with BOTH internal and external linkage in a
     * translation unit is undefined. gcc errors when `static` follows a
     * non-static declaration, and ACCEPTS the reverse (`static int x;
     * extern int x;`) because there the extern picks up internal linkage
     * from the prior declaration. Match both halves. */
    if (prev->linkage != cur->linkage && cur->linkage == LINK_INTERNAL) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, cur->span,
                  "static declaration of '%s' follows non-static "
                  "declaration",
                  cur->name);
        diag_emit(s->dc, DIAG_NOTE, prev->span, "previous declaration is here");
        return;
    }

    if (!type_compatible(prev->type, cur->type)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, cur->span,
                  "conflicting types for '%s' ('%s' vs '%s')", cur->name,
                  type_to_str(s->arena, cur->type),
                  type_to_str(s->arena, prev->type));
        diag_emit(s->dc, DIAG_NOTE, prev->span, "previous declaration is here");
        return;
    }

    if (prev->defined && cur->defined) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, cur->span, "redefinition of '%s'",
                  cur->name);
        diag_emit(s->dc, DIAG_NOTE, prev->span, "previous definition is here");
        return;
    }

    /* The symbol's type becomes the COMPOSITE of the two (6.2.7p3), which
     * is how `int a[]; int a[10];` ends up as int[10] and how an
     * unprototyped declaration followed by a prototype keeps the
     * prototype. */
    composite = type_composite(s->arena, prev->type, cur->type);
    prev->type = composite;
    prev->defined = prev->defined || cur->defined;
    /* A tentative definition stops being tentative once a real one
     * appears; several tentatives are fine (6.9.2p2). */
    if (cur->defined)
        prev->tentative = false;
    else
        prev->tentative = prev->tentative || cur->tentative;
    (void)storage;
}

/* --- declarations -------------------------------------------------------- */

/* An array declared without a bound is COMPLETED by its initializer
 * (6.7.9p22): `int a[] = {1,2,3}` is int[3]. This needs no constant
 * evaluation for the common case — it is a count of top-level items — but
 * a designator moves the cursor, so `int a[] = {[5] = 1}` is int[6]. That
 * index does need folding, which the enumerator folder already does. */
static bool array_size_from_init(Sema *s, const AstNode *init, u64 *out)
{
    u64 pos = 0, high = 0;
    u32 i;

    if (!init)
        return false;
    if (init->kind == AST_EXPR_STRING) {
        /* `char s[] = "abc"` is char[4]. The lexer's nbytes is the
         * ENCODED CONTENT and excludes the terminator (verified against
         * --dump-tokens), so the +1 is the terminator the array must hold. */
        if (!init->tok)
            return false;
        *out = (u64)init->tok->str.nbytes + 1;
        return true;
    }
    if (init->kind != AST_INIT_LIST)
        return false;
    for (i = 0; i < init->nitems; i++) {
        const AstNode *item = init->items[i];

        if (item && item->ndesignators > 0) {
            const AstNode *d0 = item->designators[0];
            i64 idx;

            if (d0 && !d0->desig_is_field && d0->desig_index &&
                enum_fold(s, d0->desig_index, &idx) && idx >= 0)
                pos = (u64)idx;
        }
        pos++;
        if (pos > high)
            high = pos;
    }
    *out = high;
    return true;
}

static void sema_init_expr_list(Sema *s, AstNode *list)
{
    u32 i;

    if (!list || list->kind != AST_INIT_LIST)
        return;
    for (i = 0; i < list->nitems; i++) {
        AstNode *item = list->items[i];

        if (!item)
            continue;
        if (item->kind == AST_INIT_LIST)
            sema_init_expr_list(s, item);
        else
            list->items[i] = sema_expr(s, item);
    }
}

/* Types an initializer and checks it against the declared type. A BRACED
 * initializer needs the current-object algorithm to match elements to
 * subobjects, which is Sprint 15/16's; here its expressions are typed so
 * errors inside them still surface, and the element-by-element
 * compatibility check waits. A scalar initializer goes through the full
 * assignment constraints with ACTX_INIT so the wording is gcc's. */
static void sema_init_expr(Sema *s, Type *target, AstNode *d)
{
    AssignCtx ctx;

    if (!d->init)
        return;
    if (d->init->kind == AST_INIT_LIST) {
        u32 i;

        for (i = 0; i < d->init->nitems; i++) {
            AstNode *item = d->init->items[i];

            if (item && item->kind != AST_INIT_LIST)
                d->init->items[i] = sema_expr(s, item);
            else if (item)
                sema_init_expr_list(s, item);
        }
        return;
    }
    d->init = sema_expr(s, d->init);
    if (!target || target->kind == TY_ARRAY)
        return; /* `char s[] = "abc"` was handled by array completion */
    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = ACTX_INIT;
    conv_assignable(s, target, &d->init, ctx);
}

/* _Alignas (6.7.5). The constraints are the deliverable: it may not
 * WEAKEN an alignment, it may not appear on a typedef, a bitfield, a
 * parameter or a `register` object, and the value must be a power of two.
 * Returns the requested alignment, or 0 for "none". */
static u64 check_alignas(Sema *s, AstNode *d, Type *type)
{
    u64 natural;
    i64 want = 0;

    if (!d->has_alignas)
        return 0;

    if (d->storage & AST_SC_TYPEDEF) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "'_Alignas' cannot appear on a typedef");
        return 0;
    }
    if (d->storage & AST_SC_REGISTER) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "'_Alignas' cannot appear on a 'register' declaration");
        return 0;
    }
    if (d->is_bitfield) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "'_Alignas' cannot appear on a bit-field");
        return 0;
    }
    if (type && type->kind == TY_FUNC) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "'_Alignas' cannot appear on a function declaration");
        return 0;
    }

    if (d->alignas_type) {
        Type *at = sema_type_from_ast(s, d->alignas_type, d->span);

        if (!layout_is_complete_for_size(at)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "'_Alignas' requires a complete type");
            return 0;
        }
        want = (i64)layout_of(s, at).align;
    } else if (d->alignas_expr) {
        if (!enum_fold(s, d->alignas_expr, &want))
            return 0; /* already reported, naming Sprint 15 if unfoldable */
        /* 6.7.5p6: zero is IGNORED, which is not the same as an error. */
        if (want == 0)
            return 0;
        if (want < 0 || (want & (want - 1)) != 0) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "requested alignment %lld is not a power of two",
                      (long long)want);
            return 0;
        }
    }

    if (!type || !layout_is_complete_for_size(type))
        return (u64)want;
    natural = layout_of(s, type).align;
    if ((u64)want < natural) {
        /* An alignment may be RAISED, never weakened — a weaker one would
         * be a promise the object cannot keep. */
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "requested alignment %lld is weaker than the natural "
                  "alignment %llu of '%s'",
                  (long long)want, (unsigned long long)natural,
                  type_to_str(s->arena, type));
        return 0;
    }
    return (u64)want;
}

static void declare_one(Sema *s, AstNode *d)
{
    Type *type;
    Symbol *sym;
    Symbol *prev;
    Symbol *visible;
    bool is_func;
    bool file_scope = s->scope->kind == SCOPE_FILE;

    if (!d || !d->name)
        return;
    if (d->poisoned)
        return; /* Sprint 11: never diagnose about a poisoned subtree */

    type = type_from_ast(s, d->type, d->span);
    is_func = type && type->kind == TY_FUNC;
    (void)check_alignas(s, d, type);

    /* Completion event: an unsized array with an initializer takes its
     * size from that initializer. Doing it before the incomplete-type
     * check below is what makes `int a[] = {1,2,3};` legal. */
    if (type && type->kind == TY_ARRAY && !type->has_size && d->init) {
        u64 n;

        if (array_size_from_init(s, d->init, &n)) {
            Type *sized = type_array(s->arena, type->base);

            sized->quals = type->quals;
            sized->has_size = true;
            sized->size = n;
            sized->size_expr = type->size_expr;
            type = sized;
        }
    }

    if (d->storage & AST_SC_TYPEDEF) {
        sym = sym_new(s, d->name, SYM_TYPEDEF, NS_ORDINARY, type, d->span);
        sym->linkage = LINK_NONE;
    } else {
        sym = sym_new(s, d->name, is_func ? SYM_FUNC : SYM_VAR, NS_ORDINARY,
                      type, d->span);
        /* The prior declaration that p4 consults must be a VISIBLE one,
         * which for a block-scope extern means walking outward. */
        visible = scope_lookup(s->scope, d->name, NS_ORDINARY);
        sym->linkage = linkage_for(s, visible, d->storage, is_func);
        sym->defined = d->init != NULL || d->kind == AST_FUNC_DEF;
        /* A file-scope object with no initializer and no `extern` is a
         * TENTATIVE definition (6.9.2p2). Resolution to a zero-initialized
         * object at end of TU is Sprint 16's; recording it is ours. */
        if (file_scope && !is_func && !sym->defined &&
            !(d->storage & AST_SC_EXTERN))
            sym->tentative = true;
    }

    /* An object of incomplete type cannot be defined — but `extern int
     * a[];` and a tentative `int a[];` are both fine, so the check is on
     * definitions only. */
    if (!is_func && sym->kind != SYM_TYPEDEF && sym->defined && type &&
        type->kind != TY_ERROR && !type_is_complete(type)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "variable '%s' has incomplete type '%s'", d->name,
                  type_to_str(s->arena, type));
    }
    if (!is_func && sym->kind != SYM_TYPEDEF && type && type->kind == TY_VOID) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span, "variable '%s' declared void",
                  d->name);
    }

    prev = scope_lookup_local(s->scope, d->name, NS_ORDINARY);
    if (prev) {
        merge_redeclaration(s, prev, sym, d->storage);
        /* The initializer still has to be TYPED even when the declaration
         * merged into an earlier one, or its expressions never get
         * checked at all. */
        sema_init_expr(s, prev->type, d);
        return;
    }
    scope_declare(s, sym);
    sema_init_expr(s, sym->type, d);
}

static void sema_stmt(Sema *s, AstNode *st);

static void sema_decl(Sema *s, AstNode *d)
{
    u32 i;

    if (!d || d->poisoned)
        return;

    switch (d->kind) {
    case AST_EMPTY_DECL:
        /* `struct S { ... };` declares no object but DOES introduce or
         * complete a tag, which is the whole point of the line. */
        if (d->type)
            (void)type_from_ast(s, d->type, d->span);
        return;
    case AST_STATIC_ASSERT: {
        i64 v;

        /* _Static_assert takes an integer constant expression, which is
         * exactly what the enumerator folder handles; anything it cannot
         * fold already reports naming the sprint that will. */
        if (enum_fold(s, d->assert_expr, &v) && v == 0) {
            s->nerrors++;
            diag_emit(
                s->dc, DIAG_ERROR, d->span, "static assertion failed%s%.*s",
                d->assert_msg ? ": " : "",
                d->assert_msg ? (int)d->assert_msg->str.nbytes : 0,
                d->assert_msg ? (const char *)d->assert_msg->str.bytes : "");
        }
        return;
    }
    case AST_FUNC_DEF:
        declare_one(s, d);
        /* The body's declarations are ours; its STATEMENTS are typechecked
         * in Sprint 16, so we walk only far enough to exercise block
         * scope, shadowing, and the block-scope linkage rules. */
        scope_push(s, SCOPE_FUNC);
        {
            u32 pi;
            const AstType *ft = d->type;

            /* Parameters live in the same scope as the body's outermost
             * block (6.2.1p4). */
            if (ft && ft->kind == ATY_FUNC) {
                for (pi = 0; pi < ft->nparams; pi++) {
                    Symbol *ps;

                    if (!ft->params[pi].name)
                        continue;
                    ps = sym_new(s, ft->params[pi].name, SYM_VAR, NS_ORDINARY,
                                 type_from_ast(s, ft->params[pi].type,
                                               ft->params[pi].span),
                                 ft->params[pi].span);
                    ps->is_param = true;
                    ps->defined = true;
                    if (scope_lookup_local(s->scope, ps->name, NS_ORDINARY)) {
                        s->nerrors++;
                        diag_emit(s->dc, DIAG_ERROR, ps->span,
                                  "redefinition of parameter '%s'", ps->name);
                    } else {
                        scope_declare(s, ps);
                    }
                }
            }
            /* The body's outermost block shares this scope with the
             * parameters, so walk its items directly rather than through
             * sema_stmt (which would push another). */
            if (d->body && d->body->kind == AST_STMT_COMPOUND) {
                u32 bi;

                for (bi = 0; bi < d->body->nitems; bi++)
                    sema_stmt(s, d->body->items[bi]);
            } else if (d->body) {
                sema_stmt(s, d->body);
            }
        }
        scope_pop(s);
        return;
    case AST_DECL:
        declare_one(s, d);
        for (i = 0; i < d->nitems; i++)
            declare_one(s, d->items[i]);
        return;
    default:
        return;
    }
}

/* Statement WALK, not statement sema: we descend only to reach the
 * declarations inside, because block scope and the 6.2.2p4 linkage rule
 * are this sprint's business. Expression typing is Sprint 13 and
 * control-flow sema is Sprint 16. */
static void sema_stmt(Sema *s, AstNode *st)
{
    u32 i;

    if (!st || st->poisoned)
        return;
    switch (st->kind) {
    case AST_STMT_COMPOUND:
        /* Every compound statement is a scope. The function body's
         * outermost block is the exception — it SHARES the parameter
         * scope (6.2.1p4) — and the FUNC_DEF path handles that by calling
         * sema_block_items directly. Missing this made an inner
         * `struct T { ... }` a redefinition of an outer one, and made
         * `int s;` inside a nested block collide with an outer `s`. */
        scope_push(s, SCOPE_BLOCK);
        for (i = 0; i < st->nitems; i++)
            sema_stmt(s, st->items[i]);
        scope_pop(s);
        return;
    case AST_STMT_DECL:
        sema_decl(s, st->lhs);
        return;
    case AST_STMT_EXPR:
        st->lhs = sema_expr(s, st->lhs);
        return;
    case AST_STMT_RETURN:
        if (st->lhs)
            st->lhs = sema_expr(s, st->lhs);
        return;
    case AST_STMT_IF:
    case AST_STMT_SWITCH:
    case AST_STMT_WHILE:
    case AST_STMT_DO:
        if (st->lhs)
            st->lhs = sema_expr(s, st->lhs);
        sema_stmt(s, st->body);
        if (st->kind == AST_STMT_IF)
            sema_stmt(s, st->rhs);
        return;
    case AST_STMT_FOR:
        /* The init declaration scopes over the condition, the step AND the
         * body, and ends with the loop — so the scope opens here, around
         * everything, and the body's own block nests inside it. */
        scope_push(s, SCOPE_BLOCK);
        /* The for-init clause is either an expression statement or a bare
         * DECLARATION node — parse_for stores the declaration directly,
         * not wrapped in AST_STMT_DECL, so dispatching on kind is what
         * makes `for (int j = 0; j < 3; j++)` see its own `j`. */
        if (st->lhs) {
            if (st->lhs->kind == AST_DECL || st->lhs->kind == AST_EMPTY_DECL ||
                st->lhs->kind == AST_FUNC_DEF)
                sema_decl(s, st->lhs);
            else
                sema_stmt(s, st->lhs);
        }
        if (st->mid)
            st->mid = sema_expr(s, st->mid);
        if (st->rhs)
            st->rhs = sema_expr(s, st->rhs);
        sema_stmt(s, st->body);
        scope_pop(s);
        return;
    case AST_STMT_CASE:
        if (st->lhs)
            st->lhs = sema_expr(s, st->lhs);
        sema_stmt(s, st->body);
        return;
    case AST_STMT_LABEL:
    case AST_STMT_DEFAULT:
        sema_stmt(s, st->body);
        return;
    default:
        return;
    }
}

void sema_run(Sema *s, AstNode *tu)
{
    u32 i;

    if (!tu)
        return;
    for (i = 0; i < tu->ndecls; i++)
        sema_decl(s, tu->decls[i]);
}
