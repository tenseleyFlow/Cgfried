#include <string.h>

#include "sema/sema.h"
#include "warn/warn.h"

/* Scopes and the four namespaces (6.2.3).
 *
 * ORDINARY holds objects, functions, typedefs and enum CONSTANTS in one
 * shared space — which is why `enum { x }; int x;` in one scope collides.
 * TAGS holds struct, union and enum together, so `struct T` and `enum T`
 * cannot coexist in one scope. MEMBERS get one namespace per struct and
 * live on the TagDecl. LABELS are function-scoped and were resolved by the
 * parser in Sprint 10, which is the right place: a goto may precede its
 * label, and only the parser sees the whole function body at once.
 *
 * Storage is arena-only. The lists are intrusive and newest-first, so a
 * shadowing declaration is simply found first and nothing has to be
 * removed on scope exit. That also means src/sema/ makes no heap
 * allocation at all — leak-freedom by construction rather than by
 * discipline. Sprint 52 adds arena-backed pointer indexes beside the chains:
 * lookup is expected O(1), while declaration-order output still walks only
 * the deterministic chains. */

void sema_init(Sema *s, Arena *ar, DiagCtx *dc, Interner *in,
               const LangOpts *lang, TargetSpec target)
{
    memset(s, 0, sizeof(*s));
    s->arena = ar;
    s->dc = dc;
    s->interner = in;
    s->lang = lang;
    s->target = target;
    s->file_scope = scope_push(s, SCOPE_FILE);
}

Scope *scope_push(Sema *s, ScopeKind k)
{
    Scope *sc = arena_alloc(s->arena, sizeof(Scope), _Alignof(Scope));

    memset(sc, 0, sizeof(*sc));
    ptrmap_init(&sc->ordinary_index, s->arena);
    ptrmap_init(&sc->tag_index, s->arena);
    sc->kind = k;
    sc->parent = s->scope;
    s->scope = sc;
    return sc;
}

void scope_pop(Sema *s)
{
    Scope *sc;

    if (!s->scope)
        CGF_ICE("scope_pop: scope stack underflow");
    sc = s->scope;
    if (sc->kind != SCOPE_FILE && sc->kind != SCOPE_PROTO) {
        Symbol *sym;
        u32 n = 0, i;

        for (sym = sc->ordinary; sym; sym = sym->next)
            n++;
        if (n) {
            Symbol **ordered =
                arena_alloc(s->arena, n * sizeof(*ordered), _Alignof(Symbol *));

            for (sym = sc->ordinary, i = n; sym; sym = sym->next)
                ordered[--i] = sym;
            for (i = 0; i < n; i++) {
                WarnId id;
                const char *what;

                sym = ordered[i];
                if (sym->kind != SYM_VAR || sym->linkage != LINK_NONE ||
                    sym->reads)
                    continue;
                if (sym->is_param) {
                    id = sym->writes ? WARN_UNUSED_BUT_SET_PARAMETER
                                     : WARN_UNUSED_PARAMETER;
                    what = sym->writes ? "parameter '%s' set but not used"
                                       : "unused parameter '%s'";
                } else {
                    id = sym->writes ? WARN_UNUSED_BUT_SET_VARIABLE
                                     : WARN_UNUSED_VARIABLE;
                    what = sym->writes ? "variable '%s' set but not used"
                                       : "unused variable '%s'";
                }
                warn_at_ex(s->lang->warnings, id, sym->span,
                           WARN_SUPPRESS_IN_MACRO, what, sym->name);
            }
        }
    }
    s->scope = s->scope->parent;
}

Symbol *scope_lookup_local(Scope *sc, const char *name, Namespace ns)
{
    if (!sc || !name)
        return NULL;
    return ptrmap_get(ns == NS_TAG ? &sc->tag_index : &sc->ordinary_index,
                      name);
}

Symbol *scope_lookup(Scope *sc, const char *name, Namespace ns)
{
    for (; sc; sc = sc->parent) {
        Symbol *sym = scope_lookup_local(sc, name, ns);

        if (sym)
            return sym;
    }
    return NULL;
}

Symbol *sym_new(Sema *s, const char *name, SymKind kind, Namespace ns,
                Type *type, Span span)
{
    Symbol *sym = arena_alloc(s->arena, sizeof(Symbol), _Alignof(Symbol));

    memset(sym, 0, sizeof(*sym));
    sym->name = name;
    sym->kind = kind;
    sym->ns = ns;
    sym->type = type;
    sym->span = span;
    sym->linkage = LINK_NONE;
    return sym;
}

Symbol *scope_declare(Sema *s, Symbol *sym)
{
    Scope *sc = s->scope;
    Symbol *outer = NULL;

    if (!sc)
        CGF_ICE("scope_declare: no scope");
    if (sc->kind != SCOPE_FILE && sc->kind != SCOPE_PROTO &&
        (sym->kind == SYM_VAR || sym->kind == SYM_FUNC))
        outer = scope_lookup(sc->parent, sym->name, sym->ns);
    if (outer && (outer->kind == SYM_VAR || outer->kind == SYM_FUNC) &&
        !(sym->span.origin & SPAN_ORIGIN_ANY_MACRO) &&
        warn_enabled(s->lang->warnings, WARN_SHADOW, sym->span)) {
        warn_at_ex(
            s->lang->warnings, WARN_SHADOW, sym->span, WARN_SUPPRESS_IN_MACRO,
            "declaration of '%s' shadows a previous declaration", sym->name);
        diag_emit(s->dc, DIAG_NOTE, outer->span,
                  "shadowed declaration is here");
    }
    if (sym->ns == NS_TAG) {
        sym->next = sc->tags;
        sc->tags = sym;
        if (sym->name)
            ptrmap_put(&sc->tag_index, sym->name, sym);
    } else {
        sym->next = sc->ordinary;
        sc->ordinary = sym;
        if (sym->name)
            ptrmap_put(&sc->ordinary_index, sym->name, sym);
    }
    return sym;
}

void sema_unimplemented(Sema *s, Span span, const char *what, int sprint)
{
    /* One choke point so the DoD's grep finds every deferral, and so no
     * message can exist that fails to name the sprint that lands it. */
    s->nerrors++;
    diag_emit(s->dc, DIAG_ERROR, span,
              "sema: unimplemented: %s (lands in Sprint %d)", what, sprint);
}

/* Debug dump for golden fixtures. Symbols are printed in DECLARATION
 * order: the chain is newest-first, so this recurses to the tail and
 * prints on the way back out. Insertion order (never hash order) reaching
 * output is the repo-wide determinism law — the byte-identical bootstrap
 * in Sprint 58 depends on it. */
static void dump_chain(Sema *s, const Symbol *sym, FILE *f)
{
    static const char *const link_name[] = {"none", "internal", "external"};
    static const char *const kind_name[] = {"var", "func", "typedef",
                                            "enum-const", "tag"};

    if (!sym)
        return;
    dump_chain(s, sym->next, f);

    if (sym->kind == SYM_TAG) {
        fprintf(f, "TAG %s: %s", sym->name ? sym->name : "<anonymous>",
                type_to_str(s->arena, sym->type));
        if (sym->tag && sym->tag->kind == TY_ENUM && sym->tag->enum_underlying)
            fprintf(f, " [underlying %s]",
                    type_to_str(s->arena, sym->tag->enum_underlying));
        fprintf(f, "\n");
        return;
    }
    if (sym->kind == SYM_ENUM_CONST) {
        fprintf(f, "ENUMCONST %s = %lld: %s\n", sym->name,
                (long long)sym->enum_value, type_to_str(s->arena, sym->type));
        return;
    }
    fprintf(f, "%s %s: %s [%s]", kind_name[sym->kind], sym->name,
            type_to_str(s->arena, sym->type), link_name[sym->linkage]);
    if (sym->tentative)
        fprintf(f, " [tentative]");
    if (sym->defined)
        fprintf(f, " [defined]");
    if (sym->tls)
        fprintf(f, " [tls]");
    /* The Sprint 16 decisions, in greppable form. The inline line answers
     * exactly the question Sprint 19 will ask: does THIS TU emit the
     * external definition? */
    switch ((InlineKind)sym->inline_kind) {
    case INL_NONE:
        break;
    case INL_STATIC:
        fprintf(f, " [static-inline]");
        break;
    case INL_INLINE_DEF:
        fprintf(f, " [inline-def emit-external=no]");
        break;
    case INL_EXTERN_INLINE:
        fprintf(f, " [extern-inline emit-external=yes]");
        break;
    }
    switch ((DefKind)sym->def_kind) {
    case DEF_NONE:
        break;
    case DEF_COMMON:
        fprintf(f, " [common]");
        break;
    case DEF_ZERO_INIT:
        fprintf(f, " [zero-init]");
        break;
    case DEF_INIT:
        break; /* [defined] already says it */
    }
    fprintf(f, "\n");
}

void sema_dump(Sema *s, FILE *f)
{
    Scope *fs = s->file_scope;

    if (!fs)
        return;
    dump_chain(s, fs->tags, f);
    dump_chain(s, fs->ordinary, f);
}
