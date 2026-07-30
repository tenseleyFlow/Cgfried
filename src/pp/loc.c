#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pp/pp.h"

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

SrcLoc pp_loc_file(LocTable *t, FileId f, u32 line, u32 col)
{
    LocEnt e;

    e.w0 = line;
    e.w1 = col;
    e.w2 = (u32)f << 1;
    return push_ent(t, e);
}

SrcLoc pp_loc_expansion(LocTable *t, SrcLoc spelled_at, SrcLoc expanded_from)
{
    LocEnt e;

    if (spelled_at == SRCLOC_INVALID || expanded_from == SRCLOC_INVALID)
        CGF_ICE("pp_loc_expansion: invalid operand loc");
    e.w0 = spelled_at;
    e.w1 = expanded_from;
    e.w2 = 1;
    return push_ent(t, e);
}

static const LocEnt *ent(const LocTable *t, SrcLoc loc)
{
    if (loc == SRCLOC_INVALID || (size_t)loc > t->len)
        CGF_ICE("bad SrcLoc %u (table has %zu)", (unsigned)loc, t->len);
    return &t->ents[loc - 1];
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

void pp_diag_at(Preprocessor *pp, DiagLevel lvl, SrcLoc loc, u32 len,
                const char *fmt, ...)
{
    FileId f = 0;
    u32 line = 0, col = 0;
    Span sp;
    va_list ap, ap2;
    int need;
    char *msg;

    memset(&sp, 0, sizeof(sp));
    if (loc != SRCLOC_INVALID)
        pp_loc_resolve(&pp->loc, loc, &f, &line, &col);
    sp.file_id =
        (f && (size_t)f <= pp->nfiles) ? pp->files[f - 1]->diag_file_id : 0;
    sp.line = line;
    sp.col = col;
    sp.len = len;

    /* #line presumed remap: display-time only; physical stays true. */
    if (f && (size_t)f <= pp->nfiles) {
        const SourceFile *sf = pp->files[f - 1];
        u32 lo = 0, hi = sf->nremaps; /* last remap with from_line <= line */

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
            sp.presumed_path = r->path; /* NULL keeps the real path */
        }
    }

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
}
