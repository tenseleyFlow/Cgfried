#include <string.h>

#include "sema/sema.h"
#include "util/vec.h"
#include "warn/warn.h"

/* Declarations: AST types to semantic Types, tag scoping, linkage,
 * redeclaration merging, and enum completion.
 *
 * Nothing here computes a size. Every path that would need one routes
 * through sema_unimplemented naming Sprint 14, so a missing capability is
 * always an error and never a wrong answer. */

static Type *type_from_ast(Sema *s, const AstType *at, Span span);
static Type *adjust_param_type(Sema *s, Type *pt);
static u64 check_alignas(Sema *s, AstNode *d, Type *type);
static u64 gnu_aligned_value(Sema *s, const GnuDeclAttrs *g, Span span);
static Type *gnu_mode_apply(Sema *s, Type *t, const GnuDeclAttrs *g,
                            bool binds_enum_definition, Span span);

VEC_DECL(InitNodeVec, AstNode *);

static bool alignment_is_supported(Sema *s, Span span, i64 want)
{
    if ((u64)want <= CGF_MAX_OBJECT_ALIGN)
        return true;
    s->nerrors++;
    diag_emit(s->dc, DIAG_ERROR, span,
              "requested alignment %lld exceeds cgfried's maximum supported "
              "alignment of %u bytes",
              (long long)want, CGF_MAX_OBJECT_ALIGN);
    return false;
}

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
    /* Set BEFORE the member walk: add_member consults it, so that a record's
     * own `packed` and a member's are one rule with one implementation. */
    tag->packed = rec->packed;
    tag->type->may_alias |= rec->may_alias;
    if (rec->record_aligned_expr || rec->record_aligned_bare) {
        GnuDeclAttrs ra = {0};

        ra.aligned_expr = rec->record_aligned_expr;
        ra.aligned_bare = rec->record_aligned_bare;
        /* Straight into the field layout already consults with `>`, which is
         * exactly the only-ever-raises rule. */
        tag->align_override = gnu_aligned_value(s, &ra, rec->span);
    }

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
         * member and gcc gives it size ZERO as a GNU extension.
         * (`struct { int :5; }` is the same case — gcc's size 1 there is
         * incidental; both are the no-named-member extension.)
         *
         * REFUSED, not deferred. Sprint 55 examined it and declined, and
         * the deciding fact is gcc's own behaviour rather than ours:
         * `struct E arr[3];` gives `&arr[0] == &arr[1]`, MEASURED. So the
         * extension does not merely add a size of zero, it breaks the
         * "distinct objects have distinct addresses" property that the
         * shared alias service and the memory-safety lattice are both
         * built on -- allocation sites there are separated by byte-offset
         * hulls, and two objects at one address with zero extent are
         * exactly what those hulls cannot express.
         *
         * Demand was measured before deciding: musl 0, glibc's C headers 0
         * (every hit under /usr/include is C++), Linux uapi 1, inside the
         * __DECLARE_FLEX_ARRAY macro. See docs/gnu-extensions.md. */
        if (!any_named) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, rec->span,
                      "a struct or union must have at least one named member; "
                      "the GNU no-named-member extension is not supported "
                      "(docs/gnu-extensions.md)");
        }
    }
}

static bool type_contains_fam(const Type *type)
{
    if (!type)
        return false;
    if ((type->kind == TY_STRUCT || type->kind == TY_UNION) && type->tag)
        return type->tag->contains_fam;
    if (type->kind == TY_ARRAY)
        return type_contains_fam(type->base);
    return false;
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
         * hard-errors rather than silently accepting semantics Sprint 19
         * could not emit. (gcc accepts them quietly and pedwarns; that is an
         * extension-scope choice documented in docs/gnu-extensions.md.) */
        if (mt && mt->kind == TY_ARRAY && !mt->has_size && !mt->is_vla) {
            if (tag->kind == TY_UNION) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, m->span,
                          "a flexible array member in a union is a GNU "
                          "extension that is not supported");
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
                          "named members is a GNU extension that is not "
                          "supported");
                mt = type_basic(TY_ERROR);
            } else {
                tag->has_fam = true;
                tag->contains_fam = true;
            }
        }
        /* ISO C permits a union to contain a FAM-bearing struct and carries
         * that property through nested unions. GCC also permits such a type
         * in a struct, with a pedantic diagnostic, and gives the containing
         * record its ordinary fixed layout. Track recursive containment
         * separately from `has_fam`: only a DIRECT FAM may extend a static
         * definition's storage in sema_init_expr. */
        if (type_contains_fam(mt)) {
            if (tag->kind == TY_STRUCT &&
                (mt->kind == TY_STRUCT || mt->kind == TY_UNION) &&
                s->lang->pedantic)
                warn_at(s->lang->warnings, WARN_PEDANTIC, m->span,
                        "invalid use of structure with flexible array "
                        "member");
            tag->contains_fam = true;
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

        /* A member's mode changes the RECORD's layout, so it has to land
         * before the Member is built rather than beside the alignment
         * override further down. */
        mt = gnu_mode_apply(s, mt, &m->gnu, false, m->span);

        mem = arena_alloc(s->arena, sizeof(Member), _Alignof(Member));
        memset(mem, 0, sizeof(*mem));
        mem->name = m->name;
        mem->type = mt;
        mem->is_bitfield = m->is_bitfield;
        mem->bitfield_width = m->bitfield_width;
        if (m->is_bitfield) {
            i64 wv = 0;

            /* gcc's implementation-defined enum-bitfield representation is
             * unsigned when the enum has no negative enumerator. This is
             * deliberately member metadata: Cgfried's existing compatible-
             * type policy keeps a small enum as int, while extraction still
             * needs the enum's value-range sign. */
            mem->bitfield_is_signed =
                mt && mt->kind == TY_ENUM && mt->tag && mt->tag->complete
                    ? mt->tag->enum_has_negative
                    : conv_is_signed(s, mt);

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
        {
            u64 ga = gnu_aligned_value(s, &m->gnu, m->span);

            mem->align_override = member_align > ga ? member_align : ga;
        }
        /* A member is destroyed with the object that contains it, not at a
         * scope exit, so there is no edge for a cleanup call to sit on. gcc
         * warns and drops it; the same is true of a typedef, which the
         * ordinary declaration path handles. */
        if (m->gnu.cleanup_fn)
            warn_at(s->lang->warnings, WARN_ATTRIBUTES, m->span,
                    "'cleanup' attribute ignored");
        mem->deprecated = m->gnu.deprecated;
        mem->deprecated_msg = m->gnu.deprecated_msg;
        mem->packed = tag->packed || m->gnu.packed;
        if (mem->packed) {
            /* Ordinary unaligned loads and stores are fine on both targets;
             * the exclusive instructions that implement _Atomic on arm64 are
             * not. An atomic that silently is not one is worse than an
             * error. */
            if (mt && (mt->quals & CGF_QUAL_ATOMIC)) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, m->span,
                          "an _Atomic member of a packed struct is not "
                          "supported: the atomic instructions require natural "
                          "alignment (docs/gnu-extensions.md)");
            }
        }
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
        gnu_attrs_merge(&sym->gnu, &m->gnu);
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
    tag->enum_has_negative = any_negative;
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
    if (rec->record_mode) {
        GnuDeclAttrs mode = {0};

        mode.mode = rec->record_mode;
        (void)gnu_mode_apply(s, tag->type, &mode, true, rec->span);
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
    case ABT_ERROR:
        return type_basic(TY_ERROR);
    case ABT_TYPEOF: {
        /* THE TYPE IS THE OPERAND'S, QUALIFIERS AND ALL. `const int c;
         * typeof(c) k;` gives a CONST k -- gcc rejects `k = 3` with
         * "assignment of read-only variable", measured. And no decay: an
         * array operand keeps its array type, which is why
         * `int arr[4]; typeof(arr) a2;` has sizeof 16.
         *
         * The expression was parsed UNEVALUATED, so typing it here cannot
         * emit side effects; `typeof(f())` calls nothing. */
        if (at->typeof_type)
            return sema_type_from_ast(s, at->typeof_type, span);
        if (at->typeof_expr) {
            AstNode *e = sema_expr(s, at->typeof_expr);

            if (e && e->sem_type)
                return e->sem_type;
        }
        return type_basic(TY_ERROR);
    }
    case ABT_AUTO_TYPE:
        /* Resolved by the DECLARATION, which is the only place the
         * initializer is in hand. Reaching here means __auto_type appeared
         * somewhere with no initializer to deduce from -- a type name, a
         * parameter, a member -- and the declaration path has already said
         * so. */
        return type_basic(TY_ERROR);
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
    case ABT_FLOAT128:
        return type_basic(TY_FLOAT128);
    case ABT_FLOAT32:
        return type_basic(TY_FLOAT32);
    case ABT_FLOAT64:
        return type_basic(TY_FLOAT64);
    case ABT_FLOAT32X:
        return type_basic(TY_FLOAT32X);
    case ABT_FLOAT64X:
        return type_basic(TY_FLOAT64X);
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

        if (sym && sym->kind == SYM_TYPEDEF) {
            sema_warn_deprecated(s, sym->name, sym->gnu.deprecated,
                                 sym->gnu.deprecated_msg, at->span);
            return sym->type;
        }
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
        {
            GnuDeclAttrs aligned = {0};
            Type *ptr = type_ptr(s->arena, inner);

            aligned.aligned_expr = at->ptr_aligned_expr;
            aligned.aligned_bare = at->ptr_aligned_bare;
            aligned.aligned_conflict = at->ptr_aligned_conflict;
            ptr = type_with_alignment(s->arena, ptr,
                                      gnu_aligned_value(s, &aligned, at->span));
            return type_qualify(s->arena, ptr, quals_from_ast(at->ptr_quals));
        }
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
            inner->tag && inner->tag->contains_fam && s->lang->pedantic) {
            /* GCC's extension uses the record's ordinary fixed sizeof as
             * the array stride; the flexible tail contributes no storage to
             * an element. This is useful for compatibility but remains a
             * constraint violation in strictly conforming C. */
            warn_at(s->lang->warnings, WARN_PEDANTIC, span,
                    "invalid use of structure with flexible array member");
        }
        if (inner && inner->kind != TY_ERROR && inner->align_override) {
            TypeLayout el = layout_of(s, inner);

            if (el.align > el.size) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, span,
                          "alignment of array elements is greater than "
                          "element size");
                inner = type_basic(TY_ERROR);
            }
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
                pt = adjust_param_type(s, pt);
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

    /* SEMA-H-04: declarations in one scope can denote the same entity only
     * when their linkage agrees. In particular, an automatic object has no
     * linkage while a block-scope extern with no linked visible predecessor
     * has external linkage; neither declaration order may merge those two
     * distinct entities. Keep this before composite-type construction, which
     * remains valid for declarations that do share linkage. */
    if ((prev->linkage == LINK_NONE) != (cur->linkage == LINK_NONE)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, cur->span,
                  "declaration of '%s' with no linkage conflicts with "
                  "declaration with linkage",
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

    /* GCC diagnoses a prototype-first K&R mismatch at the parameter after
     * the definition is entered (warning by default, pedantic error), while
     * the reverse order is an immediate conflicting-type error. Preserve
     * that diagnostic policy without weakening symmetric type compatibility. */
    if (!type_compatible(prev->type, cur->type) &&
        !(prev->type && cur->type && prev->type->kind == TY_FUNC &&
          cur->type->kind == TY_FUNC && prev->type->has_proto &&
          cur->type->kr_definition &&
          type_compatible(prev->type->base, cur->type->base))) {
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

static bool array_size_from_init(Sema *s, Type *array, const AstNode *init,
                                 u64 *out);

/* The shared completion, so a declaration and a compound literal cannot
 * drift. Returns `t` unchanged when the rule does not apply -- no type, not
 * an array, already sized, a VLA, or no initializer to count. */
Type *sema_array_complete_from_init(Sema *s, Type *t, const AstNode *init)
{
    Type *sized;
    u64 n;

    if (!t || t->kind != TY_ARRAY || t->has_size || t->is_vla || !init)
        return t;
    if (!array_size_from_init(s, t, init, &n))
        return t;
    sized = type_array(s->arena, t->base);
    sized->quals = t->quals;
    sized->may_alias = t->may_alias;
    sized->align_override = t->align_override;
    sized->has_size = true;
    sized->size = n;
    sized->size_expr = t->size_expr;
    return sized;
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

/* Fold one array designator exactly once.  Range endpoints remain syntax in
 * the parser, then become cached semantic values here; every later consumer
 * reads the same result rather than independently re-evaluating an ICE. */
static bool init_designator_bounds(Sema *s, AstNode *desig, u64 *first,
                                   u64 *last)
{
    i64 lo;
    i64 hi;

    if (!desig || desig->desig_is_field || !desig->desig_index)
        return false;
    if (desig->desig_bounds_checked) {
        if (!desig->desig_bounds_valid)
            return false;
        *first = (u64)desig->desig_index_value;
        *last = (u64)desig->desig_range_end_value;
        return true;
    }

    desig->desig_bounds_checked = true;
    desig->desig_bounds_valid = false;
    if (!sema_require_ice(s, desig->desig_index, &lo, "an array designator"))
        return false;
    if (lo < 0) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, desig->desig_index->span,
                  "array designator index is negative");
        return false;
    }
    hi = lo;
    if (desig->desig_range_end) {
        if (!sema_require_ice(s, desig->desig_range_end, &hi,
                              "an array designator range endpoint"))
            return false;
        if (hi < lo) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, desig->desig_range_end->span,
                      "empty index range in initializer");
            return false;
        }
    }
    desig->desig_index_value = lo;
    desig->desig_range_end_value = hi;
    desig->desig_bounds_valid = true;
    *first = (u64)lo;
    *last = (u64)hi;
    return true;
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
        AstNode *desig = item->designators[i];

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
        AstNode *desig = item->designators[i];
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
            u64 last;

            if (f->aggregate->kind != TY_ARRAY ||
                !init_designator_bounds(s, desig, &pos, &last))
                return false;
            if (desig->desig_range_end && f->aggregate->has_size &&
                last >= f->aggregate->size) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, desig->desig_range_end->span,
                          "array designator range exceeds array bounds");
                desig->desig_bounds_valid = false;
                return false;
            }
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

static bool init_item_ranges_valid(const AstNode *item, u64 *combinations)
{
    u64 count = 1;
    bool has_range = false;
    u32 i;

    for (i = 0; item && i < item->ndesignators; i++) {
        const AstNode *desig = item->designators[i];
        u64 width;

        if (!desig || !desig->desig_range_end)
            continue;
        has_range = true;
        if (!desig->desig_bounds_checked || !desig->desig_bounds_valid)
            return false;
        width =
            (u64)(desig->desig_range_end_value - desig->desig_index_value) + 1;
        if (width > UINT32_MAX || count > UINT32_MAX / width)
            count = (u64)UINT32_MAX + 1;
        else
            count *= width;
    }
    *combinations = has_range ? count : 0;
    return true;
}

static void init_expand_combinations(Sema *s, AstNode *item, u32 at,
                                     AstNode **selected, InitNodeVec *out)
{
    AstNode *desig;

    if (at == item->ndesignators) {
        AstNode *copy = arena_alloc(s->arena, sizeof(*copy), _Alignof(AstNode));

        *copy = *item;
        copy->init_range_origin =
            item->init_range_origin ? item->init_range_origin : item;
        copy->designators =
            arena_alloc(s->arena, item->ndesignators * sizeof(AstNode *),
                        _Alignof(AstNode *));
        memcpy(copy->designators, selected,
               item->ndesignators * sizeof(AstNode *));
        InitNodeVec_push(out, copy);
        return;
    }

    desig = item->designators[at];
    if (desig && desig->desig_range_end) {
        i64 index = desig->desig_index_value;

        for (;;) {
            AstNode *concrete =
                arena_alloc(s->arena, sizeof(*concrete), _Alignof(AstNode));

            *concrete = *desig;
            concrete->desig_range_end = NULL;
            concrete->desig_index_value = index;
            concrete->desig_range_end_value = index;
            concrete->desig_bounds_checked = true;
            concrete->desig_bounds_valid = true;
            selected[at] = concrete;
            init_expand_combinations(s, item, at + 1, selected, out);
            if (index == desig->desig_range_end_value)
                break;
            index++;
        }
        return;
    }
    selected[at] = desig;
    init_expand_combinations(s, item, at + 1, selected, out);
}

/* Normalize GNU ranges only after the initializer value has been typed once.
 * A range becomes ordinary concrete designators, including the Cartesian
 * product for chained ranges such as `[0 ... 1][2 ... 3]`.  The shared origin
 * on each shallow item copy lets lowering materialize every runtime leaf once
 * and reuse that SSA value for all selected subobjects. */
static void init_expand_ranges(Sema *s, AstNode *list)
{
    InitNodeVec expanded = {NULL, 0, 0};
    bool changed = false;
    u32 i;

    if (!list || list->kind != AST_INIT_LIST)
        return;
    for (i = 0; i < list->nitems; i++) {
        AstNode *item = list->items[i];
        u64 combinations = 0;

        if (!item || !init_item_ranges_valid(item, &combinations) ||
            combinations == 0) {
            InitNodeVec_push(&expanded, item);
            continue;
        }
        if (combinations > UINT32_MAX - expanded.len) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, item->span,
                      "range initializer expands to too many elements");
            InitNodeVec_push(&expanded, item);
            continue;
        }
        {
            AstNode **selected =
                arena_alloc(s->arena, item->ndesignators * sizeof(AstNode *),
                            _Alignof(AstNode *));

            init_expand_combinations(s, item, 0, selected, &expanded);
        }
        changed = true;
    }
    if (changed) {
        list->items = arena_alloc(s->arena, expanded.len * sizeof(AstNode *),
                                  _Alignof(AstNode *));
        memcpy(list->items, expanded.data, expanded.len * sizeof(AstNode *));
        list->nitems = (u32)expanded.len;
    }
    InitNodeVec_free(&expanded);
}

static bool init_expr_initializes_whole(Type *target, const AstNode *init)
{
    if (!target || !init)
        return false;
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_STRING)
        return true;
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_COMPOUND_LIT &&
        init->sem_type && init->sem_type->kind == TY_ARRAY)
        return type_array_initializer_compatible(target, init->sem_type);
    return init->sem_type && type_compatible(target, init->sem_type);
}

static bool init_is_compatible_array_compound_literal(Type *target,
                                                      const AstNode *init)
{
    return target && target->kind == TY_ARRAY && init &&
           init->kind == AST_EXPR_COMPOUND_LIT && init->sem_type &&
           init->sem_type->kind == TY_ARRAY &&
           type_array_initializer_compatible(target, init->sem_type);
}

/* An array declared without a bound is COMPLETED by its initializer
 * (6.7.9p22). Counting syntax items is correct only when the element type is
 * scalar: with `struct S a[] = { 1, 2, 3, 4 };`, brace elision makes several
 * scalar items initialize subobjects of ONE array element. Use the same
 * current-object cursor as initializer typing so bound inference cannot drift
 * from the stores it describes. The initializer has already been typed when
 * aggregate-valued expressions need `sem_type`; string literals are handled
 * directly because their terminator contributes to the bound. */
static bool array_size_from_init(Sema *s, Type *array, const AstNode *init,
                                 u64 *out)
{
    InitCursor cursor;
    u64 high = 0;
    u32 i;

    if (!array || array->kind != TY_ARRAY || !init)
        return false;
    /* GNU's static whole-array initializer copies the compound literal's
     * brace image. An incomplete destination consequently inherits the
     * source literal's already-completed bound, just as it does from a
     * direct initializer list. Do not count syntax again: the compound
     * literal owns its own current-object walk and designated bounds. */
    if (init_is_compatible_array_compound_literal(array, init) &&
        init->sem_type->has_size) {
        *out = init->sem_type->size;
        return true;
    }
    if (init->kind == AST_EXPR_STRING ||
        (init->kind == AST_INIT_LIST && init->nitems == 1 && init->items[0] &&
         init->items[0]->kind == AST_EXPR_STRING &&
         init->items[0]->ndesignators == 0)) {
        const AstNode *str =
            init->kind == AST_EXPR_STRING ? init : init->items[0];

        if (!str->tok)
            return false;
        *out = (u64)str->tok->str.nbytes + 1;
        return true;
    }
    if (init->kind != AST_INIT_LIST)
        return false;

    init_cursor_start(&cursor, array);
    for (i = 0; i < init->nitems; i++) {
        const AstNode *item = init->items[i];
        u64 outer;

        if (!item)
            continue;
        if (item->ndesignators &&
            !init_cursor_designate(s, &cursor, array, item))
            continue;
        if (!cursor.depth || !cursor.current)
            continue;
        outer = cursor.frames[0].pos;
        if (outer + 1 > high)
            high = outer + 1;

        if (item->kind == AST_INIT_LIST ||
            init_expr_initializes_whole(cursor.current, item)) {
            init_cursor_advance(&cursor);
            continue;
        }
        while (init_is_aggregate(cursor.current) &&
               !init_expr_initializes_whole(cursor.current, item))
            if (!init_cursor_descend(&cursor))
                break;
        init_cursor_advance(&cursor);
    }
    *out = high;
    return true;
}

static Member *flexible_array_member(Type *record)
{
    Member *m;

    if (!record || record->kind != TY_STRUCT || !record->tag ||
        !record->tag->has_fam)
        return NULL;
    for (m = record->tag->members; m; m = m->next)
        if (m->type && m->type->kind == TY_ARRAY && !m->type->has_size)
            return m;
    return NULL;
}

static void fam_note_cursor(const InitCursor *cursor, const Type *fam,
                            bool *found, u64 *high)
{
    u32 i;

    for (i = 0; i < cursor->depth; i++) {
        u64 next;

        if (cursor->frames[i].aggregate != fam)
            continue;
        *found = true;
        next = cursor->frames[i].pos + 1;
        if (next > *high)
            *high = next;
    }
}

/* GNU's static FAM initializer extension keeps the declared struct type
 * unchanged and appends storage for the initialized array elements. Walk the
 * same current-object model used by initializer typing so positional brace
 * elision, a whole string, and `[index]` designators all compute the same
 * extent as the stores they describe. */
static bool fam_size_from_init(Sema *s, Type *record, const AstNode *init,
                               Member **member_out, u64 *count_out)
{
    Member *fam_member = flexible_array_member(record);
    Type *fam;
    InitCursor cursor;
    bool found = false;
    u64 high = 0;
    u32 i;

    if (!fam_member || !init || init->kind != AST_INIT_LIST)
        return false;
    fam = fam_member->type;
    init_cursor_start(&cursor, record);
    for (i = 0; i < init->nitems; i++) {
        const AstNode *item = init->items[i];

        if (!item)
            continue;
        if (item->ndesignators &&
            !init_cursor_designate(s, &cursor, record, item))
            continue;
        if (!cursor.depth || !cursor.current)
            continue;

        fam_note_cursor(&cursor, fam, &found, &high);
        if (cursor.current == fam && (item->kind == AST_INIT_LIST ||
                                      init_expr_initializes_whole(fam, item))) {
            u64 count;

            found = true;
            if (array_size_from_init(s, fam, item, &count) && count > high)
                high = count;
            init_cursor_advance(&cursor);
            continue;
        }
        if (item->kind == AST_INIT_LIST ||
            init_expr_initializes_whole(cursor.current, item)) {
            init_cursor_advance(&cursor);
            continue;
        }
        while (init_is_aggregate(cursor.current) &&
               !init_expr_initializes_whole(cursor.current, item))
            if (!init_cursor_descend(&cursor))
                break;
        fam_note_cursor(&cursor, fam, &found, &high);
        init_cursor_advance(&cursor);
    }
    if (!found)
        return false;
    *member_out = fam_member;
    *count_out = high;
    return true;
}

/* GCC's nested-FAM extension changes which TYPES may be formed; it does not
 * create storage for a flexible tail buried inside another object. Detect an
 * initializer that reaches such a tail with the same current-object cursor
 * used for typing. This covers explicit braces, brace elision, and chained
 * designators without confusing the declared record's own direct FAM, whose
 * static-storage extension is handled by fam_size_from_init above. */
static bool cursor_reaches_nested_fam(const InitCursor *cursor, Type *root,
                                      bool declared_root)
{
    Member *direct = declared_root ? flexible_array_member(root) : NULL;
    u32 i;

    for (i = 0; i < cursor->depth; i++) {
        Type *aggregate = cursor->frames[i].aggregate;

        if (!aggregate || aggregate->kind != TY_ARRAY || aggregate->has_size ||
            aggregate->is_vla)
            continue;
        if (direct && direct->type == aggregate && i == 1 &&
            cursor->frames[0].aggregate == root)
            continue;
        return true;
    }
    return false;
}

static bool nested_fam_initialized(Sema *s, Type *target, const AstNode *init,
                                   bool declared_root)
{
    InitCursor cursor;
    Member *direct = declared_root ? flexible_array_member(target) : NULL;
    Member *unused_member;
    u64 unused_count;
    u32 i;

    if (!target || !init || init->kind != AST_INIT_LIST)
        return false;
    if (!declared_root &&
        fam_size_from_init(s, target, init, &unused_member, &unused_count))
        return true;

    init_cursor_start(&cursor, target);
    for (i = 0; i < init->nitems; i++) {
        const AstNode *item = init->items[i];

        if (!item)
            continue;
        if (item->ndesignators &&
            !init_cursor_designate(s, &cursor, target, item))
            continue;
        if (!cursor.depth || !cursor.current)
            continue;
        if (item->kind == AST_INIT_LIST) {
            bool initializes_direct =
                direct && cursor.depth == 1 && cursor.current == direct->type;

            if (cursor_reaches_nested_fam(&cursor, target, declared_root) ||
                (!initializes_direct &&
                 nested_fam_initialized(s, cursor.current, item, false)))
                return true;
            init_cursor_advance(&cursor);
            continue;
        }
        while (init_is_aggregate(cursor.current) &&
               !init_expr_initializes_whole(cursor.current, item))
            if (!init_cursor_descend(&cursor))
                break;
        if (cursor_reaches_nested_fam(&cursor, target, declared_root))
            return true;
        init_cursor_advance(&cursor);
    }
    return false;
}

static void sema_init_assign_typed(Sema *s, Type *target, AstNode **slot)
{
    AstNode *init;
    AstNode **designators;
    u32 ndesignators;
    AssignCtx ctx;

    if (!target || !slot || !*slot)
        return;
    init = *slot;
    /* A string literal initializes an array directly; it is not an
     * assignment to the array object. GNU gives a compatible array compound
     * literal the same whole-object treatment for static initialization.
     * Keeping the exception here is load-bearing: conv_assignable would
     * decay the literal to a pointer and both lose the source bound and
     * diagnose an array/pointer mismatch. */
    if (target->kind == TY_ARRAY && init->kind == AST_EXPR_STRING)
        return;
    if (s->static_init_depth &&
        init_is_compatible_array_compound_literal(target, init)) {
        if (s->lang->pedantic && !init->suppress_pedantic)
            warn_at(s->lang->warnings, WARN_PEDANTIC, init->span,
                    "initialization of an array from a compound literal is "
                    "a GNU extension");
        return;
    }
    designators = init->designators;
    ndesignators = init->ndesignators;
    memset(&ctx, 0, sizeof(ctx));
    ctx.kind = ACTX_INIT;
    conv_assignable(s, target, slot, ctx);
    if (*slot) {
        (*slot)->designators = designators;
        (*slot)->ndesignators = ndesignators;
    }
}

static AstNode *sema_init_scalar(Sema *s, Type *target, AstNode **slot)
{
    AstNode *init;
    AstNode **designators;
    u32 ndesignators;

    if (!slot || !*slot)
        return NULL;
    init = *slot;

    /* Designators belong to the initializer ITEM, not to the expression
     * nested inside it.  Expression typing and assignment conversion may
     * replace that item with an implicit cast (notably for `char *a[] = {
     * [7] = "x" }`).  Keep the parser's current-object metadata on the
     * replacement node so both the static image writer and runtime
     * initializer lowering still select the designated subobject. */
    designators = init->designators;
    ndesignators = init->ndesignators;
    *slot = sema_expr(s, init);
    sema_init_assign_typed(s, target, slot);
    if (*slot) {
        (*slot)->designators = designators;
        (*slot)->ndesignators = ndesignators;
    }
    return *slot;
}

static void sema_init_value(Sema *s, Type *target, AstNode **slot)
{
    AstNode *init;
    u32 i;

    if (!slot || !*slot)
        return;
    init = *slot;
    /* 6.7.9p14 permits an optional brace pair around a string literal used
     * to initialize a character array.  Normalize it here so the aggregate
     * cursor does not mistake the literal for the first scalar element;
     * the static-image and automatic-init paths already share the direct
     * string-array representation. */
    if (target && target->kind == TY_ARRAY && init->kind == AST_INIT_LIST &&
        init->nitems == 1 && init->items[0] &&
        init->items[0]->kind == AST_EXPR_STRING &&
        init->items[0]->ndesignators == 0) {
        *slot = init->items[0];
        sema_init_scalar(s, target, slot);
        return;
    }
    if (init->kind != AST_INIT_LIST) {
        sema_init_scalar(s, target, slot);
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

            item = sema_init_scalar(s, NULL, &init->items[i]);
            while (init_is_aggregate(cursor.current) &&
                   !init_expr_initializes_whole(cursor.current, item)) {
                if (!init_cursor_descend(&cursor))
                    break;
            }
            sema_init_assign_typed(s, cursor.current, &init->items[i]);
            init_cursor_advance(&cursor);
        }
        init_expand_ranges(s, init);
        return;
    }

    /* C permits one level of braces around a scalar initializer. Type all
     * entries for recovery, but only the first initializes the object. */
    for (i = 0; i < init->nitems; i++)
        sema_init_value(s, i == 0 ? target : NULL, &init->items[i]);
}

void sema_type_initializer(Sema *s, Type *target, AstNode **slot)
{
    sema_init_value(s, target, slot);
}

/* Types an initializer and checks each scalar element against its current
 * object. Materializing these conversions is load-bearing for both static
 * initializer bytes and the Sprint 38 conversion-warning postpass. */
static void sema_init_expr(Sema *s, Type *target, AstNode *d,
                           bool is_static_init)
{
    Member *fam_member = NULL;
    u64 fam_count = 0;

    if (!d->init)
        return;
    if (is_static_init)
        s->static_init_depth++;
    sema_type_initializer(s, target, &d->init);
    if (is_static_init)
        s->static_init_depth--;

    if (target && target->kind != TY_ERROR && type_contains_fam(target) &&
        nested_fam_initialized(s, target, d->init, true)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->init->span,
                  "initialization of flexible array member in a nested "
                  "context");
    }

    if (fam_size_from_init(s, target, d->init, &fam_member, &fam_count)) {
        TypeLayout record_layout;
        TypeLayout element_layout;

        if (!is_static_init) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->init->span,
                      "non-static initialization of a flexible array "
                      "member");
        } else {
            if (s->lang->pedantic)
                warn_at(s->lang->warnings, WARN_PEDANTIC, d->init->span,
                        "initialization of a flexible array member");
            layout_record(s, target);
            record_layout = layout_of(s, target);
            element_layout = layout_of(s, fam_member->type->base);
            if (element_layout.size != 0 &&
                fam_count >
                    (UINT64_MAX - record_layout.size) / element_layout.size) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, d->init->span,
                          "flexible array initializer is too large");
            } else if (d->sym) {
                /* GCC appends the payload to sizeof(record), even when the
                 * FAM begins inside the record's tail padding. That is why
                 * this is not `member.offset + payload`. */
                d->sym->init_storage_size =
                    record_layout.size + fam_count * element_layout.size;
            }
        }
    }

    /* An object with STATIC storage duration is initialized before any
     * code runs, so its initializer must be a CONSTANT — and an address
     * constant may not name an automatic object, which is the check that
     * keeps a stack address out of .data. This runs after typing because
     * an address constant needs its identifiers resolved first. */
    /* A COMPOUND LITERAL initializing an AGGREGATE is not a scalar constant
     * and must not be checked as one: `static struct S x = (struct S){1,2}`
     * is the literal's braces, folded into the object's image by
     * constexpr_eval_initializer, exactly as `= {1,2}` is. Checking it here
     * asked eval() for a value the literal does not have and rejected valid
     * C. The carve-out is deliberately narrow -- any OTHER non-list
     * initializer for an aggregate (`= f()`, `= other_struct`) still comes
     * through and still errors. */
    if (is_static_init && d->init->kind != AST_INIT_LIST && target &&
        target->kind != TY_ERROR && target->kind != TY_ARRAY &&
        !(d->init->kind == AST_EXPR_COMPOUND_LIT &&
          (target->kind == TY_STRUCT || target->kind == TY_UNION) &&
          d->init->sem_type && type_compatible(d->init->sem_type, target))) {
        ConstValue cv = constexpr_eval(
            s, d->init, target->kind == TY_PTR ? CE_ADDR : CE_ARITH);
        (void)cv; /* constexpr_eval reports the specific reason itself */
    }
}

static void finish_array_completion(Sema *s, AstNode *d, Symbol *sym,
                                    bool deferred)
{
    if (!deferred)
        return;
    sym->type = sema_array_complete_from_init(s, sym->type, d->init);
    d->sem_type = sym->type;
    if (!type_is_complete(sym->type)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "variable '%s' has incomplete type '%s'", d->name,
                  type_to_str(s->arena, sym->type));
    }
}

/* _Alignas (6.7.5). The constraints are the deliverable: it may not
 * WEAKEN an alignment, it may not appear on a typedef, a bitfield, a
 * parameter or a `register` object, and the value must be a power of two.
 * Returns the requested alignment, or 0 for "none". */
/* Folds an `aligned(N)` argument. Shared by all five represented positions --
 * record, member, object, function, and a declarator type layer -- because
 * copies would drift and the drift is invisible until one position disagrees
 * with another.
 *
 * The RULE that separates it from `_Alignas`: it only ever RAISES. Every
 * caller stores into an align_override field that is consumed with `>`, so a
 * request weaker than natural is declined by the consumer rather than being an
 * error here. That is why `aligned(1)` is not a spelling of `packed`.
 *
 * Returns 0 for "nothing requested", which is also what a zero or unfoldable
 * argument yields -- the diagnostic is emitted at the point of failure. */
static u64 gnu_aligned_value(Sema *s, const GnuDeclAttrs *g, Span span)
{
    i64 want = 0;

    if (!g)
        return 0;
    if (g->aligned_conflict) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, span,
                  "more than one 'aligned' attribute on one declaration is "
                  "not supported (gcc takes the largest)");
        return 0;
    }
    if (g->aligned_bare) {
        /* gcc's BIGGEST_ALIGNMENT, measured as 16 on x86-64 AND arm64-linux.
         * That is a different notion from max_align_t's alignment, but the two
         * coincide at 16 on all five targets, so this reuses the existing
         * field rather than adding a near-duplicate one. Split them the day a
         * target disagrees -- and it will be visible, because the bare form is
         * fixture-pinned per target. */
        return cgf_target_layout(s->target).max_align;
    }
    if (!g->aligned_expr)
        return 0;
    if (!enum_fold(s, g->aligned_expr, &want))
        return 0; /* already reported */
    if (want == 0)
        return 0;
    if (want < 0 || (want & (want - 1)) != 0) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, span,
                  "requested alignment %lld is not a power of two",
                  (long long)want);
        return 0;
    }
    if (!alignment_is_supported(s, span, want))
        return 0;
    return (u64)want;
}

/* Apply `mode(M)` to a declaration's type, or report why it cannot be.
 *
 * For plain integers the rule is one sentence, measured rather than read:
 * the MODE supplies the width and the DECLARATION supplies the signedness.
 * A `typedef int r` carrying `__mode__(__word__)` is exactly `long` on LP64
 * -- gcc's types_compatible_p says identical, not merely the same size --
 * and the `unsigned` spelling gives exactly `unsigned long`. Enums use the
 * same width mapping but derive signedness from their enumerator range.
 *
 * Picking the FIRST basic type of that width in rank order is what makes
 * that true: `long` and `long long` are both 8 bytes here, and gcc chooses
 * `long`. Deriving each width from the target rather than a table is what
 * keeps a cross-compile honest.
 *
 * Everything that is not an integer type is gcc's own "applied to
 * inappropriate type" -- including a function and a pointer. gcc does
 * accept the pointer case (a pointer is already DI, so it is a no-op
 * there), and refusing it is the safe direction: a clean error on a
 * construct no header in /usr/include uses, rather than a guess. */
static bool enum_mode_values_fit(Sema *s, const Type *t, const Type *repr)
{
    const AstNode *rec = t && t->tag ? t->tag->enum_ast : NULL;
    u32 bits = conv_int_bits(s, repr);
    bool is_signed = conv_is_signed(s, repr);
    u32 i;

    if (!rec)
        return false;
    for (i = 0; i < rec->nmembers; i++) {
        const AstNode *m = rec->members[i];
        i64 value;

        if (!m || !m->sym)
            continue;
        value = m->sym->enum_value;
        if (is_signed) {
            i64 min;
            i64 max;

            if (bits >= 64)
                continue;
            min = -((i64)1 << (bits - 1));
            max = ((i64)1 << (bits - 1)) - 1;
            if (value < min || value > max)
                return false;
        } else {
            u64 max = bits >= 64 ? ~0ull : ((1ull << bits) - 1);

            if (value < 0 || (u64)value > max)
                return false;
        }
    }
    return true;
}

static void enum_mode_retype_constants(Sema *s, Type *t, Type *repr)
{
    const AstNode *rec = t && t->tag ? t->tag->enum_ast : NULL;
    IntWidths w = cgf_target_int_widths(s->target);
    i64 int_max = ((i64)1 << (w.int_bits - 1)) - 1;
    i64 int_min = -((i64)1 << (w.int_bits - 1));
    u32 i;

    if (!rec)
        return;
    for (i = 0; i < rec->nmembers; i++) {
        AstNode *m = rec->members[i];

        if (m && m->sym &&
            (m->sym->enum_value < int_min || m->sym->enum_value > int_max))
            m->sym->type = repr;
    }
}

static Type *gnu_mode_apply(Sema *s, Type *t, const GnuDeclAttrs *g,
                            bool binds_enum_definition, Span span)
{
    static const TypeKind by_rank[] = {TY_SCHAR, TY_SHORT, TY_INT, TY_LONG,
                                       TY_LLONG};
    static const TypeKind by_rank_u[] = {TY_UCHAR, TY_USHORT, TY_UINT, TY_ULONG,
                                         TY_ULLONG};
    u64 want = 0;
    bool is_signed;
    size_t i;

    if (!g || g->mode == GNU_MODE_NONE || !t)
        return t;
    if (t->kind == TY_ERROR)
        return t;
    /* TWO KINDS OF NO, and they must not share a message. gcc REJECTS a
     * mode on a function, a _Bool or a floating type, so those take gcc's
     * own wording. gcc ACCEPTS it on a pointer and we do not -- borrowing
     * the rejection wording there would tell the user their program is
     * invalid C when it is only unsupported here. */
    if (t->kind == TY_PTR) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, span,
                  "the 'mode' attribute is not supported on a pointer: only a "
                  "plain integer declaration can take one "
                  "(docs/gnu-extensions.md)");
        return t;
    }
    if (t->kind == TY_ENUM && (!t->tag || !t->tag->complete)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, span,
                  "the 'mode' attribute on an incomplete enumerated type is "
                  "not supported");
        return t;
    }
    if (!type_is_integer(t) || t->kind == TY_BOOL) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, span,
                  "mode applied to inappropriate type '%s'",
                  type_to_str(s->arena, t));
        return t;
    }
    switch ((GnuMode)g->mode) {
    case GNU_MODE_QI:
    case GNU_MODE_BYTE:
        want = 1;
        break;
    case GNU_MODE_HI:
        want = 2;
        break;
    case GNU_MODE_SI:
        want = 4;
        break;
    case GNU_MODE_DI:
        want = 8;
        break;
    case GNU_MODE_WORD:
    case GNU_MODE_POINTER:
        /* Both are the target's natural word on every target we have. The
         * layout is asked rather than assumed so a future ILP32 target
         * gets the right answer instead of a silently wide typedef. */
        want = cgf_target_layout(s->target).ptr_size;
        break;
    case GNU_MODE_NONE:
        return t;
    }
    /* GCC chooses the unsigned representation when every enumerator is
     * nonnegative and the signed one when any enumerator is negative. A
     * plain integer declaration continues to supply signedness itself. */
    is_signed =
        t->kind == TY_ENUM ? t->tag->enum_has_negative : conv_is_signed(s, t);
    for (i = 0; i < sizeof(by_rank) / sizeof(by_rank[0]); i++) {
        Type *cand = type_basic(is_signed ? by_rank[i] : by_rank_u[i]);

        if (layout_of(s, cand).size == want) {
            Type *mapped = type_qualify(s->arena, cand, t->quals);

            mapped = type_with_alignment(s->arena, mapped, t->align_override);
            if (t->may_alias)
                mapped = type_may_alias(s->arena, mapped);
            if (t->kind == TY_ENUM) {
                if (!enum_mode_values_fit(s, t, mapped)) {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, span,
                              "specified mode too small for enumerated "
                              "values");
                    return t;
                }
                if (binds_enum_definition) {
                    t->tag->enum_underlying = mapped;
                    enum_mode_retype_constants(s, t, mapped);
                    return t;
                }
                return type_enum_with_repr(s->arena, t, mapped);
            }
            return mapped;
        }
    }
    s->nerrors++;
    diag_emit(s->dc, DIAG_ERROR, span,
              "no integer type of %llu bytes exists on this target",
              (unsigned long long)want);
    return t;
}

/* Fold and range-check one `constructor`/`destructor` priority.
 *
 * The range is gcc's, and so is the split within it: 0..65535 is legal, but
 * 0..100 are reserved for the implementation and merely warn. NULL is the
 * unprioritized form. Measured: gcc emits the SAME plain `.init_array` for the
 * bare form and for an explicit 65535, so the default is not a sentinel
 * standing in for "none" -- it is a real priority that happens to be the top
 * of the range. */
static u16 gnu_ctor_priority(Sema *s, AstNode *expr, bool is_ctor, Span span)
{
    i64 want = 0;

    if (!expr)
        return (u16)CGF_INIT_PRIORITY_DEFAULT;
    if (!enum_fold(s, expr, &want))
        return (u16)CGF_INIT_PRIORITY_DEFAULT; /* already reported */
    if (want < 0 || want > (i64)CGF_INIT_PRIORITY_DEFAULT) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, span,
                  "%s priorities must be integers from 0 to %u inclusive",
                  is_ctor ? "constructor" : "destructor",
                  CGF_INIT_PRIORITY_DEFAULT);
        return (u16)CGF_INIT_PRIORITY_DEFAULT;
    }
    if (want <= (i64)CGF_INIT_PRIORITY_RESERVED_MAX)
        warn_at(s->lang->warnings, WARN_PRIO_CTOR_DTOR, span,
                "%s priorities from 0 to %u are reserved for the "
                "implementation",
                is_ctor ? "constructor" : "destructor",
                CGF_INIT_PRIORITY_RESERVED_MAX);
    return (u16)want;
}

/* `constructor`/`destructor` name a function to run around `main`. On anything
 * else there is nothing to run, and gcc says so and drops the attribute rather
 * than erroring -- so a header that puts one on the wrong declaration still
 * compiles. */
static void check_ctor_dtor(Sema *s, Symbol *sym, AstNode *d)
{
    bool on_function = sym->kind == SYM_FUNC;
    int i;

    for (i = 0; i < 2; i++) {
        bool is_ctor = i == 0;
        bool *flag = is_ctor ? &sym->gnu.constructor : &sym->gnu.destructor;
        AstNode *expr =
            is_ctor ? sym->gnu.ctor_priority : sym->gnu.dtor_priority;
        u16 *slot = is_ctor ? &sym->ctor_prio : &sym->dtor_prio;

        if (!*flag)
            continue;
        if (!on_function) {
            warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                    "'%s' attribute ignored",
                    is_ctor ? "constructor" : "destructor");
            *flag = false;
            continue;
        }
        *slot = gnu_ctor_priority(s, expr, is_ctor, d->span);
    }
}

/* `cleanup(func)` runs `func(&var)` when the variable's scope exits, so it
 * needs a scope to exit and an object to take the address of. An automatic
 * block-scope variable is the only declaration that has both: a global, a
 * static local, a `_Thread_local` and a parameter all outlive every scope
 * exit that could fire it, and gcc drops the attribute with a warning rather
 * than erroring, so a header carrying one on the wrong declaration still
 * compiles.
 *
 * THE ARGUMENT CHECK IS THE ORDINARY CALL CHECK, deliberately. gcc reports a
 * cleanup function taking `long *` for an `int` variable as the everyday
 * incompatible-pointer diagnostic, not as a bespoke signature complaint, and
 * an array variable reports `int (*)[3]` — the real type of `&var`. Writing a
 * dedicated comparison here would drift from the one in expr.c and would
 * report a different sentence for the same mistake; synthesizing the argument
 * and handing it to conv_assignable cannot. */
static void check_cleanup(Sema *s, Symbol *sym, AstNode *d, bool file_scope)
{
    Symbol *fn;
    Type *ft;

    if (!sym->gnu.cleanup_fn)
        return;
    if (sym->kind != SYM_VAR || sym->is_param || file_scope ||
        (d->storage & (AST_SC_STATIC | AST_SC_EXTERN | AST_SC_THREAD_LOCAL))) {
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'cleanup' attribute ignored");
        sym->gnu.cleanup_fn = NULL;
        return;
    }

    fn = scope_lookup(s->scope, sym->gnu.cleanup_fn, NS_ORDINARY);
    /* One diagnostic for both "no such name" and "that name is not a
     * function", which is gcc's own wording and its own conflation: an
     * undeclared identifier here reports THIS rather than the ordinary
     * undeclared-identifier error, and a function POINTER variable reports it
     * too — `cleanup` takes a function, and a pointer to one is not it. */
    if (!fn || fn->kind != SYM_FUNC) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "cleanup argument not a function");
        sym->gnu.cleanup_fn = NULL;
        return;
    }

    ft = fn->type;
    if (ft && ft->kind == TY_FUNC && ft->has_proto) {
        if (ft->nparams != 1) {
            /* gcc's arity wording, from the ordinary call path. */
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "too %s arguments to function '%s'; expected %u, have 1",
                      ft->nparams < 1 ? "many" : "few", fn->name,
                      (unsigned)ft->nparams);
            sym->gnu.cleanup_fn = NULL;
            return;
        }
        if (sym->type) {
            /* The synthetic `&var`. It exists only to be type-checked and is
             * never lowered — lowering builds the address from the variable's
             * own slot — so it needs a type and a span and nothing else. */
            AstNode *addr = ast_new(s->arena, AST_EXPR_UNARY, d->span);
            AssignCtx ctx;

            addr->sem_type = type_ptr(s->arena, sym->type);
            memset(&ctx, 0, sizeof(ctx));
            ctx.kind = ACTX_ARG;
            ctx.arg_index = 1;
            ctx.callee = fn->name;
            conv_assignable(s, ft->params[0], &addr, ctx);
        }
    }
    sym->cleanup_fn = fn;
}

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

    if (!alignment_is_supported(s, d->span, want))
        return 0;

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

/* Carry every SYMBOL PROPERTY from a redeclaration onto the symbol that
 * survives it.
 *
 * declare_one always builds a fresh Symbol and validates the declaration's
 * attributes onto it; when a prior declaration exists, that fresh symbol is
 * discarded and `prev` is what lowering reads (`d->sym = prev`). Everything
 * decided on the fresh one therefore has to be moved across, and until this
 * function existed none of it was: `weak`, `used`, `aligned`, `section` and
 * `__asm__` labels were ALL silently dropped whenever the attribute sat on a
 * definition that had a plain prior declaration --
 *
 *     void f(void);
 *     [[weak-attribute]] void f(void) { }      -> emitted GLOBAL, not WEAK
 *
 * which is exactly musl's weak_alias shape, and precisely the case the comment
 * on gnu_attrs_merge claims to handle. The bug was invisible because every
 * fixture wrote the attribute and the definition together.
 *
 * The rule per field is the one that field already documents: union for the
 * flags, MAX for alignment (it may only ever raise), last-wins for the
 * string-valued ones. An alias also carries its defining side effects, since
 * naming a target is what makes the declaration a definition. */
/* `deprecated` fires at the USE, and there are THREE kinds of use that
 * reach it -- an ordinary identifier, a typedef name, and a struct member.
 * One helper rather than three copies, because the message format is the
 * thing that would drift: gcc prints `'f' is deprecated` bare and
 * `'f' is deprecated: why` with a reason, and an empty string is still the
 * second form.
 *
 * NOT called for a deprecated ENUMERATOR: gcc does not warn for one in C,
 * measured, and a walk over every declared name would. */
void sema_warn_deprecated(Sema *s, const char *name, bool deprecated,
                          const char *msg, Span sp)
{
    if (!deprecated)
        return;
    if (msg)
        warn_at(s->lang->warnings, WARN_DEPRECATED_DECLARATIONS, sp,
                "'%s' is deprecated: %s", name ? name : "?", msg);
    else
        warn_at(s->lang->warnings, WARN_DEPRECATED_DECLARATIONS, sp,
                "'%s' is deprecated", name ? name : "?");
}

static void carry_symbol_attrs(Symbol *prev, const Symbol *fresh)
{
    gnu_attrs_merge(&prev->gnu, &fresh->gnu);
    if (fresh->align_override > prev->align_override)
        prev->align_override = fresh->align_override;
    if (fresh->section_name)
        prev->section_name = fresh->section_name;
    if (fresh->asm_name)
        prev->asm_name = fresh->asm_name;
    if (fresh->asm_register_name)
        prev->asm_register_name = fresh->asm_register_name;
    if (fresh->gnu.constructor)
        prev->ctor_prio = fresh->ctor_prio;
    if (fresh->gnu.destructor)
        prev->dtor_prio = fresh->dtor_prio;
    if (fresh->alias_target) {
        prev->alias_target = fresh->alias_target;
        prev->alias_span = fresh->alias_span;
    }
}

static void mark_old_style_definition(AstNode *d, Type *type)
{
    const AstType *ft = d->type;

    if (d->kind != AST_FUNC_DEF || !type || type->kind != TY_FUNC ||
        type->has_proto || !ft || ft->kind != ATY_FUNC)
        return;
    type->old_style_definition = true;
    type->kr_definition = ft->is_kr_list;
}

static void declare_one(Sema *s, AstNode *d)
{
    bool auto_type_decl;
    Type *type;
    Symbol *sym;
    Symbol *prev;
    Symbol *visible;
    bool is_func;
    bool static_init;
    bool file_scope = s->scope->kind == SCOPE_FILE;
    bool had_prior_prototype = false;
    bool deferred_array_completion;
    u64 alignas_req;

    if (!d || !d->name)
        return;
    if (d->poisoned)
        return; /* Sprint 11: never diagnose about a poisoned subtree */

    /* __auto_type: THE TYPE IS THE INITIALIZER'S, so it can only be resolved
     * here, where the initializer is in hand -- type_from_ast has no way to
     * reach it and returns TY_ERROR for the specifier on purpose.
     *
     * LVALUE CONVERSION IS THE POINT, and it is where __auto_type parts
     * company with typeof: `const int c; __auto_type k = c;` gives a MUTABLE
     * int, while `typeof(c) k` gives a const one. Both measured against gcc
     * -- assuming they agreed would have been wrong.
     *
     * gcc's three constraints, measured: an initializer is required, the
     * declarator must be a PLAIN IDENTIFIER (`__auto_type *p` is rejected),
     * and only one declarator may share the specifier. */
    {
        /* Walk to the BASE. `__auto_type *p` wraps the specifier in an
         * ATY_PTR, so testing the outermost node misses it -- and missing
         * it means TY_ERROR reaches lowering as an ICE instead of gcc's
         * "requires a plain identifier as declarator". */
        const AstType *bt = d->type;

        while (bt && bt->kind != ATY_BASE)
            bt = bt->next;
        auto_type_decl = bt && bt->base == ABT_AUTO_TYPE;
    }
    if (auto_type_decl) {
        if (!d->init) {
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "'__auto_type' requires an initialized data "
                      "declaration");
            s->nerrors++;
            type = type_basic(TY_ERROR);
        } else if (d->type->kind != ATY_BASE) {
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "'__auto_type' requires a plain identifier as "
                      "declarator");
            s->nerrors++;
            type = type_basic(TY_ERROR);
        } else {
            AstNode *init = d->init;

            if (init->kind == AST_INIT_LIST) {
                diag_emit(s->dc, DIAG_ERROR, d->span,
                          "'__auto_type' cannot deduce a type from a braced "
                          "initializer list");
                s->nerrors++;
                type = type_basic(TY_ERROR);
            } else {
                d->init = init = conv_lvalue(s, sema_expr(s, init));
                type = init && init->sem_type ? init->sem_type
                                              : type_basic(TY_ERROR);
            }
        }
    } else {
        type = type_from_ast(s, d->type, d->span);
    }
    /* Before anything reads the type -- alignment, completeness, layout,
     * the symbol -- because `mode` REPLACES it. A typedef, an object and a
     * function all arrive here; the function is what gnu_mode_apply's
     * inappropriate-type error catches, matching gcc. */
    type = gnu_mode_apply(s, type, &d->gnu, false, d->span);
    mark_old_style_definition(d, type);
    /* gcc gives directly-written `may_alias` semantics on a typedef (and on
     * record definitions, handled by complete_struct), not on an ordinary
     * object declaration. Keeping that distinction also means a typedef of
     * a pointer marks the pointer type without incorrectly marking its
     * pointee. */
    if ((d->storage & AST_SC_TYPEDEF) && d->gnu.may_alias) {
        if (type && (type->kind == TY_STRUCT || type->kind == TY_UNION ||
                     type->kind == TY_ENUM)) {
            /* GCC accepts the spelling but ignores it after a tag has
             * already named the aggregate. The effective record positions
             * are on the definition itself, where complete_struct handles
             * them. Treating this typedef as aliasable would retain correct
             * code but discard TBAA proofs GCC is licensed to use. */
            warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                    "'may_alias' attribute ignored");
        } else {
            type = type_may_alias(s->arena, type);
        }
    }
    is_func = type && type->kind == TY_FUNC;
    alignas_req = check_alignas(s, d, type);

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
    /* The current-object walk needs expression types to distinguish brace
     * elision from one aggregate-valued initializer. Defer only this one
     * completeness event until the declaration is in scope and its
     * initializer has been typed; every other incomplete definition keeps
     * the ordinary immediate diagnostic below. */
    deferred_array_completion = !is_func && sym->kind != SYM_TYPEDEF &&
                                d->init && type && type->kind == TY_ARRAY &&
                                !type->has_size && !type->is_vla;

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

    /* GNU89 makes its inverted inline model the dialect default. In newer
     * GNU modes it is selected declaration-by-declaration by gnu_inline,
     * which is how glibc keeps GNU emission while compiling as gnu17. */
    if (is_func && (d->func_specs & AST_FS_INLINE) && s->lang->std == STD_GNU89)
        d->gnu.gnu_inline = true;

    /* `gnu_inline` only has meaning on an inline FUNCTION declaration.
     * Keeping it on a plain prototype would be worse than merely missing a
     * warning: a later definition could inherit it and select the opposite
     * emission rule. gcc warns and drops it at the declaration where it is
     * misplaced. */
    if (d->gnu.gnu_inline &&
        (!is_func || (d->func_specs & AST_FS_INLINE) == 0)) {
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'gnu_inline' attribute ignored");
        d->gnu.gnu_inline = false;
    }

    /* GCC retains always_inline on a FUNCTION even without the inline
     * specifier (and still forces direct calls), but warns because that shape
     * also emits an ordinary out-of-line definition.  On every other
     * declaration there is no call target to transform, so warn and drop it
     * before the symbol-property union can poison a later redeclaration. */
    if (d->gnu.always_inline && sym->kind != SYM_FUNC) {
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'always_inline' attribute ignored");
        d->gnu.always_inline = false;
    } else if (d->gnu.always_inline && (d->func_specs & AST_FS_INLINE) == 0) {
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'always_inline' function might not be inlinable unless also "
                "declared 'inline'");
    }

    /* UNION across declarations, not replacement: an attribute on any
     * declaration of a symbol applies to the symbol. gcc's rule, and the
     * one musl's weak_alias pattern depends on -- the attribute and the
     * definition are routinely in different places. */
    gnu_attrs_merge(&sym->gnu, &d->gnu);
    if (sym->gnu.packed) {
        /* Reaching an ORDINARY declaration means the attribute named neither
         * a record definition nor a member, so there is nothing to pack.
         * gcc's own wording, and gcc's own flag. */
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'packed' attribute ignored");
        sym->gnu.packed = false;
    }
    if (sym->gnu.weak && sym->linkage == LINK_INTERNAL) {
        /* A static symbol has no binding for the linker to weaken, and gcc
         * says so rather than emitting a .weak that does nothing. */
        warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                "'weak' attribute ignored on a symbol with internal linkage");
        sym->gnu.weak = false;
    }

    /* MAX across declarations, like the inline matrix: two declarations of
     * one object are one object, and _Alignas may only ever RAISE. Before
     * this the value was computed, validated and dropped on the floor.
     *
     * `aligned` folds into the same field. The two spellings differ in what a
     * WEAKENING means -- a constraint violation for _Alignas, a silent decline
     * for aligned -- and taking the max is both. */
    {
        u64 ga = gnu_aligned_value(s, &d->gnu, d->span);

        if (ga > alignas_req)
            alignas_req = ga;
    }
    if (alignas_req > sym->align_override)
        sym->align_override = alignas_req;

    if (d->gnu.section_name)
        sym->section_name = intern_str(
            s->interner, intern_cstr(s->interner, d->gnu.section_name));

    check_ctor_dtor(s, sym, d);
    check_cleanup(s, sym, d, file_scope);

    if (d->gnu.asm_name) {
        const char *name =
            intern_str(s->interner, intern_cstr(s->interner, d->gnu.asm_name));
        bool automatic_local = !file_scope && sym->kind == SYM_VAR &&
                               (d->storage & (AST_SC_STATIC | AST_SC_EXTERN |
                                              AST_SC_THREAD_LOCAL)) == 0;

        if (automatic_local && (d->storage & AST_SC_REGISTER)) {
            /* GNU local register variables are not symbol renames. Their
             * guarantee is intentionally limited to direct extended-asm
             * operands, where lower_asm turns the ordinary `r` constraint
             * into a fixed-register one. musl's syscall arguments 4-6 use
             * precisely this contract. */
            sym->asm_register_name = name;
        } else if (automatic_local) {
            /* GCC accepts this spelling but ignores it unless the automatic
             * variable also has `register` storage. Do not leak the name into
             * lower_link_name: an automatic object has no linker symbol. */
            warn_at(s->lang->warnings, WARN_ATTRIBUTES, d->span,
                    "ignoring asm specifier for non-register local variable "
                    "'%s'",
                    d->name);
        } else {
            sym->asm_name = name;
        }
    }

    if (d->gnu.alias_target) {
        /* Interned HERE: the parser has no interner, and this is the one
         * place that needs pointer identity against other symbol names. */
        sym->alias_target = intern_str(
            s->interner, intern_cstr(s->interner, d->gnu.alias_target));
        sym->alias_span = d->span;
        /* An alias DEFINES its name -- it emits a symbol -- but has no body
         * and no initializer of its own, so nothing downstream should treat
         * it as a tentative definition to be zero-filled. */
        sym->defined = true;
        sym->tentative = false;
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
        type->kind != TY_ERROR && !type_is_complete(type) &&
        !deferred_array_completion) {
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
    /* GNU permits an `extern void marker;` declaration for linker-defined
     * boundary symbols whose only useful operation is taking their address.
     * It still does not define storage: every void definition, tentative
     * definition, or non-extern block object remains a constraint error. */
    if (!is_func && sym->kind != SYM_TYPEDEF && type && type->kind == TY_VOID &&
        (sym->defined || !(d->storage & AST_SC_EXTERN))) {
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
    sym->func_def_inline =
        d->kind == AST_FUNC_DEF && (d->func_specs & AST_FS_INLINE) != 0;
    sym->func_def_extern =
        d->kind == AST_FUNC_DEF && (d->storage & AST_SC_EXTERN) != 0;
    sym->func_def = d->kind == AST_FUNC_DEF ? d : NULL;

    prev = scope_lookup_local(s->scope, d->name, NS_ORDINARY);
    if (prev) {
        /* Once an inline declaration selects GNU89 semantics, every later
         * inline declaration must say the same thing. A prior NON-inline
         * prototype is intentionally exempt: that is the shape glibc uses. */
        if (is_func && (d->func_specs & AST_FS_INLINE) &&
            (prev->func_specs & AST_FS_INLINE) &&
            prev->gnu.gnu_inline != sym->gnu.gnu_inline) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, d->span,
                      "'gnu_inline' attribute disagrees with an earlier "
                      "inline declaration of '%s'",
                      d->name);
        }
        /* The inline decision needs EVERY declaration (6.7.4p7), so the
         * surviving symbol accumulates across the merge. */
        prev->func_specs |= d->func_specs;
        prev->all_decls_inline =
            prev->all_decls_inline && (d->func_specs & AST_FS_INLINE) != 0;
        prev->any_decl_extern =
            prev->any_decl_extern || (d->storage & AST_SC_EXTERN) != 0;
        prev->func_def_inline |= sym->func_def_inline;
        prev->func_def_extern |= sym->func_def_extern;
        if (sym->func_def)
            prev->func_def = sym->func_def;
        if (d->storage & AST_SC_THREAD_LOCAL)
            prev->tls = true;
        carry_symbol_attrs(prev, sym);
        append_valid_attrs(s, prev, d, type);
        merge_redeclaration(s, prev, sym, d->storage);
        d->sym = prev; /* lowering resolves the DECL to its symbol */
        /* The initializer still has to be TYPED even when the declaration
         * merged into an earlier one, or its expressions never get
         * checked at all. */
        sema_init_expr(s, prev->type, d, static_init);
        finish_array_completion(s, d, prev, deferred_array_completion);
        return;
    }
    append_valid_attrs(s, sym, d, type);
    scope_declare(s, sym);
    d->sym = sym; /* lowering resolves the DECL to its symbol */
    sema_init_expr(s, sym->type, d, static_init);
    finish_array_completion(s, d, sym, deferred_array_completion);
}

/* A forwarding-pack wrapper is specialized more than once, potentially in
 * one caller. Automatic locals are naturally fresh at each expansion, but a
 * local static must denote ONE program-wide object and a VLA's cached size is
 * keyed by its semantic Type node. Labels likewise belong to the containing
 * function's one label namespace and lowering pre-collects only that
 * function's labels. va_start would inspect the containing caller's ABI
 * state after specialization rather than the captured pack. Refuse those
 * shapes here instead of silently duplicating storage, reusing a stale VLA
 * extent, branching into the caller's label map, or reading its varargs. */
static void validate_va_pack_body_node(Sema *s, const Symbol *fn, AstNode *n)
{
    u32 i;

    if (!n)
        return;
    if (n->kind == AST_STMT_GOTO || n->kind == AST_STMT_LABEL) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, n->span,
                  "function '%s' uses a variadic argument pack and cannot "
                  "contain labels or goto statements",
                  fn->name);
    }
    if (n->kind == AST_EXPR_CALL && n->op == SEMA_BUILTIN_VA_START) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, n->span,
                  "function '%s' uses a variadic argument pack and cannot "
                  "also use va_start",
                  fn->name);
    }
    if (n->kind == AST_DECL) {
        Type *t = n->sem_type ? n->sem_type : (n->sym ? n->sym->type : NULL);

        if ((n->storage & AST_SC_STATIC) ||
            ((n->storage & AST_SC_THREAD_LOCAL) &&
             !(n->storage & AST_SC_EXTERN))) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, n->span,
                      "function '%s' uses a variadic argument pack and "
                      "cannot contain a local static object",
                      fn->name);
        }
        if (t && type_contains_vla(t)) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, n->span,
                      "function '%s' uses a variadic argument pack and "
                      "cannot contain a variably modified declaration",
                      fn->name);
        }
    }

    validate_va_pack_body_node(s, fn, n->lhs);
    validate_va_pack_body_node(s, fn, n->mid);
    validate_va_pack_body_node(s, fn, n->rhs);
    validate_va_pack_body_node(s, fn, n->init);
    validate_va_pack_body_node(s, fn, n->body);
    validate_va_pack_body_node(s, fn, n->desig_index);
    validate_va_pack_body_node(s, fn, n->desig_range_end);
    for (i = 0; i < n->nargs; i++)
        validate_va_pack_body_node(s, fn, n->args[i]);
    for (i = 0; i < n->nitems; i++)
        validate_va_pack_body_node(s, fn, n->items[i]);
    for (i = 0; i < n->ndesignators; i++)
        validate_va_pack_body_node(s, fn, n->designators[i]);
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
static u32 kr_param_index(const AstType *ft, const char *name)
{
    u32 i;

    for (i = 0; i < ft->nparams; i++)
        if (ft->params[i].name == name)
            return i;
    return (u32)-1;
}

static Type *adjust_param_type(Sema *s, Type *pt)
{
    if (pt && pt->kind == TY_ARRAY)
        return type_ptr(s->arena, pt->base);
    if (pt && pt->kind == TY_FUNC)
        return type_ptr(s->arena, pt);
    return pt;
}

/* A definition's parameter declarators reuse the parser AST that was first
 * typed while building the function type in prototype scope. The expression
 * nodes therefore still point at the short-lived prototype symbols even
 * after type_from_ast returns early on their existing sem_type. Retarget only
 * those parameter references to the definition-scope symbols; the expression
 * was already fully checked, so replaying sema would duplicate diagnostics
 * and implicit conversions. */
static void rebind_parameter_refs(Sema *s, AstNode *e);

static void rebind_parameter_ast_type(Sema *s, const AstType *at)
{
    u32 i;

    if (!at)
        return;
    rebind_parameter_ast_type(s, at->next);
    rebind_parameter_ast_type(s, at->atomic_inner);
    rebind_parameter_ast_type(s, at->typeof_type);
    rebind_parameter_refs(s, at->typeof_expr);
    rebind_parameter_refs(s, at->ptr_aligned_expr);
    rebind_parameter_refs(s, at->array_size);
    for (i = 0; i < at->nparams; i++)
        rebind_parameter_ast_type(s, at->params[i].type);
}

static void rebind_parameter_refs(Sema *s, AstNode *e)
{
    u32 i;

    if (!e)
        return;
    if (e->kind == AST_EXPR_IDENT && e->sym && e->sym->is_param) {
        Symbol *body = scope_lookup(s->scope, e->name, NS_ORDINARY);

        if (body && body->is_param && body != e->sym) {
            e->sym = body;
            e->sem_type = body->type;
            body->reads++;
        }
    }
    rebind_parameter_ast_type(s, e->type);
    rebind_parameter_ast_type(s, e->type2);
    rebind_parameter_refs(s, e->lhs);
    rebind_parameter_refs(s, e->mid);
    rebind_parameter_refs(s, e->rhs);
    rebind_parameter_refs(s, e->init);
    rebind_parameter_refs(s, e->body);
    rebind_parameter_refs(s, e->desig_index);
    rebind_parameter_refs(s, e->desig_range_end);
    for (i = 0; i < e->nargs; i++)
        rebind_parameter_refs(s, e->args[i]);
    for (i = 0; i < e->nitems; i++)
        rebind_parameter_refs(s, e->items[i]);
    for (i = 0; i < e->ndesignators; i++)
        rebind_parameter_refs(s, e->designators[i]);
}

static void rebind_parameter_type(Sema *s, Type *t)
{
    for (; t && (t->kind == TY_ARRAY || t->kind == TY_PTR); t = t->base)
        if (t->kind == TY_ARRAY && t->size_expr)
            rebind_parameter_refs(s, t->size_expr);
}

static void bind_kr_param(Sema *s, AstNode *d, const AstType *ft, u32 pi,
                          Type *pt)
{
    const char *pname = ft->params[pi].name;
    Symbol *ps =
        sym_new(s, pname, SYM_VAR, NS_ORDINARY, pt, ft->params[pi].span);

    ps->is_param = true;
    ps->defined = true;
    if (scope_lookup_local(s->scope, pname, NS_ORDINARY)) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, ft->params[pi].span,
                  "redefinition of parameter '%s'", pname);
        return;
    }
    scope_declare(s, ps);
    if (d->param_syms && pi < d->nparam_syms)
        d->param_syms[pi] = ps;
}

static void declare_kr_params(Sema *s, AstNode *d, Symbol *fsym)
{
    const AstType *ft = d->type;
    Type *definition = d->sem_type;
    Type *proto = NULL;
    u32 ki, pi;

    /* Resolve inside the already-pushed function scope, in identifier-list
     * order. A later VLA parameter may depend on an earlier parameter name;
     * eager file-scope capture incorrectly rejects that valid K&R shape. */
    if (definition && definition->kind == TY_FUNC) {
        definition->nold_style_params = ft->nparams;
        if (ft->nparams)
            definition->old_style_params = arena_alloc(
                s->arena, ft->nparams * sizeof(Type *), _Alignof(Type *));
        if (definition->old_style_params)
            memset(definition->old_style_params, 0,
                   ft->nparams * sizeof(Type *));
        /* With only preceding unprototyped declarations, merging may have
         * produced a distinct surviving Type. Point it at the same signature
         * storage so a later prototype sees the resolved definition. */
        if (fsym && fsym->type && fsym->type->kind == TY_FUNC &&
            !fsym->type->has_proto) {
            fsym->type->old_style_definition = true;
            fsym->type->kr_definition = true;
            fsym->type->nold_style_params = ft->nparams;
            fsym->type->old_style_params = definition->old_style_params;
        }
    }

    /* An earlier PROTOTYPE to check against: the merged symbol type keeps
     * it (the composite of prototype and K&R list is the prototype). */
    if (fsym && fsym->type && fsym->type->kind == TY_FUNC &&
        fsym->type->has_proto)
        proto = fsym->type;

    if (proto && proto->variadic)
        warn_at(s->lang->warnings, WARN_TRADITIONAL, d->span,
                "'%s' defined as variadic function without prototype", d->name);

    if (proto && proto->nparams != ft->nparams) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, d->span,
                  "the definition of '%s' has %u parameters where the "
                  "prototype has %u",
                  d->name, (unsigned)ft->nparams, (unsigned)proto->nparams);
        proto = NULL;
    }

    /* Declaration-list order is semantically observable: `int n; int a[n];`
     * makes n visible while resolving a, even when the identifier list uses
     * another order. Resolve and bind each declarator as it appears. */
    for (ki = 0; ki < d->nkr_decls; ki++) {
        AstNode *kd = d->kr_decls[ki];
        AstNode *one = kd;
        u32 sib = 0;

        while (one) {
            u32 index = kr_param_index(ft, one->name);

            if (index == (u32)-1) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, one->span,
                          "declaration for parameter '%s' but no such "
                          "parameter",
                          one->name);
            } else {
                Type *decl_type = type_from_ast(s, one->type, one->span);
                Type *pt = adjust_param_type(s, decl_type);

                if (definition && definition->kind == TY_FUNC &&
                    index < definition->nold_style_params)
                    definition->old_style_params[index] = pt;
                if (d->param_decl_types && index < d->nparam_syms)
                    d->param_decl_types[index] = decl_type;
                bind_kr_param(s, d, ft, index, pt);
            }
            one = sib < kd->nitems ? kd->items[sib++] : NULL;
        }
    }

    /* Names omitted from the declaration list default to int and enter
     * scope only after that list, matching GCC's dependent-VLA behavior. */
    for (pi = 0; pi < ft->nparams; pi++) {
        const char *pname = ft->params[pi].name;

        if (!pname || !definition || definition->kind != TY_FUNC ||
            definition->old_style_params[pi])
            continue;
        warn_at_ex(s->lang->warnings, WARN_MISSING_PARAMETER_TYPE,
                   ft->params[pi].span, WARN_SUPPRESS_IN_MACRO,
                   "type of '%s' defaults to 'int'", pname);
        definition->old_style_params[pi] = type_basic(TY_INT);
        if (d->param_decl_types && pi < d->nparam_syms)
            d->param_decl_types[pi] = type_basic(TY_INT);
        bind_kr_param(s, d, ft, pi, type_basic(TY_INT));
    }

    for (pi = 0; proto && pi < ft->nparams && pi < proto->nparams; pi++) {
        const char *pname = ft->params[pi].name;
        Type *pt = definition && definition->kind == TY_FUNC
                       ? definition->old_style_params[pi]
                       : type_basic(TY_ERROR);

        if (pname) {
            /* Compatibility is judged on the PROMOTED K&R type. */
            Type *promoted = conv_promote_type(s, pt);
            Type *want = conv_strip_quals(s, proto->params[pi]);

            if (pt->kind == TY_FLOAT)
                promoted = type_basic(TY_DOUBLE);
            if (!type_compatible(conv_strip_quals(s, promoted), want)) {
                const char *declared = type_to_str(s->arena, pt);
                const char *promoted_name = type_to_str(s->arena, promoted);
                const char *prototype_name =
                    type_to_str(s->arena, proto->params[pi]);

                /* GCC keeps its traditional warning for the narrow/float
                 * case where the declaration-list type itself matches the
                 * prototype and only default promotion makes it differ.
                 * Unrelated types are a hard conflicting-definition
                 * constraint: continuing would bind the body parameter to
                 * one type while lowering the composite prototype ABI. */
                if (type_compatible(conv_strip_quals(s, pt), want)) {
                    warn_pedwarn_at(
                        s->lang->warnings, WARN_TRADITIONAL,
                        ft->params[pi].span,
                        "promoted argument '%s' doesn't match prototype "
                        "('%s' promotes to '%s', prototype says '%s')",
                        pname, declared, promoted_name, prototype_name);
                } else {
                    s->nerrors++;
                    diag_emit(s->dc, DIAG_ERROR, ft->params[pi].span,
                              "promoted argument '%s' doesn't match prototype "
                              "('%s' promotes to '%s', prototype says '%s')",
                              pname, declared, promoted_name, prototype_name);
                    if (fsym)
                        diag_emit(s->dc, DIAG_NOTE, fsym->span,
                                  "previous prototype is here");
                }
            }
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
    case AST_STMT_ASM:
        /* File-scope basic asm declares nothing and names nothing: the text
         * is opaque to every pass and reaches the emitter verbatim. The
         * parser already rejected the operand form here. */
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
        s->cur_func = fsym;
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
                d->param_decl_types = arena_alloc(
                    s->arena, ft->nparams * sizeof(Type *), _Alignof(Type *));
                memset(d->param_syms, 0, ft->nparams * sizeof(Symbol *));
                memset(d->param_decl_types, 0, ft->nparams * sizeof(Type *));
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
                    Type *decl_type;
                    Type *pt;

                    if (!ft->params[pi].name)
                        continue;
                    decl_type = type_from_ast(s, ft->params[pi].type,
                                              ft->params[pi].span);
                    rebind_parameter_type(s, decl_type);
                    if (d->param_decl_types && pi < d->nparam_syms)
                        d->param_decl_types[pi] = decl_type;
                    /* The same 6.7.6.3p7/p8 adjustment the prototype path
                     * applies: the BODY sees the pointer, not the array. */
                    pt = adjust_param_type(s, decl_type);
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
        if (fsym && fsym->uses_va_arg_pack)
            validate_va_pack_body_node(s, fsym, d->body);
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
        s->cur_func = NULL;
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

/* --- switch label bookkeeping (6.8.4.2p2-p3, and GNU case ranges) --------
 *
 * ONE interval list answers three questions that are really one question:
 * two plain labels alike, a range overlapping a plain label, and two ranges
 * overlapping are all "do these intervals intersect". A plain `case k` is
 * the interval [k, k], so it needs no separate path -- which is the point,
 * because two paths asking the same question are two paths that drift.
 *
 * Comparison happens in the domain of the PROMOTED CONTROLLING TYPE, per
 * 6.8.4.2p5: the constant is converted to that type first. That is not a
 * detail -- `case -1:` under `switch (unsigned x)` is UINT_MAX, and must
 * collide with `case 0xFFFFFFFF:` and not with anything near zero. */

typedef struct SwitchLabel {
    u64 lo, hi; /* inclusive, already converted; sign-extended if signed */
    Span span;
    struct SwitchLabel *next;
} SwitchLabel;

typedef struct SwitchLabels {
    SwitchLabel *labels;
    u32 bits;         /* width of the promoted controlling type */
    bool is_unsigned; /* its signedness -- the comparison domain */
    bool have_default;
    Span default_span;
    struct SwitchLabels *prev;
} SwitchLabels;

static u64 sw_low_mask(u32 bits)
{
    return bits >= 64 ? UINT64_MAX : ((1ull << bits) - 1);
}

/* 6.8.4.2p5's conversion, as a bit pattern. Signed targets are
 * sign-extended so a plain (i64) comparison is correct for them. */
static u64 sw_convert(const SwitchLabels *sw, u64 v)
{
    u64 m = sw_low_mask(sw->bits);
    u64 x = v & m;

    if (!sw->is_unsigned && sw->bits && sw->bits < 64 &&
        (x & (1ull << (sw->bits - 1))))
        x |= ~m;
    return x;
}

static bool sw_le(const SwitchLabels *sw, u64 a, u64 b)
{
    return sw->is_unsigned ? a <= b : (i64)a <= (i64)b;
}

/* Register one case label -- a plain one arrives as the degenerate range
 * [v, v] -- after checking it against every label already seen. */
static void sema_switch_add_case(Sema *s, AstNode *st, u64 raw_lo, u64 raw_hi)
{
    SwitchLabels *sw = s->switch_labels;
    SwitchLabel *e, *n;
    u64 lo = sw_convert(sw, raw_lo);
    u64 hi = sw_convert(sw, raw_hi);

    /* A reversed range matches nothing. gcc warns and drops it, which is
     * the only coherent reading -- registering it would make a later label
     * inside the reversed span collide with an interval that can never be
     * taken. */
    if (!sw_le(sw, lo, hi)) {
        diag_emit_warn(s->dc, DIAG_WARNING, st->span, WARN_SWITCH_EMPTY_RANGE,
                       "empty range specified");
        return;
    }
    for (e = sw->labels; e; e = e->next) {
        if (!sw_le(sw, e->lo, hi) || !sw_le(sw, lo, e->hi))
            continue; /* disjoint */
        s->nerrors++;
        /* gcc picks the wording from the label BEING ADDED alone, not from
         * the pair -- measured, because the obvious reading is the pair.
         * `case 3 ... 3:` colliding with a wide range gets the SINGLE
         * wording, because after folding it spans one value. */
        if (lo != hi) {
            diag_emit(s->dc, DIAG_ERROR, st->span,
                      "duplicate (or overlapping) case value");
            diag_emit(s->dc, DIAG_NOTE, e->span,
                      "this is the first entry overlapping that value");
        } else {
            diag_emit(s->dc, DIAG_ERROR, st->span, "duplicate case value");
            diag_emit(s->dc, DIAG_NOTE, e->span, "previously used here");
        }
        return; /* one report per label; the value is still not registered */
    }
    n = arena_alloc(s->arena, sizeof(SwitchLabel), _Alignof(SwitchLabel));
    n->lo = lo;
    n->hi = hi;
    n->span = st->span;
    n->next = sw->labels;
    sw->labels = n;
}

/* Statement WALK, not statement sema: we descend only to reach the
 * declarations inside, because block scope and the 6.2.2p4 linkage rule
 * are this sprint's business. Expression typing is Sprint 13 and
 * control-flow sema is Sprint 16. */
void sema_stmt_in_expr(Sema *s, AstNode *st)
{
    sema_stmt(s, st);
}

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
    case AST_STMT_ASM: {
        /* Type the operand expressions; the constraint letters are decoded
         * in lowering, where the target is known. An OUTPUT must be a
         * modifiable lvalue for the same reason the left of `=` must be:
         * the asm assigns to it. */
        u32 k;

        for (k = 0; k < st->asm_nops; k++) {
            AstNode *e = st->asm_ops[k].expr;

            if (!e)
                continue;
            st->asm_ops[k].expr = e = sema_expr(s, e);
            if (k >= st->asm_noutputs || !e->sem_type)
                continue;
            if (!e->is_lvalue) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, e->span,
                          "an asm output operand must be an lvalue");
            } else if (e->sem_type->quals & CGF_QUAL_CONST) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, e->span,
                          "an asm output operand must be modifiable, and "
                          "'%s' is const-qualified",
                          type_to_str(s->arena, e->sem_type));
            }
        }
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
        if (st->lhs) {
            st->lhs = sema_expr(s, st->lhs);
            /* `switch` takes the STRICTER rule: 6.8.4.2p1 requires an INTEGER
             * controlling expression, so a float or a pointer is out even
             * though both are scalars. Nothing enforced it -- a switch on a
             * pointer, a float or a struct reached lowering and died in the
             * IR verifier with "'switch' scrutinizes an integer", reported as
             * "this is a bug in cgfried" against a program that is simply
             * invalid C. Frontend fuzzer, seed 47924. */
            if (st->kind == AST_STMT_SWITCH) {
                if (sema_require_switch_integer(s, st->lhs))
                    /* 6.8.4.2p5: the integer promotions are performed on
                     * the controlling expression.  Leaving a char-typed
                     * switch in IR made jump-table indexing reuse dirty
                     * upper register bits; QBE's address matcher exposed
                     * that as a compiler-built-compiler crash. */
                    st->lhs = conv_promote(s, st->lhs);
            } else
                sema_require_scalar(s, st->lhs);
        }
        if (st->kind == AST_STMT_SWITCH) {
            VmDecl *saved_sw = s->vm_switch_chain;
            SwitchLabels sw;

            memset(&sw, 0, sizeof(sw));
            /* The comparison domain for every label of THIS switch. A
             * nested switch pushes its own, so an inner `case` can never
             * be measured against an outer switch's type. */
            if (st->lhs && st->lhs->sem_type &&
                type_is_integer(st->lhs->sem_type)) {
                Type *pt = conv_promote_type(s, st->lhs->sem_type);

                sw.bits = conv_int_bits(s, pt);
                sw.is_unsigned = !conv_is_signed(s, pt);
            }
            sw.prev = s->switch_labels;
            s->switch_labels = &sw;
            s->vm_switch_chain = s->vm_chain;
            sema_stmt(s, st->body);
            s->switch_labels = sw.prev;
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
        if (st->mid) {
            st->mid = sema_expr(s, st->mid);
            sema_require_scalar(s, st->mid);
        }
        if (st->rhs)
            st->rhs = sema_expr(s, st->rhs);
        sema_mark_discarded_update(st->rhs);
        sema_stmt(s, st->body);
        scope_pop(s);
        return;
    case AST_STMT_CASE:
    case AST_STMT_DEFAULT:
        if (st->kind == AST_STMT_CASE && st->lhs) {
            i64 cv, cv_hi = 0;
            bool ok;

            st->lhs = sema_expr(s, st->lhs);
            /* Case labels are integer constant expressions; Sprint 15
             * gave us the evaluator, so duplicate checking is possible
             * now — and 6.8.4.2p3 makes it a REQUIRED diagnostic. */
            ok = sema_require_ice(s, st->lhs, &cv, "a case label");
            if (st->rhs) {
                st->rhs = sema_expr(s, st->rhs);
                ok &= sema_require_ice(s, st->rhs, &cv_hi,
                                       "the end of a case range");
            }
            if (ok && s->switch_labels)
                sema_switch_add_case(s, st, (u64)cv,
                                     st->rhs ? (u64)cv_hi : (u64)cv);
        } else if (st->kind == AST_STMT_DEFAULT && s->switch_labels) {
            SwitchLabels *sw = s->switch_labels;

            /* 6.8.4.2p2: at most one default. Silently keeping the first
             * is how a typo'd second one disappears. */
            if (sw->have_default) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, st->span,
                          "multiple default labels in one switch");
                diag_emit(s->dc, DIAG_NOTE, sw->default_span,
                          "this is the first default label");
            } else {
                sw->have_default = true;
                sw->default_span = st->span;
            }
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
        else if (sym->gnu.gnu_inline && sym->func_def_inline &&
                 sym->func_def_extern)
            sym->inline_kind = INL_INLINE_DEF; /* GNU extern inline: no body */
        else if (sym->gnu.gnu_inline && sym->func_def_inline)
            sym->inline_kind = INL_EXTERN_INLINE; /* GNU plain inline emits */
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

    /* A FILE-SCOPE VOLATILE is exempt, and only at file scope: gcc warns
     * for an unused `volatile int` local but not for one at file scope,
     * where the object is a hardware register or a location another agent
     * reads and its mere existence is the point. Measured -- musl's
     *
     *     static FILE *volatile dummy = 0;
     *
     * in fflush.c and __stdio_exit.c is exactly this, and it is what the
     * zero-false-positive musl gate caught the moment extended asm made
     * those translation units parse. */
    if (sym->linkage == LINK_INTERNAL && (sym->defined || sym->tentative) &&
        !sym->reads && !(sym->type && (sym->type->quals & CGF_QUAL_VOLATILE)))
        /* A CONST object gets its OWN flag. gcc reports an unused static
         * const under -Wunused-const-variable= and an unused static
         * non-const under -Wunused-variable, both enabled by -Wall in C --
         * measured. Reporting the const one under the wrong flag makes
         * -Wno-unused-const-variable do nothing, which is the whole reason
         * gcc split them: a header full of `static const` tables is a
         * different judgement call from an unused mutable global.
         * musl's fork.c is the file that showed it. */
        warn_at_ex(s->lang->warnings,
                   (sym->type && (sym->type->quals & CGF_QUAL_CONST))
                       ? WARN_UNUSED_CONST_VARIABLE
                       : WARN_UNUSED_VARIABLE,
                   sym->span, WARN_SUPPRESS_IN_MACRO,
                   "'%s' defined but not used", sym->name);

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
            one->may_alias = sym->type->may_alias;
            one->align_override = sym->type->align_override;
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

/* An alias's TARGET must be defined in this translation unit -- gcc's rule,
 * and the one that makes an alias a purely local fact the emitter can settle
 * with a single `.set`. It is an END-OF-TU question for the same reason the
 * inline matrix is: the target may be defined after the alias that names it.
 *
 * A `defined` FUNCTION and a `defined` OBJECT both qualify; another alias does
 * not, because chaining would need a resolution order this deliberately has
 * none of. */
static void check_alias_targets(Sema *s, Symbol *chain)
{
    Symbol *sym;

    for (sym = chain; sym; sym = sym->next) {
        Symbol *t;
        bool ok = false;

        if (!sym->alias_target)
            continue;
        for (t = chain; t; t = t->next) {
            if (t->name != sym->alias_target || t->ns != NS_ORDINARY)
                continue;
            if (t->alias_target)
                break; /* an alias of an alias: refused below by name */
            if (t->defined || t->def_kind != DEF_NONE)
                ok = true;
            break;
        }
        if (!ok) {
            s->nerrors++;
            diag_emit(s->dc, DIAG_ERROR, sym->alias_span,
                      "'%s' is aliased to '%s', which is not defined in this "
                      "translation unit",
                      sym->name, sym->alias_target);
        }
    }
}

/* An ALIAS IS A USE of its target, and it is the only kind of use that
 * appears in no expression: `.set` names the target, so nothing in the tree
 * references it. Without this, musl's
 *
 *     static FILE *volatile dummy = 0;
 *     weak_alias(dummy, __stdout_used);
 *
 * warns "'dummy' defined but not used" where gcc is silent -- and the same
 * blind spot hits -Wunused-function for the far commoner
 * `weak_alias(impl, pub)` shape.
 *
 * This must run BEFORE finish_symbol, which is what emits those warnings.
 * It is the same fact IPO learned separately: an alias target is a root,
 * because a `.set` is not a relocation and no callgraph edge exists. Two
 * passes needed the fact and each discovered it the hard way. */
static void mark_alias_targets_used(Symbol *chain)
{
    Symbol *sym;

    for (sym = chain; sym; sym = sym->next) {
        Symbol *t;

        if (!sym->alias_target)
            continue;
        for (t = chain; t; t = t->next)
            if (t->name == sym->alias_target && t->ns == NS_ORDINARY) {
                t->reads++;
                break;
            }
    }
}

void sema_finish(Sema *s)
{
    if (s->file_scope) {
        mark_alias_targets_used(s->file_scope->ordinary);
        finish_symbol(s, s->file_scope->ordinary);
        check_alias_targets(s, s->file_scope->ordinary);
    }
}

static Symbol *va_pack_direct_callee(AstNode *fn)
{
    for (;;) {
        if (!fn)
            return NULL;
        if (fn->kind == AST_EXPR_PAREN ||
            (fn->kind == AST_EXPR_CAST && fn->implicit)) {
            fn = fn->lhs;
            continue;
        }
        if (fn->kind == AST_EXPR_UNARY && fn->op == PUNCT_STAR) {
            fn = fn->lhs;
            continue;
        }
        if (fn->kind == AST_EXPR_IDENT && fn->sym && fn->sym->kind == SYM_FUNC)
            return fn->sym;
        return NULL;
    }
}

/* Whole-TU check: whether a function needs mandatory pack specialization is
 * only known after its definition has been typed. Diagnose every use that
 * would require a standalone address/body now, including an initializer that
 * appeared before the definition. */
static void check_va_pack_uses(Sema *s, AstNode *n, Symbol *current)
{
    u32 i;

    if (!n)
        return;
    if (n->kind == AST_FUNC_DEF) {
        check_va_pack_uses(s, n->body, n->sym);
        return;
    }
    if (n->kind == AST_EXPR_CALL) {
        Symbol *callee = va_pack_direct_callee(n->lhs);

        if (callee && callee->uses_va_arg_pack) {
            if (current && current->uses_va_arg_pack) {
                s->nerrors++;
                diag_emit(s->dc, DIAG_ERROR, n->span,
                          "nested calls to variadic argument-pack wrappers "
                          "are not supported");
            }
        } else {
            check_va_pack_uses(s, n->lhs, current);
        }
        for (i = 0; i < n->nargs; i++)
            check_va_pack_uses(s, n->args[i], current);
        return;
    }
    if (n->kind == AST_EXPR_IDENT && n->sym && n->sym->kind == SYM_FUNC &&
        n->sym->uses_va_arg_pack) {
        s->nerrors++;
        diag_emit(s->dc, DIAG_ERROR, n->span,
                  "cannot take the address of variadic argument-pack "
                  "wrapper '%s'; it must be called directly",
                  n->sym->name);
        return;
    }

    check_va_pack_uses(s, n->lhs, current);
    check_va_pack_uses(s, n->mid, current);
    check_va_pack_uses(s, n->rhs, current);
    check_va_pack_uses(s, n->init, current);
    check_va_pack_uses(s, n->body, current);
    check_va_pack_uses(s, n->desig_index, current);
    check_va_pack_uses(s, n->desig_range_end, current);
    for (i = 0; i < n->nargs; i++)
        check_va_pack_uses(s, n->args[i], current);
    for (i = 0; i < n->nitems; i++)
        check_va_pack_uses(s, n->items[i], current);
    for (i = 0; i < n->ndesignators; i++)
        check_va_pack_uses(s, n->designators[i], current);
}

void sema_run(Sema *s, AstNode *tu)
{
    u32 i;

    if (!tu)
        return;
    for (i = 0; i < tu->ndecls; i++)
        sema_decl(s, tu->decls[i]);
    for (i = 0; i < tu->ndecls; i++)
        check_va_pack_uses(s, tu->decls[i], NULL);
    sema_finish(s);
}
