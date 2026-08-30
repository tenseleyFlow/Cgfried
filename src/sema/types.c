#include <string.h>

#include "sema/sema.h"
#include "util/buf.h"

/* The Type graph. Basics are interned so they compare by pointer; derived
 * types are per-use nodes compared structurally.
 *
 * Why not unique everything? Two reasons, both fatal to the idea. The same
 * struct tag completes DIFFERENTLY in different translation units, so a
 * global table would need per-TU keys anyway; and qualifier combinations
 * multiply nodes without bound. chibicc and cproc both settle here, and
 * structural comparison on demand is cheap because the graphs are tiny. */

static Type basics[TY_ERROR + 1];
static bool basics_ready;

static void init_basics(void)
{
    int k;

    if (basics_ready)
        return;
    for (k = 0; k <= (int)TY_ERROR; k++) {
        memset(&basics[k], 0, sizeof(basics[k]));
        basics[k].kind = (TypeKind)k;
    }
    basics_ready = true;
}

Type *type_basic(TypeKind k)
{
    init_basics();
    if (k > TY_FLOAT64X && k != TY_VOID && k != TY_BOOL && k != TY_ERROR)
        CGF_ICE("type_basic: %d is not a basic type", (int)k);
    return &basics[k];
}

static Type *type_new(Arena *ar, TypeKind k)
{
    Type *t = arena_alloc(ar, sizeof(Type), _Alignof(Type));

    memset(t, 0, sizeof(*t));
    t->kind = k;
    return t;
}

Type *type_qualify(Arena *ar, const Type *t, unsigned quals)
{
    Type *q;

    if (!t)
        return NULL;
    if (quals == 0)
        return (Type *)t;
    /* A qualified type is a DISTINCT type from its unqualified version
     * (6.2.5p26), so it needs its own node even for a basic kind — which
     * is exactly why basics are interned but qualified basics are not. */
    q = type_new(ar, t->kind);
    *q = *t;
    q->quals = t->quals | quals;
    return q;
}

Type *type_may_alias(Arena *ar, const Type *t)
{
    Type *a;

    if (!t || t->may_alias)
        return (Type *)t;
    /* Basics are process-global interned nodes, so the property must live on
     * an arena-owned copy rather than poisoning every `int` in every TU. */
    a = type_new(ar, t->kind);
    *a = *t;
    a->may_alias = true;
    return a;
}

Type *type_with_alignment(Arena *ar, const Type *t, u64 align)
{
    Type *a;

    if (!t || align <= t->align_override)
        return (Type *)t;
    /* Basic types are process-global interned nodes, and derived types may be
     * shared by typedef lookup. Never mutate either in place: the alignment
     * belongs only to the attributed spelling that requested it. */
    a = type_new(ar, t->kind);
    *a = *t;
    a->align_override = align;
    return a;
}

Type *type_ptr(Arena *ar, Type *pointee)
{
    Type *t = type_new(ar, TY_PTR);

    t->base = pointee;
    return t;
}

Type *type_array(Arena *ar, Type *elem)
{
    Type *t = type_new(ar, TY_ARRAY);

    t->base = elem;
    return t;
}

Type *type_func(Arena *ar, Type *ret)
{
    Type *t = type_new(ar, TY_FUNC);

    t->base = ret;
    return t;
}

Type *type_tag(Arena *ar, TagDecl *tag)
{
    Type *t = type_new(ar, tag->kind);

    t->tag = tag;
    return t;
}

Type *type_enum_with_repr(Arena *ar, const Type *t, Type *repr)
{
    Type *mode;

    if (!t || t->kind != TY_ENUM || !repr || !type_is_integer(repr) ||
        repr->kind == TY_ENUM)
        CGF_ICE("type_enum_with_repr requires an enum and a basic integer");
    mode = type_new(ar, TY_ENUM);
    *mode = *t;
    mode->enum_repr = repr;
    return mode;
}

Type *type_enum_underlying(const Type *t)
{
    if (!t || t->kind != TY_ENUM)
        return NULL;
    if (t->enum_repr)
        return t->enum_repr;
    return t->tag && t->tag->enum_underlying ? t->tag->enum_underlying
                                             : type_basic(TY_INT);
}

bool type_is_basic(const Type *t)
{
    return t && t->kind <= TY_FLOAT64X;
}

bool type_is_integer(const Type *t)
{
    if (!t)
        return false;
    /* An enum is an integer type; its underlying type is chosen in decl.c. */
    return (t->kind >= TY_BOOL && t->kind <= TY_ULLONG) || t->kind == TY_ENUM;
}

bool type_is_floating(const Type *t)
{
    return t && t->kind >= TY_FLOAT && t->kind <= TY_FLOAT64X;
}

bool type_is_arithmetic(const Type *t)
{
    return type_is_integer(t) || type_is_floating(t);
}

bool type_is_complete(const Type *t)
{
    if (!t)
        return false;
    switch (t->kind) {
    case TY_VOID:
        return false; /* never completable */
    case TY_ARRAY:
        return t->has_size || t->is_vla;
    case TY_STRUCT:
    case TY_UNION:
    case TY_ENUM:
        return t->tag && t->tag->complete;
    default:
        return true;
    }
}

bool type_is_runtime_sized_array(const Type *t)
{
    for (; t && t->kind == TY_ARRAY; t = t->base)
        if (t->is_vla)
            return true;
    return false;
}

/* --- compatibility (6.2.7, 6.7.6) ---------------------------------------- */

static bool survives_default_arg_promotions(const Type *t)
{
    if (!t || t->kind == TY_ERROR)
        return true; /* suppress cascades from an already-poisoned parameter */
    switch (t->kind) {
    case TY_BOOL:
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
    case TY_SHORT:
    case TY_USHORT:
    case TY_FLOAT:
        return false;
    default:
        return true;
    }
}

static const Type *default_promoted_param(const Type *t)
{
    if (!t)
        return t;
    switch (t->kind) {
    case TY_BOOL:
    case TY_CHAR:
    case TY_SCHAR:
    case TY_UCHAR:
    case TY_SHORT:
    case TY_USHORT:
        return type_basic(TY_INT);
    case TY_FLOAT:
        return type_basic(TY_DOUBLE);
    case TY_ENUM:
        t = type_enum_underlying(t);
        return survives_default_arg_promotions(t) ? t : type_basic(TY_INT);
    default:
        return t;
    }
}

static bool old_style_definition_matches(const Type *old_style,
                                         const Type *proto)
{
    u32 i;

    if (proto->variadic)
        return false;
    if (!old_style->kr_definition)
        return proto->nparams == 0;
    if (old_style->nold_style_params != proto->nparams)
        return false;
    for (i = 0; i < proto->nparams; i++) {
        Type old_unqual, proto_unqual;
        const Type *old_param =
            default_promoted_param(old_style->old_style_params[i]);
        const Type *proto_param = proto->params[i];

        if (old_param && old_param->quals) {
            old_unqual = *old_param;
            old_unqual.quals = 0;
            old_param = &old_unqual;
        }
        if (proto_param && proto_param->quals) {
            proto_unqual = *proto_param;
            proto_unqual.quals = 0;
            proto_param = &proto_unqual;
        }
        if (!type_compatible(old_param, proto_param))
            return false;
    }
    return true;
}

static bool params_compatible(const Type *a, const Type *b)
{
    u32 i;

    if (!a->has_proto || !b->has_proto) {
        const Type *old_style, *proto;

        if (!a->has_proto && !b->has_proto)
            return true;
        proto = a->has_proto ? a : b;
        old_style = a->has_proto ? b : a;
        /* SEMA-H-03: a definition is not an unspecified declaration. Its
         * resolved identifier-list signature (or its known zero parameters
         * for `f(){}`) must participate whichever operand comes first. */
        if (old_style->old_style_definition)
            return old_style_definition_matches(old_style, proto);
        /* SEMA-H-03, C11 6.7.6.3p15: an empty identifier-list declaration
         * matches a prototype only when there is no ellipsis and every
         * fixed parameter already has its default-promoted type. Otherwise
         * calls compiled through the old declaration use a different ABI. */
        if (proto->variadic)
            return false;
        for (i = 0; i < proto->nparams; i++)
            if (!survives_default_arg_promotions(proto->params[i]))
                return false;
        return true;
    }
    if (a->nparams != b->nparams || a->variadic != b->variadic)
        return false;
    for (i = 0; i < a->nparams; i++) {
        const Type *pa = a->params[i];
        const Type *pb = b->params[i];
        Type ua, ub;

        /* 6.7.6.3p15: for compatibility, a parameter declared with a
         * QUALIFIED type is taken as having the unqualified version. So
         * `void f(int *const);` and `void f(int *p){}` are the same
         * function — the const is a promise to the body, not part of the
         * interface. Only the TOP level is stripped: `const int *` and
         * `int *` stay incompatible. */
        if (pa && pa->quals) {
            ua = *pa;
            ua.quals = 0;
            pa = &ua;
        }
        if (pb && pb->quals) {
            ub = *pb;
            ub.quals = 0;
            pb = &ub;
        }
        if (!type_compatible(pa, pb))
            return false;
    }
    return true;
}

bool type_compatible(const Type *a, const Type *b)
{
    Type unqual;

    if (a == b)
        return true;
    if (!a || !b)
        return false;
    /* A poisoned type is compatible with everything: Sprint 11's contract
     * is that nothing is diagnosed about an already-diagnosed construct. */
    if (a->kind == TY_ERROR || b->kind == TY_ERROR)
        return true;
    /* Qualifiers are part of type identity everywhere below the top level.
     * Callers that want lvalue conversion strip them first (Sprint 13). */
    if (a->quals != b->quals)
        return false;
    /* 6.7.2.2p4: every enum is compatible with its implementation-chosen
     * integer representation.  Keep distinct enum tags distinct below, but
     * bridge an enum/basic-integer pair here.  GCC exposes this through both
     * __builtin_types_compatible_p and conditional pointer composition; the
     * latter is what torture enum-3.c relies on. */
    if (a->kind == TY_ENUM && b->kind != TY_ENUM && type_is_integer(b)) {
        unqual = *b;
        unqual.quals = 0;
        return type_compatible(type_enum_underlying(a), &unqual);
    }
    if (b->kind == TY_ENUM && a->kind != TY_ENUM && type_is_integer(a)) {
        unqual = *a;
        unqual.quals = 0;
        return type_compatible(&unqual, type_enum_underlying(b));
    }
    if (a->kind != b->kind)
        return false;

    switch (a->kind) {
    case TY_PTR:
        return type_compatible(a->base, b->base);
    case TY_ARRAY:
        if (!type_compatible(a->base, b->base))
            return false;
        /* Only when BOTH sizes are known do they have to agree; an
         * incomplete array is compatible with a sized one, which is what
         * makes `int a[]; int a[10];` legal. */
        if (a->has_size && b->has_size)
            return a->size == b->size;
        return true;
    case TY_FUNC:
        return type_compatible(a->base, b->base) && params_compatible(a, b);
    case TY_STRUCT:
    case TY_UNION:
        /* Within one translation unit, tag identity IS compatibility.
         * Member-wise comparison only matters across TUs (6.2.7p1). */
        return a->tag == b->tag;
    case TY_ENUM:
        /* A mode on an existing tag is a distinct enum view. Two such views
         * are compatible exactly when their tag and explicit mode match;
         * even mode(SI) is distinct from the un-attributed enum, matching
         * GCC's types_compatible_p result. */
        if (a->tag != b->tag || (!!a->enum_repr != !!b->enum_repr))
            return false;
        return !a->enum_repr || type_compatible(a->enum_repr, b->enum_repr);
    default:
        return true; /* same basic kind, same quals */
    }
}

Member *type_union_cast_member(const Type *union_type, const Type *operand_type)
{
    Member *m;

    if (!union_type || union_type->kind != TY_UNION || !union_type->tag ||
        !operand_type)
        return NULL;
    for (m = union_type->tag->members; m; m = m->next) {
        Type member_unqual;
        Type operand_unqual;

        /* GCC does not treat a bit-field's declared base type as a member
         * type available to this extension. Unnamed fields are therefore
         * excluded with the named ones. */
        if (!m->type || m->is_bitfield || m->type->kind != operand_type->kind)
            continue;
        member_unqual = *m->type;
        operand_unqual = *operand_type;
        member_unqual.quals = 0;
        operand_unqual.quals = 0;
        if (type_compatible(&member_unqual, &operand_unqual))
            return m;
    }
    return NULL;
}

bool type_array_initializer_compatible(const Type *target, const Type *source)
{
    Type target_unqual;
    Type source_unqual;

    if (!target || !source || target->kind != TY_ARRAY ||
        source->kind != TY_ARRAY)
        return false;
    if (target->has_size && source->has_size && target->size != source->size)
        return false;
    if (target->base->kind == TY_ARRAY || source->base->kind == TY_ARRAY)
        return type_array_initializer_compatible(target->base, source->base);
    target_unqual = *target->base;
    source_unqual = *source->base;
    target_unqual.quals = 0;
    source_unqual.quals = 0;
    return type_compatible(&target_unqual, &source_unqual);
}

Type *type_composite(Arena *ar, Type *a, Type *b)
{
    Type *c;

    if (!a)
        return b;
    if (!b)
        return a;
    if (a->kind == TY_ERROR || b->kind == TY_ERROR)
        return type_basic(TY_ERROR);
    if (a->kind != b->kind)
        return a;

    switch (a->kind) {
    case TY_PTR:
        c = type_ptr(ar, type_composite(ar, a->base, b->base));
        c->quals = a->quals;
        c->may_alias = a->may_alias || b->may_alias;
        c->align_override = a->align_override > b->align_override
                                ? a->align_override
                                : b->align_override;
        return c;
    case TY_ARRAY:
        /* The size comes from whichever declaration HAS one: this is what
         * turns `int a[]; int a[10];` into `int[10]`. */
        c = type_array(ar, type_composite(ar, a->base, b->base));
        c->quals = a->quals;
        c->may_alias = a->may_alias || b->may_alias;
        c->align_override = a->align_override > b->align_override
                                ? a->align_override
                                : b->align_override;
        if (a->has_size) {
            c->has_size = true;
            c->size = a->size;
            c->size_expr = a->size_expr;
        } else if (b->has_size) {
            c->has_size = true;
            c->size = b->size;
            c->size_expr = b->size_expr;
        }
        c->is_vla = a->is_vla || b->is_vla;
        return c;
    case TY_FUNC: {
        const Type *proto = a->has_proto ? a : (b->has_proto ? b : NULL);

        c = type_func(ar, type_composite(ar, a->base, b->base));
        c->quals = a->quals;
        c->may_alias = a->may_alias || b->may_alias;
        c->align_override = a->align_override > b->align_override
                                ? a->align_override
                                : b->align_override;
        if (!proto) {
            const Type *definition = a->old_style_definition
                                         ? a
                                         : (b->old_style_definition ? b : NULL);

            if (definition) {
                c->old_style_definition = true;
                c->kr_definition = definition->kr_definition;
                c->nold_style_params = definition->nold_style_params;
                c->old_style_params = definition->old_style_params;
            }
            return c; /* neither had one: still unprototyped */
        }
        c->has_proto = true;
        c->variadic = proto->variadic;
        c->nparams = proto->nparams;
        if (proto->nparams) {
            u32 i;

            c->params = arena_alloc(ar, proto->nparams * sizeof(Type *),
                                    _Alignof(Type *));
            for (i = 0; i < proto->nparams; i++) {
                /* When BOTH are prototypes, composite each parameter;
                 * otherwise take the prototype's. */
                if (a->has_proto && b->has_proto)
                    c->params[i] =
                        type_composite(ar, a->params[i], b->params[i]);
                else
                    c->params[i] = proto->params[i];
            }
        }
        return c;
    }
    default: {
        Type *out = a->may_alias || b->may_alias ? type_may_alias(ar, a) : a;
        u64 align = a->align_override > b->align_override ? a->align_override
                                                          : b->align_override;

        return type_with_alignment(ar, out, align);
    }
    }
}

bool type_compatible_cross_tu(Sema *s, const Type *a, const Type *b)
{
    /* 6.2.7p1's member-wise rule is only observable when two translation
     * units meet, so it belongs with the multi-TU campaign. */
    sema_unimplemented(s, (Span){0},
                       "cross-translation-unit type "
                       "compatibility",
                       57);
    (void)a;
    (void)b;
    return false;
}

/* --- rendering ----------------------------------------------------------- */

static const char *basic_name(TypeKind k)
{
    switch (k) {
    case TY_VOID:
        return "void";
    case TY_BOOL:
        return "_Bool";
    case TY_CHAR:
        return "char";
    case TY_SCHAR:
        return "signed char";
    case TY_UCHAR:
        return "unsigned char";
    case TY_SHORT:
        return "short";
    case TY_USHORT:
        return "unsigned short";
    case TY_INT:
        return "int";
    case TY_UINT:
        return "unsigned int";
    case TY_LONG:
        return "long";
    case TY_ULONG:
        return "unsigned long";
    case TY_LLONG:
        return "long long";
    case TY_ULLONG:
        return "unsigned long long";
    case TY_FLOAT:
        return "float";
    case TY_DOUBLE:
        return "double";
    case TY_LDOUBLE:
        return "long double";
    case TY_FLOAT128:
        return "_Float128";
    case TY_FLOAT32:
        return "_Float32";
    case TY_FLOAT64:
        return "_Float64";
    case TY_FLOAT32X:
        return "_Float32x";
    case TY_FLOAT64X:
        return "_Float64x";
    case TY_ERROR:
        return "<error-type>";
    default:
        return "?";
    }
}

static void render_quals_prefix(unsigned q, Buf *b)
{
    if (q & CGF_QUAL_CONST)
        buf_printf(b, "const ");
    if (q & CGF_QUAL_VOLATILE)
        buf_printf(b, "volatile ");
    if (q & CGF_QUAL_RESTRICT)
        buf_printf(b, "restrict ");
    if (q & CGF_QUAL_ATOMIC)
        buf_printf(b, "_Atomic ");
}

/* Qualifiers on a DERIVED type belong after the derivation, not before
 * it: `const char *const` is a const pointer to const char, and printing
 * both quals up front would render it as "const const char *" — which is
 * exactly the bug the first dump showed. */
static void render_quals_suffix(unsigned q, Buf *b)
{
    if (q & CGF_QUAL_CONST)
        buf_printf(b, " const");
    if (q & CGF_QUAL_VOLATILE)
        buf_printf(b, " volatile");
    if (q & CGF_QUAL_RESTRICT)
        buf_printf(b, " restrict");
    if (q & CGF_QUAL_ATOMIC)
        buf_printf(b, " _Atomic");
}

static void render(const Type *t, Buf *b)
{
    if (!t) {
        buf_printf(b, "<null>");
        return;
    }

    switch (t->kind) {
    case TY_PTR:
        render(t->base, b);
        buf_printf(b, " *");
        render_quals_suffix(t->quals, b);
        return;
    case TY_ARRAY: {
        /* C writes the OUTERMOST bound first: `int a[2][3]` is 2 arrays of
         * 3 ints. Since the chain runs outer-to-inner, render the ultimate
         * element type once and then walk the chain forwards emitting
         * bounds — rendering each level recursively would print them
         * inside-out. */
        const Type *e = t;

        while (e->kind == TY_ARRAY)
            e = e->base;
        render(e, b);
        for (e = t; e->kind == TY_ARRAY; e = e->base) {
            render_quals_suffix(e->quals, b);
            if (e->is_vla)
                buf_printf(b, " [vla]");
            else if (e->has_size)
                buf_printf(b, " [%llu]", (unsigned long long)e->size);
            else
                buf_printf(b, " []");
        }
        return;
    }
    case TY_FUNC: {
        u32 i;

        render(t->base, b);
        render_quals_suffix(t->quals, b);
        buf_printf(b, " (");
        if (!t->has_proto) {
            buf_printf(b, "unspecified");
        } else if (t->nparams == 0) {
            buf_printf(b, "void");
        } else {
            for (i = 0; i < t->nparams; i++) {
                if (i)
                    buf_printf(b, ", ");
                render(t->params[i], b);
            }
            if (t->variadic)
                buf_printf(b, ", ...");
        }
        buf_printf(b, ")");
        return;
    }
    case TY_STRUCT:
    case TY_UNION:
    case TY_ENUM:
        render_quals_prefix(t->quals, b);
        buf_printf(b, "%s %s",
                   t->kind == TY_STRUCT  ? "struct"
                   : t->kind == TY_UNION ? "union"
                                         : "enum",
                   t->tag && t->tag->name ? t->tag->name : "<anonymous>");
        if (t->tag && !t->tag->complete)
            buf_printf(b, " [incomplete]");
        return;
    default:
        render_quals_prefix(t->quals, b);
        buf_printf(b, "%s", basic_name(t->kind));
        return;
    }
}

char *type_to_str(Arena *ar, const Type *t)
{
    Buf b;
    char *out;

    buf_init(&b);
    render(t, &b);
    out = arena_alloc(ar, b.len + 1, 1);
    if (b.len)
        memcpy(out, b.data, b.len);
    out[b.len] = '\0';
    buf_free(&b);
    return out;
}

/* The callback ast.c uses to print a node's resolved type in --dump-ast
 * and -fdump-sema goldens. */
static void sem_type_render_cb(const AstNode *n, Buf *out)
{
    if (n && n->sem_type)
        render(n->sem_type, out);
    else
        buf_printf(out, "<untyped>");
}

void sema_install_renderer(void)
{
    ast_set_sem_type_renderer(sem_type_render_cb);
}

/* --- the builtin table (Sprint 28) --------------------------------------- */

/* One row per compiler-owned __builtin_*, generated from builtins.def so
 * the marker enum, the arity check and the result rule can never drift
 * apart. Lookup is by the spelling AFTER the "__builtin_" prefix. */
u16 sema_builtin_lookup(const char *suffix, int *nargs, int *kind)
{
    static const struct {
        const char *name;
        u16 marker;
        int nargs;
        int kind;
    } table[] = {
#define B(sfx, NAME, n, k) {#sfx, (u16)SEMA_BUILTIN_##NAME, (n), (k)},
#include "builtins.def"
#undef B
    };
    size_t i;

    for (i = 0; i < CGF_ARRAY_LEN(table); i++) {
        if (strcmp(table[i].name, suffix) == 0) {
            if (nargs)
                *nargs = table[i].nargs;
            if (kind)
                *kind = table[i].kind;
            return table[i].marker;
        }
    }
    return 0;
}

/* The exact-width unsigned type a BK_U* row names. 8/16/32 are the same
 * spelling on every target we have; 64 is `unsigned long long` on Darwin
 * and `unsigned long` everywhere else, and that fact lives in target.c so
 * the predefined __UINT64_TYPE__ and this cannot disagree. */
Type *sema_builtin_uint_type(Sema *s, int kind)
{
    switch (kind) {
    case BK_U16:
        return type_basic(TY_USHORT);
    case BK_U32:
        return type_basic(TY_UINT);
    case BK_U64:
        return type_basic(cgf_target_int64_is_longlong(s->target) ? TY_ULLONG
                                                                  : TY_ULONG);
    default:
        return NULL;
    }
}

unsigned sema_builtin_bswap_bytes(u16 marker)
{
    switch (marker) {
    case SEMA_BUILTIN_BSWAP16:
        return 2;
    case SEMA_BUILTIN_BSWAP32:
        return 4;
    case SEMA_BUILTIN_BSWAP64:
        return 8;
    default:
        return 0;
    }
}

u64 cgf_bswap(u64 v, unsigned bytes)
{
    u64 r = 0;
    unsigned i;

    for (i = 0; i < bytes; i++)
        r |= ((v >> (8u * i)) & 0xffu) << (8u * (bytes - 1u - i));
    return r;
}

/* Anonymous-member-transparent lookup used by the offsetof folder (the
 * one in sema/expr.c is file-static and returns the innermost Member;
 * this only answers "is the name reachable from here"). */
bool find_member_named(const Type *t, const char *name)
{
    Member *m;

    if (!t || !t->tag)
        return false;
    for (m = t->tag->members; m; m = m->next) {
        if (m->name == name)
            return true;
        if (!m->name && m->type &&
            (m->type->kind == TY_STRUCT || m->type->kind == TY_UNION) &&
            find_member_named(m->type, name))
            return true;
    }
    return false;
}
