#include <string.h>

#include "sema/sema.h"

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
 * discipline. Lookup is linear within a scope; the graphs are small and
 * Sprint 57's musl campaign is where a hash index would earn its keep. */

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
    sc->kind = k;
    sc->parent = s->scope;
    s->scope = sc;
    return sc;
}

void scope_pop(Sema *s)
{
    if (!s->scope)
        CGF_ICE("scope_pop: scope stack underflow");
    s->scope = s->scope->parent;
}

static Symbol *chain_for(Scope *sc, Namespace ns)
{
    return ns == NS_TAG ? sc->tags : sc->ordinary;
}

Symbol *scope_lookup_local(Scope *sc, const char *name, Namespace ns)
{
    Symbol *sym;

    if (!sc || !name)
        return NULL;
    for (sym = chain_for(sc, ns); sym; sym = sym->next)
        if (sym->name == name) /* interned: pointer compare */
            return sym;
    return NULL;
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

    if (!sc)
        CGF_ICE("scope_declare: no scope");
    if (sym->ns == NS_TAG) {
        sym->next = sc->tags;
        sc->tags = sym;
    } else {
        sym->next = sc->ordinary;
        sc->ordinary = sym;
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
