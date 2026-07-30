#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "pp/pp.h"

void pp_init(Preprocessor *pp, Arena *arena, DiagCtx *diag, Interner *interner)
{
    memset(pp, 0, sizeof(*pp));
    pp->arena = arena;
    pp->diag = diag;
    pp->interner = interner;
    pp_loc_init(&pp->loc);
    strmap_init(&pp->macros);
}

/* Phase 1 normalization, in dependency order:
 *   1. line endings: CRLF and lone CR -> LF
 *   2. trigraphs (only under -trigraphs; BEFORE splicing so a `??/` at end
 *      of line becomes a backslash that splices)
 * Splicing itself (phase 2) is NOT a rewrite — the lexer skips backslash-
 * newline in place so tokens keep true physical locations. */
static char *normalize(Preprocessor *pp, const char *in, size_t in_len,
                       size_t *out_len)
{
    char *out = arena_alloc(pp->arena, in_len + 2, 1); /* +LF +NUL */
    size_t n = 0, i = 0;

    while (i < in_len) {
        char c = in[i];

        if (c == '\r') {
            out[n++] = '\n';
            i += (i + 1 < in_len && in[i + 1] == '\n') ? 2 : 1;
            continue;
        }
        if (pp->trigraphs && c == '?' && i + 2 < in_len && in[i + 1] == '?') {
            char rep = 0;
            switch (in[i + 2]) {
            case '=':
                rep = '#';
                break;
            case '(':
                rep = '[';
                break;
            case ')':
                rep = ']';
                break;
            case '<':
                rep = '{';
                break;
            case '>':
                rep = '}';
                break;
            case '/':
                rep = '\\';
                break;
            case '\'':
                rep = '^';
                break;
            case '!':
                rep = '|';
                break;
            case '-':
                rep = '~';
                break;
            default:
                break;
            }
            if (rep) {
                out[n++] = rep;
                i += 3;
                continue;
            }
        }
        out[n++] = c;
        i++;
    }
    /* C11 5.1.1.2p1(2): source must end in a newline. gcc accepts with a
     * pedantic warning; we append silently for now (the -Wpedantic hook is
     * Sprint 37's warning machinery — never an error). */
    if (n == 0 || out[n - 1] != '\n')
        out[n++] = '\n';
    out[n] = '\0';
    *out_len = n;
    return out;
}

static SourceFile *add_normalized(Preprocessor *pp, const char *path,
                                  char *contents, size_t size)
{
    SourceFile *sf =
        arena_alloc(pp->arena, sizeof(SourceFile), _Alignof(SourceFile));
    size_t i, nlines;

    if (pp->nfiles == pp->files_cap) {
        size_t cap = pp->files_cap ? pp->files_cap * 2 : 8;
        SourceFile **grown = arena_alloc(pp->arena, cap * sizeof(SourceFile *),
                                         _Alignof(SourceFile *));
        if (pp->nfiles) /* memcpy from NULL is UB even for 0 */
            memcpy(grown, pp->files, pp->nfiles * sizeof(SourceFile *));
        pp->files = grown;
        pp->files_cap = cap;
    }
    if (pp->nfiles >= 0xFFFF)
        CGF_ICE("too many source files (FileId is 16-bit)");

    memset(sf, 0, sizeof(*sf)); /* arena memory is never pre-zeroed */
    sf->id = (FileId)(pp->nfiles + 1);
    sf->path = arena_strdup(pp->arena, path);
    sf->contents = contents;
    sf->size = (u32)size;
    sf->diag_file_id = diag_add_file(pp->diag, path, contents, size);

    /* Physical line starts over the NORMALIZED buffer. */
    nlines = 1;
    for (i = 0; i < size; i++)
        if (contents[i] == '\n' && i + 1 < size)
            nlines++;
    sf->line_offsets =
        arena_alloc(pp->arena, nlines * sizeof(u32), _Alignof(u32));
    sf->nlines = (u32)nlines;
    sf->line_offsets[0] = 0;
    {
        u32 ln = 1;
        for (i = 0; i < size; i++)
            if (contents[i] == '\n' && i + 1 < size)
                sf->line_offsets[ln++] = (u32)(i + 1);
    }

    pp->files[pp->nfiles++] = sf;
    return sf;
}

SourceFile *pp_source_add_buffer(Preprocessor *pp, const char *path,
                                 const char *bytes, size_t len)
{
    size_t norm_len;
    char *norm = normalize(pp, bytes, len, &norm_len);

    return add_normalized(pp, path, norm, norm_len);
}

SourceFile *pp_source_load(Preprocessor *pp, const char *path)
{
    FILE *f = fopen(path, "rb");
    char *raw;
    long sz;
    Span no_span = {0};
    SourceFile *sf;
    struct stat st;
    bool have_st = false;

    if (f && fstat(fileno(f), &st) == 0)
        have_st = true; /* identity captured while OPEN (#pragma once) */
    if (!f) {
        diag_emit(pp->diag, DIAG_ERROR, no_span, "cannot open '%s': %s", path,
                  strerror(errno));
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        diag_emit(pp->diag, DIAG_ERROR, no_span, "cannot read '%s': %s", path,
                  strerror(errno));
        return NULL;
    }
    raw = arena_alloc(pp->arena, (size_t)sz + 1, 1);
    if (sz > 0 && fread(raw, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        diag_emit(pp->diag, DIAG_ERROR, no_span, "cannot read '%s': %s", path,
                  strerror(errno));
        return NULL;
    }
    fclose(f);

    sf = pp_source_add_buffer(pp, path, raw, (size_t)sz);
    if (sf && have_st) {
        sf->st_dev = (u64)st.st_dev;
        sf->st_ino = (u64)st.st_ino;
    }
    return sf;
}
