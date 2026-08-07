#include <string.h>

#include "sema/sema.h"
#include "warn/warn.h"

/* Declarations: AST types to semantic Types, tag scoping, linkage,
 * redeclaration merging, and enum completion.
 *
 * Nothing here computes a size. Every path that would need one routes
 * through sema_unimplemented naming Sprint 14, so a missing capability is
 * always an error and never a wrong answer. */

static Type *type_from_ast(Sema *s, const AstType *at, Span span);
static u64 check_alignas(Sema *s, AstNode *d, Type *type);

/* --- constant folding ---------------------------------------------------- */

/* Sprint 12 landed a minimal integer folder here because enum values were
 * needed before the real evaluator existed. Sprint 15 replaced it: every
 * constant context now goes through constexpr.c's single evaluator, so
 * there is exactly one set of rules and no chance of two folders
 * disagreeing about what is constant. */
static bool enum_fold(Sema *s, AstNode *e, i64 *out)
{
    return sema_require_ice(s, e, out, "this");
}

static void reject_nonfunction_attrs(Sema *s, const CgfAttr *attrs)
{
    const CgfAttr *a;

    for (a = attrs; a; a = a->next) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, a->span,
                  "attribute '%s' applies only to functions",
                  cgf_attr_name(a->kind));
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

/* The synthesized va_list (see sema.h for the shape's why). Member names are
 * interned so lowering's pointer-compare member lookup works.
 *
 * Two shapes, because the two psABIs disagree about more than field names.
 * SysV x86-64 keeps two 32-bit offsets that count UP from zero toward their
 * limits, plus a save-area base. Linux AAPCS64 keeps two save-area TOPS and
 * two NEGATIVE offsets that count up toward zero, so the argument at offset
 * o lives at `top + o`. Apple's arm64 has neither: its va_list is a plain
 * `char *`, which is Sprint 50's divergence.
 *
 * Both are wrapped in a one-element ARRAY so `va_list` decays to a pointer
 * on every use, which is what makes a callee advance its caller's cursor. */
static Type *va_list_sysv(Sema *s)
{
    static const char *const names[] = {"gp_offset", "fp_offset",
                                        "overflow_arg_area", "reg_save_area"};
    Span sp = {0};
    TagDecl *tag;
    Member *prev = NULL;
    Type *arr;
    int i;

    tag = tag_new(
        s,
        intern_str(s->interner, intern_cstr(s->interner, "__cgf_va_list_rec")),
        TY_STRUCT, sp);
    for (i = 3; i >= 0; i--) {
        Member *m = arena_alloc(s->arena, sizeof(Member), _Alignof(Member));

        memset(m, 0, sizeof(*m));
        m->name = intern_str(s->interner, intern_cstr(s->interner, names[i]));
        m->type = i < 2 ? type_basic(TY_UINT)
                        : type_ptr(s->arena, type_basic(TY_VOID));
        m->next = prev;
        prev = m;
    }
    tag->members = prev;
    tag->nmembers = 4;
    tag->complete = true;
    arr = type_array(s->arena, tag->type);
    arr->has_size = true;
    arr->size = 1;
    return arr;
}

/* Apple's arm64 va_list is a plain `char *`, and NOT wrapped in an array.
 *
 * That is a real semantic difference, not a simplification. The array wrapper
 * on the other two targets is what makes `va_list` decay to a pointer at
 * every use, so a callee handed one advances ITS CALLER's cursor. Apple's is
 * an ordinary pointer object: passing it copies the cursor, and a callee that
 * consumes arguments leaves the caller's copy untouched. Code relying on the
 * array behaviour is relying on an ABI accident, and `va_copy` exists for
 * exactly this reason.
 *
 * `char *` rather than `void *` because va_arg advances it by the argument's
 * size and pointer arithmetic on void is a GNU extension. */
static Type *va_list_apple(Sema *s)
{
    return type_ptr(s->arena, type_basic(TY_CHAR));
}

/* Field order and types are gcc's aarch64 `build_va_list`, verbatim:
 * __stack +0, __gr_top +8, __vr_top +16, __gr_offs +24, __vr_offs +28. The
 * offsets are SIGNED — negative is the whole design. */
static Type *va_list_aapcs64(Sema *s)
{
    static const char *const names[] = {"__stack", "__gr_top", "__vr_top",
                                        "__gr_offs", "__vr_offs"};
    Span sp = {0};
    TagDecl *tag;
    Member *prev = NULL;
    Type *arr;
    int i;

    tag = tag_new(
        s,
        intern_str(s->interner, intern_cstr(s->interner, "__cgf_va_list_rec")),
        TY_STRUCT, sp);
    for (i = 4; i >= 0; i--) {
        Member *m = arena_alloc(s->arena, sizeof(Member), _Alignof(Member));

        memset(m, 0, sizeof(*m));
        m->name = intern_str(s->interner, intern_cstr(s->interner, names[i]));
        m->type = i < 3 ? type_ptr(s->arena, type_basic(TY_VOID))
                        : type_basic(TY_INT);
        m->next = prev;
        prev = m;
    }
    tag->members = prev;
    tag->nmembers = 5;
    tag->complete = true;
    arr = type_array(s->arena, tag->type);
    arr->has_size = true;
    arr->size = 1;
    return arr;
}

Type *sema_va_list_type(Sema *s)
{
    if (s->va_list_type)
        return s->va_list_type;
    switch (s->target.kind) {
    case CGF_TARGET_ARM64_LINUX:
        s->va_list_type = va_list_aapcs64(s);
        break;
    case CGF_TARGET_ARM64_MACOS:
        s->va_list_type = va_list_apple(s);
        break;
    default:
        s->va_list_type = va_list_sysv(s);
        break;
    }
    return s->va_list_type;
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
        if (found && found->tag && found->tag->defining) {
            /* `struct T { struct T { ... } ... }`: the inner definition
             * must NOT complete the tag whose completion is underway —
             * that makes the struct a member of itself and every member
             * walk a cycle. */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span, "nested redefinition of '%s %s'",
                      tag_kw(kind), name);
            return tag_new(s, name, kind, span);
        }
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
        warn_at(s->lang->warnings, WARN_VISIBILITY, span,
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

    tag->defining = true;

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
    tag->defining = false;
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
        reject_nonfunction_attrs(s, m->cgf_attrs);
        mt = type_from_ast(s, m->type, m->span);
        /* A member carries its own _Alignas constraints; the bitfield
         * case in particular has no address to align. The RESULT feeds
         * layout — an _Alignas on a member raises the record's alignment
         * too, so it cannot just be validated and dropped. */
        member_align = check_alignas(s, (AstNode *)m, mt);

        /* Flexible array members (6.7.2.1p18). The standard form — an
         * incomplete array as the LAST member of a STRUCT that has other
         * named members — is legal and marks the tag. Every GNU variation
         * is an extension whose LOWERING we have not built, so each
         * hard-errors naming Sprint 55 rather than silently accepting
         * semantics Sprint 19 could not emit. (gcc accepts them quietly
         * and pedwarns; that is an extension-scope choice, documented.) */
        if (mt && mt->kind == TY_ARRAY && !mt->has_size && !mt->is_vla) {
            if (tag->kind == TY_UNION) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, m->span,
                          "a flexible array member in a union is a GNU "
                          "extension (lands in Sprint 55)");
                mt = type_basic(TY_ERROR);
            } else if (!is_last_decl) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, m->span,
                          "flexible array member not at end of struct");
                mt = type_basic(TY_ERROR);
            } else if (!tag->members) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, m->span,
                          "a flexible array member in a struct with no other "
                          "named members is a GNU extension (lands in "
                          "Sprint 55)");
                mt = type_basic(TY_ERROR);
            } else {
                tag->has_fam = true;
            }
        }
        /* A struct that ends in a FAM cannot itself be a member: the FAM
         * would be buried mid-object. A POINTER to one is fine. */
        if (mt && (mt->kind == TY_STRUCT || mt->kind == TY_UNION) && mt->tag &&
            mt->tag->has_fam) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, m->span,
                      "a struct with a flexible array member cannot be a "
                      "member of another struct (the GNU form lands in "
                      "Sprint 55)");
            mt = type_basic(TY_ERROR);
        }

        /* A member of incomplete type has no size, so the struct could
         * never be laid out — catchable now, without knowing any size.
         * The one exception is the flexible array member handled above. */
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

    tag->enum_ast = (AstNode *)rec;

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
                warn_at(s->lang->warnings, WARN_PEDANTIC, m->span,
                        "ISO C restricts enumerator values to range "
                        "of 'int'");
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
        ((AstNode *)m)->sym = sym;

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
    if (at->atomic_inner) {
        /* `_Atomic(type-name)`: the named type becomes atomic-qualified.
         * 6.7.2.4p3 forbids an array, function, atomic or qualified type
         * INSIDE the parentheses — note that `_Atomic(int) a[3]` is fine
         * (an ARRAY OF atomic int); only `_Atomic(int[3])` is not. */
        Type *inner = type_from_ast(s, at->atomic_inner, span);

        if (inner && inner->kind == TY_ARRAY) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "'_Atomic'-qualified array "
                      "type");
            return type_basic(TY_ERROR);
        }
        if (inner && inner->kind == TY_FUNC) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "'_Atomic'-qualified function type");
            return type_basic(TY_ERROR);
        }
        if (inner && (inner->quals & CGF_QUAL_ATOMIC))
            warn_at(s->lang->warnings, WARN_DUPLICATE_DECL_SPECIFIER, span,
                    "'_Atomic' applied to an already-atomic type");
        return inner; /* the ATOMIC qual is applied by the caller's
                         quals_from_ast, since the spelling set it */
    }
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
    case ABT_VA_LIST:
        return sema_va_list_type(s);
    case ABT_RECORD:
    case ABT_ENUM: {
        TypeKind kind = at->base == ABT_ENUM                   ? TY_ENUM
                        : (at->record && at->record->is_union) ? TY_UNION
                                                               : TY_STRUCT;
        TagDecl *tag;

        /* Sibling declarators share ONE specifier: `struct S {...} a, b;`
         * walks this AstType once per declarator, and reprocessing a
         * DEFINITION would either report a bogus redefinition (named) or
         * mint two incompatible types (anonymous). The first walk memoizes
         * its answer on the definition node. Found by the Sprint 18
         * lowering corpus; latent since Sprint 12. */
        if (at->record && at->record->is_definition && at->record->sem_type)
            return at->record->sem_type;

        tag = resolve_tag(s, at->record, kind, span);
        if (at->record && at->record->is_definition && !tag->complete) {
            if (kind == TY_ENUM)
                complete_enum(s, tag, at->record);
            else
                complete_struct(s, tag, at->record);
        }
        if (at->record && at->record->is_definition)
            at->record->sem_type = tag->type;
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
        if (inner && (inner->kind == TY_STRUCT || inner->kind == TY_UNION) &&
            inner->tag && inner->tag->has_fam) {
            /* An ARRAY of FAM-bearing structs has no meaningful stride —
             * where would each element's flexible tail go? */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, span,
                      "an array of structs with a flexible array member is "
                      "a GNU extension (lands in Sprint 55)");
            inner = type_basic(TY_ERROR);
        }
        arr = type_array(s->arena, inner);
        arr->size_expr = at->array_size;
        if (at->array_star) {
            /* `[*]` is legal only in a prototype: it promises "a VLA of
             * some size" where no size expression could be written. */
            arr->is_vla = true;
            if (s->scope->kind != SCOPE_PROTO) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, span,
                          "'[*]' is only allowed in a function prototype");
            }
        }
        if (at->array_size) {
            ConstValue cv;
            AstNode *bound = sema_expr(s, at->array_size);

            arr->size_expr = bound;
            if (bound->sem_type && bound->sem_type->kind != TY_ERROR &&
                !type_is_integer(bound->sem_type)) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, span,
                          "the size of an array has non-integer type '%s'",
                          type_to_str(s->arena, bound->sem_type));
            }
            /* CE_FOLD, deliberately: failing to fold is not an error here —
             * it is what MAKES this a VLA. The constraints on where a VLA
             * may live are checked at the declaration, where the storage
             * class is known. */
            cv = constexpr_eval(s, bound, CE_FOLD);
            if (cv.kind == CV_INT) {
                i64 n = (i64)cv.i;

                if (n < 0) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, span,
                              "array has a negative size");
                } else if (n == 0) {
                    warn_at(s->lang->warnings, WARN_ZERO_LENGTH_ARRAY, span,
                            "ISO C forbids zero-size arrays");
                    arr->has_size = true;
                    arr->size = 0;
                } else {
                    arr->has_size = true;
                    arr->size = (u64)n;
                }
            } else if (!bound->poisoned &&
                       (!bound->sem_type ||
                        bound->sem_type->kind != TY_ERROR)) {
                arr->is_vla = true;
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

                reject_nonfunction_attrs(s, at->params[i].cgf_attrs);

                /* 6.7.6.3p7/p8: a parameter of array type is adjusted to
                 * pointer-to-element, and of function type to pointer-to
                 * -function. Doing it HERE means every later pass sees the
                 * adjusted type and no one re-derives the rule. */
                if (pt && pt->kind == TY_ARRAY)
                    pt = type_ptr(s->arena, pt->base);
                else if (pt && pt->kind == TY_FUNC)
                    pt = type_ptr(s->arena, pt);
                /* The parser consumes the one legal `(void)` spelling as
                 * an empty parameter list.  Any void type that survives as
                 * an actual parameter is therefore constrained-invalid:
                 * named, qualified, or accompanied by another parameter. */
                if (pt && pt->kind == TY_VOID) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, at->params[i].span,
                              "'void' must be the only parameter and unnamed");
                    pt = type_basic(TY_ERROR);
                }
                fn->params[i] = pt;

                /* The name enters scope NOW, not after the list: `int m[n]`
                 * needs the earlier parameter n visible while m's bound is
                 * typed (6.7.6.2p5's whole point). Declared with the
                 * ADJUSTED type, since that is the type the body sees. */
                if (at->params[i].name) {
                    Symbol *ps = sym_new(s, at->params[i].name, SYM_VAR,
                                         NS_ORDINARY, pt, at->params[i].span);

                    ps->is_param = true;
                    ps->defined = true;
                    scope_declare(s, ps);
                }
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
            warn_at(s->lang->warnings, WARN_TYPEDEF_REDEFINITION, cur->span,
                    "redefinition of typedef '%s' is a C11 feature", cur->name);
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
    prev->static_storage = prev->static_storage || cur->static_storage;
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

static Member *init_find_member(Type *t, const char *name)
{
    Member *m;

    if (!t || !t->tag || !name)
        return NULL;
    for (m = t->tag->members; m; m = m->next)
        if (m->name == name)
            return m;
    return NULL;
}

/* Follow an initializer item's designator chain from the declared object.
 * The parser preserves `[2].x[1]` as three nodes; every array designator
 * selects the element type and every field designator selects that member.
 * Bounds and existence diagnostics stay with the constant/current-object
 * validation paths -- this helper answers only the type question needed to
 * materialize the assignment conversion. */
static Type *init_designated_type(Type *root, const AstNode *item)
{
    Type *t = root;
    u32 i;

    for (i = 0; item && i < item->ndesignators && t; i++) {
        const AstNode *desig = item->designators[i];

        if (!desig)
            return NULL;
        if (desig->desig_is_field) {
            Member *m = init_find_member(t, desig->desig_field);

            if (!m)
                return NULL;
            t = m->type;
        } else {
            if (t->kind != TY_ARRAY)
                return NULL;
            t = t->base;
        }
    }
    return t;
}

#define INIT_CURSOR_MAX 256u

typedef struct {
    Type *aggregate;
    u64 pos;
} InitCursorFrame;

typedef struct {
    InitCursorFrame frames[INIT_CURSOR_MAX];
    u32 depth;
    Type *current;
} InitCursor;

static bool init_is_aggregate(const Type *t)
{
    return t &&
           (t->kind == TY_ARRAY || t->kind == TY_STRUCT || t->kind == TY_UNION);
}

static Type *init_child_type(Type *aggregate, u64 pos)
{
    Member *m;
    u64 at = 0;

    if (!aggregate)
        return NULL;
    if (aggregate->kind == TY_ARRAY) {
        if (aggregate->has_size && pos >= aggregate->size)
            return NULL;
        return aggregate->base;
    }
    if ((aggregate->kind != TY_STRUCT && aggregate->kind != TY_UNION) ||
        !aggregate->tag)
        return NULL;
    for (m = aggregate->tag->members; m; m = m->next) {
        if (m->is_bitfield && !m->name)
            continue;
        if (at++ == pos)
            return m->type;
    }
    return NULL;
}

static bool init_member_position(Type *aggregate, const char *name, u64 *out)
{
    Member *m;
    u64 at = 0;

    if (!aggregate || !aggregate->tag || !name)
        return false;
    for (m = aggregate->tag->members; m; m = m->next) {
        if (m->is_bitfield && !m->name)
            continue;
        if (m->name == name) {
            *out = at;
            return true;
        }
        at++;
    }
    return false;
}

static void init_cursor_start(InitCursor *c, Type *root)
{
    memset(c, 0, sizeof(*c));
    if (!init_is_aggregate(root)) {
        c->current = root;
        return;
    }
    c->frames[0].aggregate = root;
    c->depth = 1;
    c->current = init_child_type(root, 0);
}

static bool init_cursor_descend(InitCursor *c)
{
    Type *aggregate = c->current;

    if (!init_is_aggregate(aggregate) || c->depth >= INIT_CURSOR_MAX)
        return false;
    c->frames[c->depth].aggregate = aggregate;
    c->frames[c->depth].pos = 0;
    c->depth++;
    c->current = init_child_type(aggregate, 0);
    return c->current != NULL;
}

static void init_cursor_advance(InitCursor *c)
{
    while (c->depth) {
        InitCursorFrame *f = &c->frames[c->depth - 1];

        /* A union consumes exactly one selected member. */
        if (f->aggregate->kind != TY_UNION) {
            Type *next;

            f->pos++;
            next = init_child_type(f->aggregate, f->pos);
            if (next) {
                c->current = next;
                return;
            }
        }
        c->depth--;
    }
    c->current = NULL;
}

static bool init_cursor_designate(Sema *s, InitCursor *c, Type *root,
                                  const AstNode *item)
{
    u32 i;

    init_cursor_start(c, root);
    for (i = 0; item && i < item->ndesignators; i++) {
        const AstNode *desig = item->designators[i];
        InitCursorFrame *f;
        u64 pos;

        if (!desig || c->depth == 0)
            return false;
        f = &c->frames[c->depth - 1];
        if (desig->desig_is_field) {
            if ((f->aggregate->kind != TY_STRUCT &&
                 f->aggregate->kind != TY_UNION) ||
                !init_member_position(f->aggregate, desig->desig_field, &pos))
                return false;
        } else {
            i64 idx;

            if (f->aggregate->kind != TY_ARRAY || !desig->desig_index ||
                !enum_fold(s, desig->desig_index, &idx) || idx < 0)
                return false;
            pos = (u64)idx;
        }
        f->pos = pos;
        c->current = init_child_type(f->aggregate, pos);
        if (!c->current)
            return false;
        if (i + 1 < item->ndesignators) {
            if (!init_is_aggregate(c->current) || c->depth >= INIT_CURSOR_MAX)
                return false;
            c->frames[c->depth].aggregate = c->current;
            c->frames[c->depth].pos = 0;
            c->depth++;
        }
    }
    return true;
}

static bool init_expr_initializes_whole(Type *target, const AstNode *init)
{
    if (!target || !init)
        return false;
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_STRING)
        return true;
    return init->sem_type && type_compatible(target, init->sem_type);
}

static void sema_init_assign_typed(Sema *s, Type *target, AstNode **slot)
{
    AstNode *init = slot ? *slot : NULL;
    AssignCtx ctx;

    if (!target || !init)
        return;
    /* A string literal initializes an array directly; it is not an
     * assignment to the array object. */
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_STRING)
        return;
    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = ACTX_INIT;
    conv_assignable(s, target, slot, ctx);
}

static void sema_init_value(Sema *s, Type *target, AstNode **slot)
{
    AstNode *init = slot ? *slot : NULL;
    u32 i;

    if (!init)
        return;
    if (init->kind != AST_INIT_LIST) {
        *slot = init = sema_expr(s, init);
        sema_init_assign_typed(s, target, slot);
        return;
    }

    if (!target) {
        for (i = 0; i < init->nitems; i++)
            sema_init_value(s, NULL, &init->items[i]);
        return;
    }

    if (init_is_aggregate(target)) {
        InitCursor cursor;

        init_cursor_start(&cursor, target);
        for (i = 0; i < init->nitems; i++) {
            AstNode *item = init->items[i];
            Type *item_type;

            if (!item)
                continue;
            if (item->ndesignators &&
                !init_cursor_designate(s, &cursor, target, item)) {
                sema_init_value(s, init_designated_type(target, item),
                                &init->items[i]);
                continue;
            }
            item_type = cursor.current;
            if (!item_type) {
                sema_init_value(s, NULL, &init->items[i]);
                continue;
            }
            if (item->kind == AST_INIT_LIST) {
                sema_init_value(s, item_type, &init->items[i]);
                init_cursor_advance(&cursor);
                continue;
            }

            init->items[i] = item = sema_expr(s, item);
            while (init_is_aggregate(cursor.current) &&
                   !init_expr_initializes_whole(cursor.current, item)) {
                if (!init_cursor_descend(&cursor))
                    break;
            }
            sema_init_assign_typed(s, cursor.current, &init->items[i]);
            init_cursor_advance(&cursor);
        }
        return;
    }

    /* C permits one level of braces around a scalar initializer. Type all
     * entries for recovery, but only the first initializes the object. */
    for (i = 0; i < init->nitems; i++)
        sema_init_value(s, i == 0 ? target : NULL, &init->items[i]);
}

/* Types an initializer and checks each scalar element against its current
 * object. Materializing these conversions is load-bearing for both static
 * initializer bytes and the Sprint 38 conversion-warning postpass. */
static void sema_init_expr(Sema *s, Type *target, AstNode *d,
                           bool is_static_init)
{
    if (!d->init)
        return;
    if (d->init->kind == AST_INIT_LIST) {
        /* Initializing the FLEXIBLE MEMBER is GNU's static-init extension:
         * the image would need a size the type does not have. Counting
         * items against named members catches the positional form; the
         * designated form is caught by the same count once designators
         * reposition (kept simple deliberately — the corpus shapes are
         * positional). */
        if (target && (target->kind == TY_STRUCT) && target->tag &&
            target->tag->has_fam && d->init->nitems >= target->tag->nmembers) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->init->span,
                      "initialization of a flexible array member is a GNU "
                      "extension (lands in Sprint 55)");
        }
    }
    sema_init_value(s, target, &d->init);

    /* An object with STATIC storage duration is initialized before any
     * code runs, so its initializer must be a CONSTANT — and an address
     * constant may not name an automatic object, which is the check that
     * keeps a stack address out of .data. This runs after typing because
     * an address constant needs its identifiers resolved first. */
    if (is_static_init && d->init->kind != AST_INIT_LIST && target &&
        target->kind != TY_ERROR && target->kind != TY_ARRAY) {
        ConstValue cv = constexpr_eval(
            s, d->init, target->kind == TY_PTR ? CE_ADDR : CE_ARITH);
        (void)cv; /* constexpr_eval reports the specific reason itself */
    }
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

        d->sem_alignas_type = at;
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

/* True if the type is VARIABLY MODIFIED: a VLA anywhere in the chain —
 * pointer to VLA, array of VLA, function returning pointer-to-VLA. The
 * distinction matters because most constraints (storage, linkage, the
 * jump checker) apply to VM types generally, not just to VLAs proper. */
static bool type_is_vm(const Type *t)
{
    for (; t; t = t->base) {
        if (t->kind == TY_ARRAY && t->is_vla)
            return true;
        if (t->kind != TY_PTR && t->kind != TY_ARRAY && t->kind != TY_FUNC)
            return false;
    }
    return false;
}

static bool type_contains_vla(const Type *t)
{
    for (; t; t = t->base)
        if (t->kind == TY_ARRAY && t->is_vla)
            return true;
    return false;
}

static bool attrs_conflict(const CgfAttr *a, const CgfAttr *b)
{
    if ((a->kind == CGF_ATTR_RETURNS_OWNED &&
         b->kind == CGF_ATTR_RETURNS_BORROWED) ||
        (b->kind == CGF_ATTR_RETURNS_OWNED &&
         a->kind == CGF_ATTR_RETURNS_BORROWED))
        return true;
    if (a->kind == CGF_ATTR_RETURNS_BORROWED &&
        b->kind == CGF_ATTR_RETURNS_BORROWED && a->arg != b->arg)
        return true;
    return a->arg == b->arg && ((a->kind == CGF_ATTR_TAKES_OWNERSHIP &&
                                 b->kind == CGF_ATTR_BORROWS) ||
                                (b->kind == CGF_ATTR_TAKES_OWNERSHIP &&
                                 a->kind == CGF_ATTR_BORROWS));
}

static void append_valid_attrs(Sema *s, Symbol *sym, const AstNode *d,
                               Type *decl_type)
{
    const CgfAttr *a;
    CgfAttr *head = NULL;
    CgfAttr *tail = NULL;
    u32 nparams = d->type && d->type->kind == ATY_FUNC ? d->type->nparams : 0;
    bool is_func = decl_type && decl_type->kind == TY_FUNC;

    if (!d->cgf_attrs)
        return;
    /* Never extend an already-published list in place. Redeclarations may
     * already have handed the old head to analysis clients, so merging is a
     * copied concatenation and every observable list remains immutable. */
    for (a = sym->cgf_attrs; a; a = a->next) {
        CgfAttr *copy = arena_alloc(s->arena, sizeof(*copy), _Alignof(CgfAttr));

        *copy = *a;
        copy->next = NULL;
        if (tail)
            tail->next = copy;
        else
            head = copy;
        tail = copy;
    }
    for (a = d->cgf_attrs; a; a = a->next) {
        const CgfAttr *old;
        CgfAttr *copy;

        if (!is_func) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, a->span,
                      "attribute '%s' applies only to functions",
                      cgf_attr_name(a->kind));
            continue;
        }
        if ((a->kind == CGF_ATTR_RETURNS_OWNED ||
             a->kind == CGF_ATTR_RETURNS_BORROWED) &&
            (!decl_type->base || decl_type->base->kind != TY_PTR)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, a->span,
                      "attribute '%s' requires a pointer return type",
                      cgf_attr_name(a->kind));
            continue;
        }
        if (a->kind != CGF_ATTR_RETURNS_OWNED &&
            (a->arg == 0 || a->arg > nparams)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, a->span,
                      "attribute '%s' parameter index %u is out of range "
                      "for function with %u parameter%s",
                      cgf_attr_name(a->kind), a->arg, nparams,
                      nparams == 1 ? "" : "s");
            continue;
        }
        if (a->kind != CGF_ATTR_RETURNS_OWNED &&
            (!decl_type->has_proto || a->arg > decl_type->nparams ||
             !decl_type->params[a->arg - 1] ||
             decl_type->params[a->arg - 1]->kind != TY_PTR)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, a->span,
                      "attribute '%s' parameter %u must have pointer type",
                      cgf_attr_name(a->kind), a->arg);
            continue;
        }
        for (old = head; old; old = old->next)
            if (attrs_conflict(old, a)) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, a->span,
                          "attribute '%s' contradicts '%s'",
                          cgf_attr_name(a->kind), cgf_attr_name(old->kind));
                diag_emit(s->dc, DIAG_NOTE, old->span,
                          "conflicting ownership attribute is here");
                break;
            }
        if (old)
            continue;
        copy = arena_alloc(s->arena, sizeof(*copy), _Alignof(CgfAttr));
        *copy = *a;
        copy->next = NULL;
        if (tail)
            tail->next = copy;
        else
            head = copy;
        tail = copy;
    }
    sym->cgf_attrs = head;
}

static void declare_one(Sema *s, AstNode *d)
{
    Type *type;
    Symbol *sym;
    Symbol *prev;
    Symbol *visible;
    bool is_func;
    bool static_init;
    bool file_scope = s->scope->kind == SCOPE_FILE;
    bool had_prior_prototype = false;

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
    if (type && type->kind == TY_ARRAY && !type->has_size && !type->is_vla &&
        d->init) {
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
        had_prior_prototype = visible && visible->kind == SYM_FUNC &&
                              visible->type && visible->type->kind == TY_FUNC &&
                              visible->type->has_proto;
        sym->linkage = linkage_for(s, visible, d->storage, is_func);
        sym->defined = d->init != NULL || d->kind == AST_FUNC_DEF;
        sym->static_storage =
            !is_func && !(d->storage & AST_SC_THREAD_LOCAL) &&
            (file_scope || (d->storage & (AST_SC_STATIC | AST_SC_EXTERN)));
        /* A file-scope object with no initializer and no `extern` is a
         * TENTATIVE definition (6.9.2p2). Resolution to a zero-initialized
         * object at end of TU is Sprint 16's; recording it is ours. */
        if (file_scope && !is_func && !sym->defined &&
            !(d->storage & AST_SC_EXTERN))
            sym->tentative = true;
    }

    /* --- Sprint 16 constraints, all needing the storage class ---------- */

    if (type_is_vm(type) && sym->kind != SYM_TYPEDEF) {
        /* 6.7.6.2p2: a VM type has automatic storage or is a parameter —
         * nothing with linkage, nothing static, nothing at file scope,
         * and never an initializer (there is no time to evaluate one). */
        if (file_scope) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "variably modified type at file scope");
        } else if (d->storage & (AST_SC_STATIC | AST_SC_EXTERN)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "a variably modified type must have automatic "
                      "storage duration");
        }
        if (d->init && type->kind == TY_ARRAY && type->is_vla) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "a variable-length array may not be initialized");
        }
        if (d->storage & AST_SC_THREAD_LOCAL) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "a variably modified type cannot have thread storage "
                      "duration");
        }
    } else if (type_is_vm(type) && sym->kind == SYM_TYPEDEF && file_scope) {
        /* A VM typedef is legal at BLOCK scope only (6.7.6.2p2). */
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "a variably modified typedef is only allowed at block "
                  "scope");
    }

    if (sym->kind == SYM_VAR && type_contains_vla(type))
        warn_at_ex(s->lang->warnings, WARN_VLA, d->span, WARN_SUPPRESS_IN_MACRO,
                   "variable length array '%s' is used", d->name);

    /* UNION across declarations, not replacement: an attribute on any
     * declaration of a symbol applies to the symbol. gcc's rule, and the
     * one musl's weak_alias pattern depends on -- the attribute and the
     * definition are routinely in different places. */
    gnu_attrs_merge(&sym->gnu, &d->gnu);
    if (sym->gnu.weak && sym->linkage == LINK_INTERNAL) {
        /* A static symbol has no binding for the linker to weaken, and gcc
         * says so rather than emitting a .weak that does nothing. */
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'weak' attribute ignored on a symbol with internal linkage");
        sym->gnu.weak = false;
    }

    if (d->storage & AST_SC_THREAD_LOCAL) {
        sym->tls = true;
        if (is_func) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "'_Thread_local' cannot appear on a function");
        } else if (!file_scope &&
                   !(d->storage & (AST_SC_STATIC | AST_SC_EXTERN))) {
            /* 6.7.1p3: at block scope, _Thread_local MUST be accompanied
             * by static or extern — a bare one has no storage duration to
             * name. */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "'_Thread_local' at block scope requires 'static' or "
                      "'extern'");
        }
    }

    /* _Atomic on an array — either spelling — qualifies the ELEMENT, so
     * the checks below look at the bottom of the chain. (The illegal
     * `_Atomic(int[3])` form was already rejected while resolving the
     * specifier's inner type-name.) */
    {
        Type *elem = type;

        while (elem && (elem->kind == TY_ARRAY || elem->kind == TY_PTR))
            elem = elem->base;
        if (elem && (elem->quals & CGF_QUAL_ATOMIC)) {
            if (is_func && type && type->kind == TY_FUNC &&
                (type->quals & CGF_QUAL_ATOMIC)) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, d->span,
                          "'_Atomic' cannot qualify a function");
            } else if (elem->kind == TY_STRUCT || elem->kind == TY_UNION) {
                /* An honest scope cut, not a stub: atomic aggregates need
                 * libatomic-shaped lowering that v0.1.0 does not take on. */
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, d->span,
                          "atomic struct/union types are outside v0.1.0 "
                          "scope");
            } else if (type_is_vm(type)) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, d->span,
                          "an atomic type cannot be variably modified");
            }
        }
    }

    if (is_func) {
        const AstType *ft = d->type;
        bool is_main = d->name == intern_str(s->interner,
                                             intern_cstr(s->interner, "main"));

        if (ft && ft->kind == ATY_FUNC && !type->has_proto)
            warn_at_ex(s->lang->warnings, WARN_STRICT_PROTOTYPES, d->span,
                       WARN_SUPPRESS_IN_MACRO,
                       "function declaration isn't a prototype");
        if (d->kind == AST_FUNC_DEF && ft && ft->kind == ATY_FUNC &&
            ft->is_kr_list)
            warn_at_ex(s->lang->warnings, WARN_OLD_STYLE_DEFINITION, d->span,
                       WARN_SUPPRESS_IN_MACRO,
                       "old-style function definition of '%s'", d->name);
        if (d->kind == AST_FUNC_DEF && sym->linkage == LINK_EXTERNAL &&
            !had_prior_prototype && !is_main)
            warn_at_ex(s->lang->warnings, WARN_MISSING_PROTOTYPES, d->span,
                       WARN_SUPPRESS_IN_MACRO, "no previous prototype for '%s'",
                       d->name);
        if ((d->func_specs & AST_FS_INLINE) && is_main) {
            /* 6.7.4p4 is a constraint, but gcc WARNS by default and errors
             * only under -pedantic-errors; real code (test harnesses,
             * mostly) relies on the warning. */
            warn_at(s->lang->warnings, WARN_MAIN, d->span,
                    "cannot inline function 'main'");
        }
    } else if (d->func_specs) {
        /* 6.7.4p2: function specifiers appear only on functions. gcc
         * warns rather than errors here too. */
        warn_at(s->lang->warnings, WARN_INVALID_FUNCTION_SPECIFIER, d->span,
                "%s '%s' declared '%s'",
                sym->kind == SYM_TYPEDEF ? "typedef" : "variable", d->name,
                (d->func_specs & AST_FS_INLINE) ? "inline" : "_Noreturn");
    }

    /* 6.7.4p3: an inline definition may not define a MODIFIABLE object
     * with static storage duration. gcc warns (const-qualified statics
     * are exempt); matched by observation. */
    if (s->cur_inline_candidate && !file_scope && !is_func &&
        sym->kind != SYM_TYPEDEF &&
        (d->storage & (AST_SC_STATIC | AST_SC_THREAD_LOCAL)) && type &&
        !(type->quals & CGF_QUAL_CONST))
        warn_at(s->lang->warnings, WARN_STATIC_IN_INLINE, d->span,
                "'%s' is static but declared in inline function '%s' "
                "which is not static",
                d->name, s->cur_fname ? s->cur_fname : "?");

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
    /* 6.7p7: an object with NO LINKAGE must have a complete type by the end
     * of its declarator (or of its init-declarator, which the completion
     * event above has already applied). `int a[];` at file scope is a
     * tentative definition a later one may complete, so the check above is
     * rightly keyed on `defined`; inside a block there is no later one and
     * nothing has a size, which used to reach lowering and ICE. */
    if (!is_func && sym->kind != SYM_TYPEDEF && !sym->defined &&
        sym->linkage == LINK_NONE && type && type->kind != TY_ERROR &&
        type->kind != TY_VOID && !type_is_complete(type)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  type->kind == TY_ARRAY ? "array size missing in '%s'"
                                         : "variable '%s' has incomplete type",
                  d->name);
    }
    if (!is_func && sym->kind != SYM_TYPEDEF && type && type->kind == TY_VOID) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span, "variable '%s' declared void",
                  d->name);
    }

    /* File-scope objects and block-scope `static` ones share one rule:
     * there is no code running when they are initialized. */
    static_init = !is_func && sym->kind != SYM_TYPEDEF &&
                  (file_scope || (d->storage & AST_SC_STATIC));

    d->sem_type = type; /* the jump checker reads VM-ness off the node */

    sym->func_specs = d->func_specs;
    sym->all_decls_inline = (d->func_specs & AST_FS_INLINE) != 0;
    sym->any_decl_extern = (d->storage & AST_SC_EXTERN) != 0;

    prev = scope_lookup_local(s->scope, d->name, NS_ORDINARY);
    if (prev) {
        /* The inline decision needs EVERY declaration (6.7.4p7), so the
         * surviving symbol accumulates across the merge. */
        prev->func_specs |= d->func_specs;
        prev->all_decls_inline =
            prev->all_decls_inline && (d->func_specs & AST_FS_INLINE) != 0;
        prev->any_decl_extern =
            prev->any_decl_extern || (d->storage & AST_SC_EXTERN) != 0;
        if (d->storage & AST_SC_THREAD_LOCAL)
            prev->tls = true;
        append_valid_attrs(s, prev, d, type);
        merge_redeclaration(s, prev, sym, d->storage);
        d->sym = prev; /* lowering resolves the DECL to its symbol */
        /* The initializer still has to be TYPED even when the declaration
         * merged into an earlier one, or its expressions never get
         * checked at all. */
        sema_init_expr(s, prev->type, d, static_init);
        return;
    }
    append_valid_attrs(s, sym, d, type);
    scope_declare(s, sym);
    d->sym = sym; /* lowering resolves the DECL to its symbol */
    sema_init_expr(s, sym->type, d, static_init);
}

static void sema_stmt(Sema *s, AstNode *st);

/* main's accepted shapes: `int main(void)`, `int main()` and
 * `int main(int, char **)` (or char *argv[], which adjusts to the same).
 * Everything else gets gcc's warnings, not errors — freestanding code
 * defines weird mains on purpose. */
static void check_main_signature(Sema *s, AstNode *d, Type *ftype)
{
    Type *cc;

    if (!ftype || ftype->kind != TY_FUNC)
        return;
    if (!ftype->base || ftype->base->kind != TY_INT)
        warn_at(s->lang->warnings, WARN_MAIN, d->span,
                "return type of 'main' is not 'int'");
    if (!ftype->has_proto || ftype->nparams == 0)
        return;
    if (ftype->nparams != 2) {
        warn_at(s->lang->warnings, WARN_MAIN, d->span,
                "'main' takes only zero or two arguments");
        return;
    }
    if (!type_is_integer(ftype->params[0]))
        warn_at(s->lang->warnings, WARN_MAIN, d->span,
                "first argument of 'main' should be 'int'");
    cc = ftype->params[1];
    if (!(cc && cc->kind == TY_PTR && cc->base && cc->base->kind == TY_PTR &&
          cc->base->base && cc->base->base->kind == TY_CHAR))
        warn_at(s->lang->warnings, WARN_MAIN, d->span,
                "second argument of 'main' should be 'char **'");
}

/* K&R parameter resolution (the definition's declaration list), plus the
 * 6.7.6.3p15 promoted-parameter compatibility check against any earlier
 * prototype. THE trap: `void f(x) float x; {}` against `void f(float);`
 * is INCOMPATIBLE, because default promotion carries the K&R float to
 * double — same story for char and short against themselves. */
static void declare_kr_params(Sema *s, AstNode *d, Symbol *fsym)
{
    const AstType *ft = d->type;
    Type *proto = NULL;
    u32 pi;

    /* An earlier PROTOTYPE to check against: the merged symbol type keeps
     * it (the composite of prototype and K&R list is the prototype). */
    if (fsym && fsym->type && fsym->type->kind == TY_FUNC &&
        fsym->type->has_proto)
        proto = fsym->type;

    if (proto && proto->nparams != ft->nparams) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "the definition of '%s' has %u parameters where the "
                  "prototype has %u",
                  d->name, (unsigned)ft->nparams, (unsigned)proto->nparams);
        proto = NULL;
    }

    for (pi = 0; pi < ft->nparams; pi++) {
        const char *pname = ft->params[pi].name;
        Type *pt = NULL;
        Symbol *ps;
        u32 ki;

        if (!pname)
            continue;
        /* Find this name in the K&R declaration list. */
        for (ki = 0; ki < d->nkr_decls && !pt; ki++) {
            AstNode *kd = d->kr_decls[ki];
            AstNode *one = kd;
            u32 sib = 0;

            while (one) {
                if (one->name == pname) {
                    pt = type_from_ast(s, one->type, one->span);
                    break;
                }
                one = sib < kd->nitems ? kd->items[sib++] : NULL;
            }
        }
        if (!pt) {
            /* No declaration: implicitly int, with gcc's Wextra warning. */
            warn_at_ex(s->lang->warnings, WARN_MISSING_PARAMETER_TYPE,
                       ft->params[pi].span, WARN_SUPPRESS_IN_MACRO,
                       "type of '%s' defaults to 'int'", pname);
            pt = type_basic(TY_INT);
        }
        if (pt->kind == TY_ARRAY)
            pt = type_ptr(s->arena, pt->base);
        else if (pt->kind == TY_FUNC)
            pt = type_ptr(s->arena, pt);

        if (proto && pi < proto->nparams) {
            /* Compatibility is judged on the PROMOTED K&R type. */
            Type *promoted = conv_promote_type(s, pt);
            Type *want = conv_strip_quals(s, proto->params[pi]);

            if (pt->kind == TY_FLOAT)
                promoted = type_basic(TY_DOUBLE);
            if (!type_compatible(conv_strip_quals(s, promoted), want))
                warn_at(s->lang->warnings, WARN_TRADITIONAL,
                        ft->params[pi].span,
                        "promoted argument '%s' doesn't match prototype ('%s' "
                        "promotes to '%s', prototype says '%s')",
                        pname, type_to_str(s->arena, pt),
                        type_to_str(s->arena, promoted),
                        type_to_str(s->arena, proto->params[pi]));
        }

        ps = sym_new(s, pname, SYM_VAR, NS_ORDINARY, pt, ft->params[pi].span);
        ps->is_param = true;
        ps->defined = true;
        if (scope_lookup_local(s->scope, pname, NS_ORDINARY)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, ft->params[pi].span,
                      "redefinition of parameter '%s'", pname);
        } else {
            scope_declare(s, ps);
            if (d->param_syms && pi < d->nparam_syms)
                d->param_syms[pi] = ps;
        }
    }
}

/* True iff every VM declaration in scope at `target` is also in scope at
 * `source` — the condition that makes the jump legal. Chains share stack
 * structure, so pointer identity is the membership test. */
static bool vm_chain_subset(VmDecl *target, VmDecl *source)
{
    VmDecl *t;

    for (t = target; t; t = t->parent) {
        VmDecl *u;
        bool found = false;

        for (u = source; u; u = u->parent)
            if (u == t) {
                found = true;
                break;
            }
        if (!found)
            return false;
    }
    return true;
}

static void resolve_vm_jumps(Sema *s)
{
    VmGoto *g;

    for (g = s->vm_gotos; g; g = g->next) {
        VmLabel *l;

        for (l = s->vm_labels; l; l = l->next)
            if (l->name == g->label)
                break;
        if (!l)
            continue; /* undefined labels were the parser's diagnostic */
        if (!vm_chain_subset(l->chain, g->chain)) {
            VmDecl *t = l->chain;

            /* Name the specific object whose scope the jump enters. */
            while (t && vm_chain_subset(t, g->chain))
                t = t->parent;
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, g->span,
                      "jump into scope of identifier '%s' with variably "
                      "modified type",
                      t && t->name ? t->name : "?");
        }
    }
    s->vm_gotos = NULL;
    s->vm_labels = NULL;
    s->vm_chain = NULL;
}

static void sema_decl(Sema *s, AstNode *d)
{
    u32 i;

    if (!d || d->poisoned)
        return;

    switch (d->kind) {
    case AST_EMPTY_DECL:
        /* `struct S { ... };` declares no object but DOES introduce or
         * complete a tag, which is the whole point of the line. */
        reject_nonfunction_attrs(s, d->cgf_attrs);
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
    case AST_FUNC_DEF: {
        Symbol *fsym;
        Type *ftype;

        declare_one(s, d);
        fsym = scope_lookup_local(s->scope->kind == SCOPE_FILE ? s->scope
                                                               : s->file_scope,
                                  d->name, NS_ORDINARY);
        ftype = fsym ? fsym->type : NULL;

        /* main gets its signature checked once, at the definition. */
        if (d->name &&
            d->name ==
                intern_str(s->interner, intern_cstr(s->interner, "main"))) {
            if (fsym)
                fsym->is_main = true;
            check_main_signature(s, d, ftype);
        }

        /* Function-body state for the return checker. */
        s->cur_ret = ftype && ftype->kind == TY_FUNC ? ftype->base : NULL;
        s->cur_fname = d->name;
        s->cur_func_specs = fsym ? fsym->func_specs : d->func_specs;
        /* An inline-definition candidate, judged on declarations seen SO
         * FAR — which is when gcc judges it too. */
        s->cur_inline_candidate = fsym && (fsym->func_specs & AST_FS_INLINE) &&
                                  fsym->linkage == LINK_EXTERNAL &&
                                  fsym->all_decls_inline &&
                                  !fsym->any_decl_extern;
        s->vm_chain = NULL;
        s->vm_labels = NULL;
        s->vm_gotos = NULL;
        s->vm_switch_chain = NULL;

        scope_push(s, SCOPE_FUNC);
        {
            u32 pi;
            const AstType *ft = d->type;

            /* Record parameter SYMBOLS on the definition node: the scope
             * they live in is popped when the body ends, and Sprint 18's
             * lowering needs them to bind IR parameters. */
            if (ft && ft->kind == ATY_FUNC && ft->nparams) {
                d->param_syms =
                    arena_alloc(s->arena, ft->nparams * sizeof(Symbol *),
                                _Alignof(Symbol *));
                memset(d->param_syms, 0, ft->nparams * sizeof(Symbol *));
                d->nparam_syms = ft->nparams;
            }
            if (ft && ft->kind == ATY_FUNC && ft->is_kr_list) {
                /* A K&R definition: parameter TYPES come from the
                 * declaration list between ')' and '{'; a parameter with
                 * no declaration there is implicitly int. Resolved here —
                 * nothing earlier had both the names and the list. */
                declare_kr_params(s, d, fsym);
            } else if (ft && ft->kind == ATY_FUNC) {
                for (pi = 0; pi < ft->nparams; pi++) {
                    Symbol *ps;
                    Type *pt;

                    if (!ft->params[pi].name)
                        continue;
                    pt = type_from_ast(s, ft->params[pi].type,
                                       ft->params[pi].span);
                    /* The same 6.7.6.3p7/p8 adjustment the prototype path
                     * applies: the BODY sees the pointer, not the array. */
                    if (pt && pt->kind == TY_ARRAY)
                        pt = type_ptr(s->arena, pt->base);
                    else if (pt && pt->kind == TY_FUNC)
                        pt = type_ptr(s->arena, pt);
                    ps = sym_new(s, ft->params[pi].name, SYM_VAR, NS_ORDINARY,
                                 pt, ft->params[pi].span);
                    ps->is_param = true;
                    ps->defined = true;
                    if (scope_lookup_local(s->scope, ps->name, NS_ORDINARY)) {
                        s->nerrors++;
                        diag_emit(s->dc, DIAG_ERROR, ps->span,
                                  "redefinition of parameter '%s'", ps->name);
                    } else {
                        scope_declare(s, ps);
                        if (d->param_syms && pi < d->nparam_syms)
                            d->param_syms[pi] = ps;
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

        /* Resolve the collected gotos against their labels: a jump whose
         * TARGET scope contains a VM declaration not in scope at the
         * SOURCE would enter the object's lifetime without ever running
         * its size expression (6.8.6.1p1). */
        resolve_vm_jumps(s);
        s->cur_inline_candidate = false;
        s->cur_ret = NULL;
        s->cur_fname = NULL;
        s->cur_func_specs = 0;
        return;
    }
    case AST_DECL:
        declare_one(s, d);
        for (i = 0; i < d->nitems; i++)
            declare_one(s, d->items[i]);
        return;
    default:
        return;
    }
}

static AstNode *discarded_update_core(AstNode *e)
{
    while (e && ((e->kind == AST_EXPR_CAST && e->implicit) ||
                 e->kind == AST_EXPR_PAREN))
        e = e->lhs;
    return e;
}

/* A self-update whose result is discarded does not make a variable
 * meaningfully used for GCC's unused-but-set diagnostics.  Expression
 * typing necessarily reads the old value; remove precisely that bookkeeping
 * read once the statement context is known. */
static void sema_mark_discarded_update(AstNode *e)
{
    AstNode *target;
    AstNode *ident;

    e = discarded_update_core(e);
    if (!e)
        return;
    if (e->kind == AST_EXPR_BINARY && e->op == PUNCT_COMMA) {
        sema_mark_discarded_update(e->lhs);
        sema_mark_discarded_update(e->rhs);
        return;
    }
    if (e->kind == AST_EXPR_BINARY && e->op >= PUNCT_STAR_ASSIGN &&
        e->op <= PUNCT_PIPE_ASSIGN)
        target = discarded_update_core(e->lhs);
    else if (e->kind == AST_EXPR_UNARY &&
             (e->op == PUNCT_PLUSPLUS || e->op == PUNCT_MINUSMINUS))
        target = discarded_update_core(e->lhs);
    else
        return;
    ident = sema_lvalue_root_ident(target);
    if (ident && ident->sym && ident->sym->reads)
        ident->sym->reads--;
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
    case AST_STMT_COMPOUND: {
        /* Every compound statement is a scope. The function body's
         * outermost block is the exception — it SHARES the parameter
         * scope (6.2.1p4) — and the FUNC_DEF path handles that by calling
         * sema_block_items directly. Missing this made an inner
         * `struct T { ... }` a redefinition of an outer one, and made
         * `int s;` inside a nested block collide with an outer `s`.
         *
         * The VM chain is saved and restored with the scope: a VM object
         * dies with its block, and a label AFTER the block is a legal
         * jump target again. */
        VmDecl *saved = s->vm_chain;

        scope_push(s, SCOPE_BLOCK);
        for (i = 0; i < st->nitems; i++)
            sema_stmt(s, st->items[i]);
        scope_pop(s);
        s->vm_chain = saved;
        return;
    }
    case AST_STMT_DECL:
        sema_decl(s, st->lhs);
        /* A VM declaration extends the chain the jump checker snapshots:
         * every label and goto from here to the end of the block carries
         * it. Sibling declarators ride the same statement node. */
        {
            AstNode *one = st->lhs;
            u32 sib = 0;

            while (one) {
                if (one->sem_type && type_is_vm(one->sem_type) &&
                    !(one->storage & AST_SC_TYPEDEF)) {
                    VmDecl *vd =
                        arena_alloc(s->arena, sizeof(VmDecl), _Alignof(VmDecl));

                    vd->name = one->name;
                    vd->span = one->span;
                    vd->parent = s->vm_chain;
                    s->vm_chain = vd;
                }
                one = st->lhs && sib < st->lhs->nitems ? st->lhs->items[sib++]
                                                       : NULL;
            }
        }
        return;
    case AST_STMT_EXPR:
        st->lhs = sema_expr(s, st->lhs);
        sema_mark_discarded_update(st->lhs);
        return;
    case AST_STMT_RETURN:
        if (st->lhs)
            st->lhs = sema_expr(s, st->lhs);
        /* A `return` inside a _Noreturn function is gcc's warning, not an
         * error — the promise is broken but the code is well-defined. The
         * flow-sensitive falls-off-the-end half joins Sprint 40's CFG. */
        if (s->cur_func_specs & AST_FS_NORETURN)
            warn_at(s->lang->warnings, WARN_RETURN_TYPE, st->span,
                    "function declared 'noreturn' has a 'return' "
                    "statement");
        if (s->cur_ret && s->cur_ret->kind == TY_VOID) {
            if (st->lhs)
                warn_at(s->lang->warnings, WARN_RETURN_TYPE, st->span,
                        "'return' with a value, in function returning void");
        } else if (s->cur_ret) {
            if (!st->lhs) {
                warn_at(
                    s->lang->warnings, WARN_RETURN_TYPE, st->span,
                    "'return' with no value, in function returning non-void");
            } else if (!st->lhs->poisoned) {
                AssignCtx ctx;

                memset(&ctx, 0, sizeof(ctx));
                ctx.kind = ACTX_RETURN;
                conv_assignable(s, s->cur_ret, &st->lhs, ctx);
            }
        }
        return;
    case AST_STMT_IF:
    case AST_STMT_SWITCH:
    case AST_STMT_WHILE:
    case AST_STMT_DO:
        if (st->lhs)
            st->lhs = sema_expr(s, st->lhs);
        if (st->kind == AST_STMT_SWITCH) {
            VmDecl *saved_sw = s->vm_switch_chain;

            s->vm_switch_chain = s->vm_chain;
            sema_stmt(s, st->body);
            s->vm_switch_chain = saved_sw;
            return;
        }
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
        sema_mark_discarded_update(st->rhs);
        sema_stmt(s, st->body);
        scope_pop(s);
        return;
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
        if (st->kind == AST_STMT_CASE && st->lhs) {
            i64 cv;

            st->lhs = sema_expr(s, st->lhs);
            /* Case labels are integer constant expressions; Sprint 15
             * gave us the evaluator, so duplicate checking is possible
             * now — but the VALUE check that matters here is the VM one. */
            (void)sema_require_ice(s, st->lhs, &cv, "a case label");
        }
        /* A switch jumps from its controlling expression to each label:
         * a case inside a VM scope the switch itself is outside of would
         * enter the object without sizing it (6.8.4.2p2). */
        if (!vm_chain_subset(s->vm_chain, s->vm_switch_chain)) {
            VmDecl *t = s->vm_chain;

            while (t && vm_chain_subset(t, s->vm_switch_chain))
                t = t->parent;
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, st->span,
                      "switch jumps into scope of identifier '%s' with "
                      "variably modified type",
                      t && t->name ? t->name : "?");
        }
        sema_stmt(s, st->body);
        return;
    case AST_STMT_LABEL: {
        VmLabel *l = arena_alloc(s->arena, sizeof(VmLabel), _Alignof(VmLabel));

        l->name = st->name;
        l->chain = s->vm_chain;
        l->next = s->vm_labels;
        s->vm_labels = l;
        sema_stmt(s, st->body);
        return;
    }
    case AST_STMT_GOTO: {
        VmGoto *g = arena_alloc(s->arena, sizeof(VmGoto), _Alignof(VmGoto));

        g->label = st->name;
        g->span = st->span;
        g->chain = s->vm_chain;
        g->next = s->vm_gotos;
        s->vm_gotos = g;
        return;
    }
    default:
        return;
    }
}

static void finish_symbol(Sema *s, Symbol *sym)
{
    if (!sym)
        return;
    finish_symbol(s, sym->next); /* declaration order */

    if (sym->kind == SYM_FUNC) {
        /* THE inline matrix (6.7.4p7). The decision needs every
         * declaration in the TU, which is why it lives here and nowhere
         * earlier: `inline` definition + a later plain `int f(void);`
         * flips the answer to "emit". */
        if (!(sym->func_specs & AST_FS_INLINE))
            sym->inline_kind = INL_NONE;
        else if (sym->linkage == LINK_INTERNAL)
            sym->inline_kind = INL_STATIC;
        else if (sym->all_decls_inline && !sym->any_decl_extern)
            sym->inline_kind = INL_INLINE_DEF; /* do NOT emit here */
        else
            sym->inline_kind = INL_EXTERN_INLINE; /* THE external def */
        if (sym->inline_kind == INL_INLINE_DEF && !sym->defined) {
            /* Declared inline everywhere, never defined, possibly called:
             * fine — calls bind to the external definition elsewhere. */
            sym->inline_kind = INL_NONE;
        }
        if (sym->defined && sym->linkage == LINK_INTERNAL && !sym->reads &&
            !(sym->func_specs & AST_FS_INLINE))
            warn_at_ex(s->lang->warnings, WARN_UNUSED_FUNCTION, sym->span,
                       WARN_SUPPRESS_IN_MACRO, "'%s' defined but not used",
                       sym->name);
        return;
    }
    if (sym->kind != SYM_VAR)
        return;

    if (sym->linkage == LINK_INTERNAL && (sym->defined || sym->tentative) &&
        !sym->reads)
        warn_at_ex(s->lang->warnings, WARN_UNUSED_VARIABLE, sym->span,
                   WARN_SUPPRESS_IN_MACRO, "'%s' defined but not used",
                   sym->name);

    /* Tentative resolution (6.9.2p2): at end of TU a tentative becomes a
     * definition with zero initializer. Under -fcommon (gcc 8's default)
     * an EXTERNAL tentative becomes a COMMON symbol instead, so multiple
     * TUs each saying `int x;` still link. */
    if (sym->defined) {
        sym->def_kind = DEF_INIT;
        return;
    }
    if (!sym->tentative) {
        sym->def_kind = DEF_NONE; /* a plain extern declaration */
        return;
    }
    if (sym->type && !type_is_complete(sym->type)) {
        if (sym->type->kind == TY_ARRAY && !sym->type->has_size &&
            sym->linkage == LINK_EXTERNAL) {
            /* `int a[];` at end of TU completes to one element — gcc's
             * behavior, warning included. */
            Type *one = type_array(s->arena, sym->type->base);

            one->quals = sym->type->quals;
            one->has_size = true;
            one->size = 1;
            sym->type = one;
            warn_at(s->lang->warnings, WARN_TENTATIVE_DEFINITION_ARRAY,
                    sym->span, "array '%s' assumed to have one element",
                    sym->name);
        } else {
            /* 6.9.2p3: an internal-linkage tentative must have a complete
             * type by end of TU — nothing can complete it later. */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, sym->span,
                      "storage size of '%s' isn't known", sym->name);
            return;
        }
    }
    sym->def_kind = (sym->linkage == LINK_EXTERNAL && s->fcommon)
                        ? DEF_COMMON
                        : DEF_ZERO_INIT;
}

void sema_finish(Sema *s)
{
    if (s->file_scope)
        finish_symbol(s, s->file_scope->ordinary);
}

void sema_run(Sema *s, AstNode *tu)
{
    u32 i;

    if (!tu)
        return;
    for (i = 0; i < tu->ndecls; i++)
        sema_decl(s, tu->decls[i]);
    sema_finish(s);
}
