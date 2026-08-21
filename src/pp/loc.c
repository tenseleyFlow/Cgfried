#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/toolchain.h"
#include "pp/pp.h"
#include "warn/warn.h"

void pp_loc_init(LocTable *t)
{
    memset(t, 0, sizeof(*t));
}

void pp_loc_free(LocTable *t)
{
    free(t->ents);
    memset(t, 0, sizeof(*t));
}

static SrcLoc push_ent(LocTable *t, LocEnt e)
{
    if (t->len == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 256;
        t->ents = cgf_xrealloc(t->ents, t->cap * sizeof(LocEnt));
    }
    t->ents[t->len] = e;
    t->len++;
    return (SrcLoc)t->len; /* ids are index+1; 0 = SRCLOC_INVALID */
}

static const LocEnt *ent(const LocTable *t, SrcLoc loc);

SrcLoc pp_loc_file(LocTable *t, FileId f, u32 line, u32 col)
{
    LocEnt e;

    e.seq = 0;
    e.macro_name = NULL;
    e.macro_def_loc = SRCLOC_INVALID;
    e.w0 = line;
    e.w1 = col;
    e.w2 = (u32)f << 1;
    return push_ent(t, e);
}

SrcLoc pp_loc_expansion(LocTable *t, SrcLoc spelled_at, SrcLoc expanded_from,
                        const char *macro_name, SrcLoc macro_def_loc)
{
    LocEnt e;

    if (spelled_at == SRCLOC_INVALID || expanded_from == SRCLOC_INVALID)
        CGF_ICE("pp_loc_expansion: invalid operand loc");
    e.seq = 0;
    e.w0 = spelled_at;
    e.w1 = expanded_from;
    e.w2 = 1;
    e.macro_name = macro_name;
    e.macro_def_loc = macro_def_loc;
    return push_ent(t, e);
}

static size_t expansion_depth(const LocTable *t, SrcLoc loc, SrcLoc *terminal)
{
    size_t n = 0;

    while (loc != SRCLOC_INVALID && pp_loc_is_expansion(t, loc)) {
        n++;
        loc = pp_loc_expansion_parent(t, loc);
    }
    *terminal = loc;
    return n;
}

static void collect_expansions(const LocTable *t, SrcLoc loc, SrcLoc *frames,
                               size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        frames[i] = loc;
        loc = pp_loc_expansion_parent(t, loc);
    }
}

SrcLoc pp_loc_graft_expansions(LocTable *t, SrcLoc loc, SrcLoc expanded_from,
                               const char *macro_name, SrcLoc macro_def_loc)
{
    SrcLoc old_terminal, inv_terminal;
    SrcLoc *old_frames;
    SrcLoc *inv_frames;
    SrcLoc parent;
    SrcLoc spelling;
    size_t nold, ninv, keep, oi, ii;

    if (!pp_loc_is_expansion(t, loc))
        CGF_ICE("pp_loc_graft_expansions: location is not an expansion");
    if (expanded_from == SRCLOC_INVALID)
        CGF_ICE("pp_loc_graft_expansions: invalid invocation");

    nold = expansion_depth(t, loc, &old_terminal);
    ninv = expansion_depth(t, expanded_from, &inv_terminal);
    old_frames = cgf_xmalloc(nold * sizeof(*old_frames));
    inv_frames = ninv ? cgf_xmalloc(ninv * sizeof(*inv_frames)) : NULL;
    collect_expansions(t, loc, old_frames, nold);
    if (ninv)
        collect_expansions(t, expanded_from, inv_frames, ninv);

    /* A common suffix denotes the same enclosing expansions reached through
     * the argument and through the new macro invocation. Compare outward-in:
     * exact terminal identity establishes the base case, then name/definition
     * identity extends it without conflating recursive invocations. */
    oi = nold;
    ii = ninv;
    if (old_terminal == inv_terminal) {
        while (oi > 0 && ii > 0) {
            const LocEnt *old = ent(t, old_frames[oi - 1]);
            const LocEnt *inv = ent(t, inv_frames[ii - 1]);

            if (!old->macro_name || old->macro_name != inv->macro_name ||
                old->macro_def_loc != inv->macro_def_loc)
                break;
            oi--;
            ii--;
        }
    }
    keep = oi;
    spelling = keep < nold ? old_frames[keep] : old_terminal;
    parent =
        pp_loc_expansion(t, spelling, expanded_from, macro_name, macro_def_loc);
    for (oi = keep; oi > 0; oi--) {
        LocEnt old = *ent(t, old_frames[oi - 1]);
        parent = pp_loc_expansion(t, old.w0, parent, old.macro_name,
                                  old.macro_def_loc);
    }

    free(inv_frames);
    free(old_frames);
    return parent;
}

u32 pp_loc_mark(LocTable *t, SrcLoc loc)
{
    LocEnt *e;

    if (loc == SRCLOC_INVALID)
        return 0;
    if ((size_t)loc > t->len)
        CGF_ICE("bad SrcLoc %u (table has %zu)", (unsigned)loc, t->len);
    e = &t->ents[loc - 1];
    if (e->seq == 0) {
        if (t->next_seq == UINT32_MAX)
            CGF_ICE("preprocessor lexical sequence exhausted");
        e->seq = ++t->next_seq;
    }
    return e->seq;
}

static const LocEnt *ent(const LocTable *t, SrcLoc loc)
{
    if (loc == SRCLOC_INVALID || (size_t)loc > t->len)
        CGF_ICE("bad SrcLoc %u (table has %zu)", (unsigned)loc, t->len);
    return &t->ents[loc - 1];
}

const char *pp_loc_macro_name(const LocTable *t, SrcLoc loc)
{
    const LocEnt *e = ent(t, loc);

    return (e->w2 & 1u) ? e->macro_name : NULL;
}

SrcLoc pp_loc_macro_def(const LocTable *t, SrcLoc loc)
{
    const LocEnt *e = ent(t, loc);

    return (e->w2 & 1u) ? e->macro_def_loc : SRCLOC_INVALID;
}

bool pp_loc_is_expansion(const LocTable *t, SrcLoc loc)
{
    return (ent(t, loc)->w2 & 1u) != 0;
}

void pp_loc_resolve(const LocTable *t, SrcLoc loc, FileId *f, u32 *line,
                    u32 *col)
{
    const LocEnt *e = ent(t, loc);

    /* Walk spelled_at links down to the physical spelling location. */
    while (e->w2 & 1u)
        e = ent(t, e->w0);
    if (f)
        *f = (FileId)(e->w2 >> 1);
    if (line)
        *line = e->w0;
    if (col)
        *col = e->w1;
}

SrcLoc pp_loc_expansion_parent(const LocTable *t, SrcLoc loc)
{
    const LocEnt *e = ent(t, loc);

    return (e->w2 & 1u) ? (SrcLoc)e->w1 : SRCLOC_INVALID;
}

/* Fills a Span for one resolved location (physical + #line display info). */
static Span span_at(Preprocessor *pp, SrcLoc loc, u32 len)
{
    Span sp;
    FileId f = 0;
    u32 line = 0, col = 0;

    memset(&sp, 0, sizeof(sp));
    if (loc != SRCLOC_INVALID)
        pp_loc_resolve(&pp->loc, loc, &f, &line, &col);
    sp.file_id =
        (f && (size_t)f <= pp->nfiles) ? pp->files[f - 1]->diag_file_id : 0;
    sp.line = line;
    sp.col = col;
    sp.len = len;
    if (f && (size_t)f <= pp->nfiles) {
        const SourceFile *sf = pp->files[f - 1];
        u32 lo = 0, hi = sf->nremaps;

        while (hi > lo) {
            u32 mid = lo + (hi - lo) / 2;
            if (sf->remaps[mid].from_line <= line)
                lo = mid + 1;
            else
                hi = mid;
        }
        if (lo > 0) {
            const PresumedRemap *r = &sf->remaps[lo - 1];
            sp.presumed_line = r->presumed_line + (line - r->from_line);
            sp.presumed_path = r->path;
        }
    }
    return sp;
}

bool pp_loc_is_system(Preprocessor *pp, SrcLoc loc)
{
    FileId f = 0;
    u32 line = 0;
    const SourceFile *sf;

    if (loc == SRCLOC_INVALID)
        return false;
    pp_loc_resolve(&pp->loc, loc, &f, &line, NULL);
    if (!f || (size_t)f > pp->nfiles)
        return false;
    sf = pp->files[f - 1];
    return sf->is_system ||
           (sf->system_from_line && line >= sf->system_from_line);
}

/* Diagnostics point at the token spelling and render the macro backtrace.
 * Debuggers instead need the executable statement's user-visible origin:
 * walk expansion parents through nested macros to the outermost invocation
 * and preserve that second location in the DiagCtx registry. */
Span pp_span(Preprocessor *pp, SrcLoc loc, u32 len)
{
    Span sp = span_at(pp, loc, len);
    SrcLoc invocation = loc;
    bool from_macro =
        loc != SRCLOC_INVALID && pp_loc_is_expansion(&pp->loc, loc);

    while (invocation != SRCLOC_INVALID &&
           pp_loc_is_expansion(&pp->loc, invocation))
        invocation = pp_loc_expansion_parent(&pp->loc, invocation);
    /* Raw token flow normally stamps before any consumer sees the token.
     * The fallback keeps direct location-unit callers deterministic. */
    sp.seq = pp_loc_mark(&pp->loc, loc);
    if (pp_loc_is_system(pp, loc))
        sp.origin |= SPAN_ORIGIN_SYSTEM_SPELLING;
    if (from_macro) {
        sp.origin |= SPAN_ORIGIN_ANY_MACRO;
        if (pp_loc_is_system(pp, loc))
            sp.origin |= SPAN_ORIGIN_SYSTEM_MACRO;
    }
    if (invocation != loc && invocation != SRCLOC_INVALID)
        sp.debug_loc =
            diag_add_debug_span(pp->diag, span_at(pp, invocation, len));
    return sp;
}

/* Macro-expansion backtrace (gcc order: innermost first). Each frame gets
 * "in expansion of macro 'X'" carets at the USE site; each DISTINCT macro
 * additionally gets one "macro 'X' defined here" note per diagnostic.
 * Deep chains are real (X-macro stacks go 50+ deep), so frames are capped
 * at 8 with an elision note; CGF_DIAG_FULL_BACKTRACE=1 disables the cap. */
#define BT_MAX_FRAMES 8
#define BT_HEAD 4
#define BT_TAIL 3

static void emit_expansion_chain(Preprocessor *pp, SrcLoc loc)
{
    SrcLoc frames[256];
    const char *seen[256];
    size_t nframes = 0, nseen = 0, i;
    bool full = cgf_env("CGF_DIAG_FULL_BACKTRACE") != NULL;
    size_t head, tail;

    for (; loc != SRCLOC_INVALID && pp_loc_is_expansion(&pp->loc, loc);
         loc = pp_loc_expansion_parent(&pp->loc, loc)) {
        if (nframes == CGF_ARRAY_LEN(frames))
            break;
        frames[nframes++] = loc;
    }
    if (nframes == 0)
        return;

    head = nframes;
    tail = 0;
    if (!full && nframes > BT_MAX_FRAMES) {
        head = BT_HEAD;
        tail = BT_TAIL;
    }

    for (i = 0; i < nframes; i++) {
        const char *name;
        SrcLoc def;

        if (i == head && tail) {
            Span none;
            memset(&none, 0, sizeof(none));
            diag_emit(pp->diag, DIAG_NOTE, none, "(skipped %zu expansions)",
                      nframes - head - tail);
            i = nframes - tail - 1;
            continue;
        }
        name = pp_loc_macro_name(&pp->loc, frames[i]);
        if (!name)
            continue;
        /* The use site is this frame's parent (where the macro was
         * invoked); the innermost frame's own span is the message's. */
        diag_emit(pp->diag, DIAG_NOTE,
                  pp_span(pp, pp_loc_expansion_parent(&pp->loc, frames[i]),
                          (u32)strlen(name)),
                  "in expansion of macro '%s'", name);

        {
            bool dup = false;
            size_t k;
            for (k = 0; k < nseen; k++)
                if (seen[k] == name)
                    dup = true;
            if (dup)
                continue;
            if (nseen < CGF_ARRAY_LEN(seen))
                seen[nseen++] = name;
        }
        def = pp_loc_macro_def(&pp->loc, frames[i]);
        if (def != SRCLOC_INVALID)
            diag_emit(pp->diag, DIAG_NOTE, pp_span(pp, def, (u32)strlen(name)),
                      "macro '%s' defined here", name);
    }
}

void pp_diag_at(Preprocessor *pp, DiagLevel lvl, SrcLoc loc, u32 len,
                const char *fmt, ...)
{
    Span sp = pp_span(pp, loc, len);
    va_list ap, ap2;
    int need;
    char *msg;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        CGF_ICE("pp_diag_at: vsnprintf failed on format \"%s\"", fmt);
    msg = arena_alloc(pp->arena, (size_t)need + 1, 1);
    vsnprintf(msg, (size_t)need + 1, fmt, ap2);
    va_end(ap2);

    diag_emit(pp->diag, lvl, sp, "%s", msg);
    /* Anything diagnosed inside a macro expansion gets its chain. This is
     * what the Sprint-3 location design was for; Sprint 8+ front-end
     * diagnostics inherit it for free. Notes never recurse (they carry
     * already-resolved spans). */
    if (lvl != DIAG_NOTE)
        emit_expansion_chain(pp, loc);
}

void pp_warn_at(Preprocessor *pp, WarnId id, SrcLoc loc, u32 len,
                const char *fmt, ...)
{
    Span sp = pp_span(pp, loc, len);
    va_list ap, ap2;
    int need;
    char *msg;

    if (!pp->warn)
        CGF_ICE("pp_warn_at called without a warning context");
    if (!warn_enabled(pp->warn, id, sp))
        return;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        CGF_ICE("pp_warn_at: vsnprintf failed on format \"%s\"", fmt);
    msg = arena_alloc(pp->arena, (size_t)need + 1, 1);
    vsnprintf(msg, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    warn_at(pp->warn, id, sp, "%s", msg);
    if (loc != SRCLOC_INVALID && pp_loc_is_expansion(&pp->loc, loc))
        emit_expansion_chain(pp, loc);
}

void pp_pedwarn_at(Preprocessor *pp, WarnId id, SrcLoc loc, u32 len,
                   const char *fmt, ...)
{
    Span sp = pp_span(pp, loc, len);
    va_list ap, ap2;
    int need;
    char *msg;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        CGF_ICE("pp_pedwarn_at: vsnprintf failed on format \"%s\"", fmt);
    msg = arena_alloc(pp->arena, (size_t)need + 1, 1);
    vsnprintf(msg, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    if (!pp->warn)
        CGF_ICE("pp_pedwarn_at called without a warning context");
    warn_pedwarn_at(pp->warn, id, sp, "%s", msg);
    if (loc != SRCLOC_INVALID && pp_loc_is_expansion(&pp->loc, loc))
        emit_expansion_chain(pp, loc);
}
