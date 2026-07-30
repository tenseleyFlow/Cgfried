#include <stdio.h>
#include <string.h>

#include "pp/pp.h"

/* The directive engine: an include-stack of lexers under a single token
 * stream. pp_next yields text tokens; every `#` directive line (BOL # or
 * %:) is consumed internally. Skipped conditional groups honor ISO
 * 6.10p... semantics: only #if*/ /* family directives are even looked at. */

/* --- frame / stack helpers --------------------------------------------- */

static PpFrame *cur_frame(Preprocessor *pp)
{
    return pp->nframes ? &pp->frames[pp->nframes - 1] : NULL;
}

static void push_frame(Preprocessor *pp, SourceFile *sf, int found_dir)
{
    PpFrame *f;

    if (pp->nframes == pp->frames_cap) {
        size_t cap = pp->frames_cap ? pp->frames_cap * 2 : 8;
        PpFrame *grown =
            arena_alloc(pp->arena, cap * sizeof(PpFrame), _Alignof(PpFrame));
        if (pp->nframes)
            memcpy(grown, pp->frames, pp->nframes * sizeof(PpFrame));
        pp->frames = grown;
        pp->frames_cap = cap;
    }
    f = &pp->frames[pp->nframes++];
    memset(f, 0, sizeof(*f));
    pp_lexer_init(&f->lx, pp, sf);
    f->cond_base = pp->nconds;
    f->found_dir = found_dir;
}

static void pop_frame(Preprocessor *pp)
{
    PpFrame *f = cur_frame(pp);

    /* Conditionals may not straddle include boundaries (gcc parity). */
    while (pp->nconds > f->cond_base) {
        PpCond *c = &pp->conds[pp->nconds - 1];
        pp_diag_at(pp, DIAG_ERROR, c->loc, 1,
                   "unterminated #if at end of file");
        pp->nconds--;
    }
    buf_free(&f->lx.scratch);
    pp->nframes--;
}

static void push_cond(Preprocessor *pp, SrcLoc loc, bool parent_live,
                      bool group_true)
{
    PpCond *c;

    if (pp->nconds == pp->conds_cap) {
        size_t cap = pp->conds_cap ? pp->conds_cap * 2 : 16;
        PpCond *grown =
            arena_alloc(pp->arena, cap * sizeof(PpCond), _Alignof(PpCond));
        if (pp->nconds)
            memcpy(grown, pp->conds, pp->nconds * sizeof(PpCond));
        pp->conds = grown;
        pp->conds_cap = cap;
    }
    c = &pp->conds[pp->nconds++];
    c->loc = loc;
    c->parent_live = parent_live;
    c->live = parent_live && group_true;
    c->taken_any = group_true;
    c->seen_else = false;
}

static bool in_live_region(const Preprocessor *pp)
{
    return pp->nconds == 0 || pp->conds[pp->nconds - 1].live;
}

/* --- raw token flow ----------------------------------------------------- */

/* Next token from the CURRENT frame only; false at that frame's EOF. */
static bool frame_next(Preprocessor *pp, PpToken *out)
{
    return pp_lex_token(&cur_frame(pp)->lx, out);
}

/* True iff the current frame is at end-of-directive-line (next token would
 * start a new logical line). Never consumes: a directive's effects must be
 * recorded before the following line's tokens get lexed. */
static bool at_line_end(Preprocessor *pp)
{
    return pp_lex_at_line_end(&cur_frame(pp)->lx);
}

/* Collects the rest of the current directive line. Returns count; tokens
 * are arena-copied into *out. */
static u32 read_line(Preprocessor *pp, PpToken **out)
{
    PpToken t;
    PpToken *buf = NULL;
    u32 n = 0, cap = 0;

    while (!at_line_end(pp) && frame_next(pp, &t)) {
        if (n == cap) {
            u32 newcap = cap ? cap * 2 : 8;
            PpToken *grown = arena_alloc(pp->arena, newcap * sizeof(PpToken),
                                         _Alignof(PpToken));
            if (n)
                memcpy(grown, buf, n * sizeof(PpToken));
            buf = grown;
            cap = newcap;
        }
        buf[n++] = t;
    }
    *out = buf;
    return n;
}

/* --- #include resolution ------------------------------------------------ */

static const char *dir_of(Preprocessor *pp, const char *path)
{
    const char *slash = strrchr(path, '/');

    if (!slash)
        return ".";
    return arena_strndup(pp->arena, path, (size_t)(slash - path));
}

/* Builds the search chain for one lookup. Chain entries are directories;
 * index 0 is the includer's own dir for the quote form. */
static size_t build_chain(Preprocessor *pp, bool angled, const char **chain,
                          size_t max)
{
    size_t n = 0, i;

    if (!angled) {
        chain[n++] = dir_of(pp, cur_frame(pp)->lx.sf->path);
        for (i = 0; i < pp->n_iquote && n < max; i++)
            chain[n++] = pp->iquote_dirs[i];
    }
    for (i = 0; i < pp->n_include && n < max; i++)
        chain[n++] = pp->include_dirs[i];
    for (i = 0; i < pp->n_system && n < max; i++)
        chain[n++] = pp->system_dirs[i];
    return n;
}

static SourceFile *try_open(Preprocessor *pp, const char *dir, const char *name)
{
    size_t dlen = strlen(dir), nlen = strlen(name);
    char *path = arena_alloc(pp->arena, dlen + nlen + 2, 1);
    FILE *probe;

    memcpy(path, dir, dlen);
    path[dlen] = '/';
    memcpy(path + dlen + 1, name, nlen + 1);
    probe = fopen(path, "rb");
    if (!probe)
        return NULL;
    fclose(probe);
    return pp_source_load(pp, path);
}

static void do_include(Preprocessor *pp, const char *name, bool angled,
                       bool is_next, SrcLoc loc)
{
    const char *chain[2 + 3 * PP_MAX_DIRS];
    size_t n, start = 0, i;
    SourceFile *sf = NULL;
    int found = -1;

    if (pp->nframes >= PP_INCLUDE_DEPTH_MAX) {
        pp_diag_at(pp, DIAG_FATAL, loc, 1,
                   "#include nested depth %d exceeds maximum",
                   PP_INCLUDE_DEPTH_MAX);
        pp->fatal = true;
        return;
    }

    n = build_chain(pp, angled, chain, CGF_ARRAY_LEN(chain));
    if (is_next) {
        /* GNU #include_next: resume AFTER the dir the current file was
         * found in (glibc's own headers require this). Pedwarn hook is
         * Sprint 37. Main file (found_dir -1) searches from the start. */
        start = (size_t)(cur_frame(pp)->found_dir + 1);
        if (start > n)
            start = n;
    }

    for (i = start; i < n; i++) {
        sf = try_open(pp, chain[i], name);
        if (sf) {
            found = (int)i;
            break;
        }
    }
    /* Absolute paths bypass the chain. */
    if (!sf && name[0] == '/') {
        FILE *probe = fopen(name, "rb");
        if (probe) {
            fclose(probe);
            sf = pp_source_load(pp, name);
            found = -1;
        }
    }

    if (pp->verbose) {
        fprintf(stderr, "#include %c%s%c search starts here:\n",
                angled ? '<' : '"', name, angled ? '>' : '"');
        for (i = start; i < n; i++)
            fprintf(stderr, " %s\n", chain[i]);
        fprintf(stderr, "End of search list.\n");
    }

    if (!sf) {
        pp_diag_at(pp, DIAG_FATAL, loc, 1, "%s: No such file or directory",
                   name);
        pp->fatal = true; /* include errors are fatal (gcc parity) */
        return;
    }
    push_frame(pp, sf, found);
}

static void directive_include(Preprocessor *pp, SrcLoc dloc, bool is_next)
{
    PpFrame *f = cur_frame(pp);
    PpToken h;

    if (at_line_end(pp)) {
        pp_diag_at(pp, DIAG_ERROR, dloc, 1,
                   "#include expects <FILENAME> or \"FILENAME\"");
        return;
    }
    if (pp_lex_header_name(&f->lx, &h)) {
        /* <h-chars> or "q-chars" form, lexed as a header-name: // and slash-
         * star inside are h-chars, never comments; backslashes pass to the
         * OS untouched. */
        PpToken *rest;
        u32 nrest = read_line(pp, &rest);
        char *name;

        if (h.len < 2) {
            pp_diag_at(pp, DIAG_ERROR, h.loc, h.len, "empty header name");
            return;
        }
        if (nrest != 0)
            pp_diag_at(pp, DIAG_WARNING, rest[0].loc, rest[0].len,
                       "extra tokens at end of #include directive");
        name = arena_strndup(pp->arena, h.spelling + 1, h.len - 2);
        do_include(pp, name, h.spelling[0] == '<', is_next, dloc);
        return;
    }

    /* Computed include: macro-expand the line, then re-form a header name.
     * Without expansion support the seam hard-errors; a computed include
     * whose tokens are macro-free is malformed anyway. */
    {
        PpToken *toks;
        u32 n = read_line(pp, &toks);

        if (n == 0) {
            pp_diag_at(pp, DIAG_ERROR, dloc, 1,
                       "#include expects <FILENAME> or \"FILENAME\"");
            return;
        }
        if (pp_expansion_needed(pp, toks, n, "a computed #include"))
            return;
        if (toks[0].kind == PPTOK_STRLIT) {
            if (n > 1)
                pp_diag_at(pp, DIAG_WARNING, toks[1].loc, toks[1].len,
                           "extra tokens at end of #include directive");
            {
                char *name = arena_strndup(pp->arena, toks[0].spelling + 1,
                                           toks[0].len - 2);
                do_include(pp, name, false, is_next, dloc);
            }
            return;
        }
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "#include expects <FILENAME> or \"FILENAME\"");
    }
}

/* --- #line -------------------------------------------------------------- */

static void directive_line(Preprocessor *pp, PpToken *toks, u32 n, SrcLoc dloc,
                           u32 phys_next_line)
{
    u64 v = 0;
    const char *path = NULL;
    SourceFile *sf = cur_frame(pp)->lx.sf;
    u32 i;

    if (n == 0) {
        pp_diag_at(pp, DIAG_ERROR, dloc, 1, "#line expects a line number");
        return;
    }
    if (pp_expansion_needed(pp, toks, n, "#line"))
        return;
    if (toks[0].kind != PPTOK_PPNUM) {
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "#line expects a decimal line number");
        return;
    }
    for (i = 0; i < toks[0].len; i++) {
        char ch = toks[0].spelling[i];
        if (ch < '0' || ch > '9') {
            pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                       "#line expects a decimal line number");
            return;
        }
        v = v * 10 + (u64)(ch - '0');
        if (v > 0x7FFFFFFF)
            break;
    }
    if (v == 0 || v > 0x7FFFFFFF) {
        /* ISO: 1..2147483647; gcc accepts out-of-range with a pedwarn
         * (hook: Sprint 37) and clamps nothing. We accept and continue. */
        if (v == 0)
            v = 1;
    }
    if (n >= 2) {
        if (toks[1].kind != PPTOK_STRLIT) {
            pp_diag_at(pp, DIAG_ERROR, toks[1].loc, toks[1].len,
                       "#line filename must be a string literal");
            return;
        }
        path = arena_strndup(pp->arena, toks[1].spelling + 1, toks[1].len - 2);
        /* Trailing gcc linemarker flags (1..4) parsed and ignored. */
    }

    if (sf->nremaps == sf->remaps_cap) {
        u32 cap = sf->remaps_cap ? sf->remaps_cap * 2 : 4;
        PresumedRemap *grown = arena_alloc(
            pp->arena, cap * sizeof(PresumedRemap), _Alignof(PresumedRemap));
        if (sf->nremaps)
            memcpy(grown, sf->remaps, sf->nremaps * sizeof(PresumedRemap));
        sf->remaps = grown;
        sf->remaps_cap = cap;
    }
    /* Keep the previous file if this #line has none. */
    if (!path && sf->nremaps)
        path = sf->remaps[sf->nremaps - 1].path;
    sf->remaps[sf->nremaps].from_line = phys_next_line;
    sf->remaps[sf->nremaps].presumed_line = (u32)v;
    sf->remaps[sf->nremaps].path = path;
    sf->nremaps++;
}

/* --- #pragma ------------------------------------------------------------ */

static PpStdcSwitch parse_stdc_switch(const PpToken *t)
{
    if (strcmp(t->spelling, "ON") == 0)
        return PP_STDC_ON;
    if (strcmp(t->spelling, "OFF") == 0)
        return PP_STDC_OFF;
    return PP_STDC_DEFAULT;
}

/* Returns true if the pragma line should pass through to -E output. */
static bool directive_pragma(Preprocessor *pp, PpToken *toks, u32 n)
{
    if (n >= 1 && toks[0].kind == PPTOK_IDENT &&
        strcmp(toks[0].spelling, "once") == 0) {
        pp_diag_at(pp, DIAG_ERROR, toks[0].loc, toks[0].len,
                   "#pragma once is not yet supported" LANDS_IN_SPRINT(6));
        return false;
    }
    if (n >= 3 && toks[0].kind == PPTOK_IDENT &&
        strcmp(toks[0].spelling, "STDC") == 0 && toks[1].kind == PPTOK_IDENT &&
        toks[2].kind == PPTOK_IDENT) {
        /* Recorded now so "accepted" is not a lie; consumed by the
         * constexpr/fast-math sprints (15/36). Still passes through -E. */
        PpStdcSwitch sw = parse_stdc_switch(&toks[2]);
        if (strcmp(toks[1].spelling, "FP_CONTRACT") == 0)
            pp->stdc.fp_contract = sw;
        else if (strcmp(toks[1].spelling, "FENV_ACCESS") == 0)
            pp->stdc.fenv_access = sw;
        else if (strcmp(toks[1].spelling, "CX_LIMITED_RANGE") == 0)
            pp->stdc.cx_limited_range = sw;
        return true;
    }
    /* Unknown pragmas: ignored (-Wunknown-pragmas hook is Sprint 37; gcc
     * default off) and passed through under -E verbatim. */
    return true;
}

/* --- conditionals ------------------------------------------------------- */

typedef enum {
    DK_IF,
    DK_IFDEF,
    DK_IFNDEF,
    DK_ELIF,
    DK_ELSE,
    DK_ENDIF,
    DK_INCLUDE,
    DK_INCLUDE_NEXT,
    DK_DEFINE,
    DK_UNDEF,
    DK_LINE,
    DK_ERROR,
    DK_WARNING,
    DK_PRAGMA,
    DK_LINEMARKER,
    DK_NULL,
    DK_UNKNOWN,
} DirKind;

static DirKind classify(const PpToken *t)
{
    const char *s;

    if (t->kind == PPTOK_PPNUM)
        return DK_LINEMARKER; /* gcc `# 1 "file"` form */
    if (t->kind != PPTOK_IDENT)
        return DK_UNKNOWN;
    s = t->spelling;
    if (strcmp(s, "if") == 0)
        return DK_IF;
    if (strcmp(s, "ifdef") == 0)
        return DK_IFDEF;
    if (strcmp(s, "ifndef") == 0)
        return DK_IFNDEF;
    if (strcmp(s, "elif") == 0)
        return DK_ELIF;
    if (strcmp(s, "else") == 0)
        return DK_ELSE;
    if (strcmp(s, "endif") == 0)
        return DK_ENDIF;
    if (strcmp(s, "include") == 0)
        return DK_INCLUDE;
    if (strcmp(s, "include_next") == 0)
        return DK_INCLUDE_NEXT;
    if (strcmp(s, "define") == 0)
        return DK_DEFINE;
    if (strcmp(s, "undef") == 0)
        return DK_UNDEF;
    if (strcmp(s, "line") == 0)
        return DK_LINE;
    if (strcmp(s, "error") == 0)
        return DK_ERROR;
    if (strcmp(s, "warning") == 0)
        return DK_WARNING;
    if (strcmp(s, "pragma") == 0)
        return DK_PRAGMA;
    return DK_UNKNOWN;
}

/* Renders directive tokens with single-space SPACE fidelity (#error text). */
static const char *spell_line(Preprocessor *pp, const PpToken *toks, u32 n)
{
    Buf b;
    char *s;
    u32 i;

    buf_init(&b);
    for (i = 0; i < n; i++) {
        if (i && (toks[i].flags & PPTOK_F_SPACE))
            buf_push_u8(&b, ' ');
        buf_append(&b, toks[i].spelling, toks[i].len);
    }
    s = arena_strndup(pp->arena, (const char *)b.data, b.len);
    buf_free(&b);
    return s;
}

/* Handles one directive line; the leading # token is `hash`. Returns true
 * if the line should be passed through to the -E stream (#pragma only). */
static bool handle_directive(Preprocessor *pp, const PpToken *hash,
                             PpToken **passthrough, u32 *npassthrough)
{
    PpFrame *f = cur_frame(pp);
    PpToken name;
    DirKind k;
    bool live = in_live_region(pp);

    *passthrough = NULL;
    *npassthrough = 0;

    /* Null directive: `#` alone on a line (ISO 6.10p2). */
    if (at_line_end(pp))
        return false;
    if (!frame_next(pp, &name))
        return false;
    k = classify(&name);

    /* Skipped regions look ONLY at conditional directives — `#garbage`
     * inside a false #if is fine (ISO); anything else breaks real headers. */
    if (!live && k != DK_IF && k != DK_IFDEF && k != DK_IFNDEF &&
        k != DK_ELIF && k != DK_ELSE && k != DK_ENDIF) {
        PpToken *toks;
        read_line(pp, &toks);
        return false;
    }

    switch (k) {
    case DK_IF: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        bool taken = false;
        if (live)
            taken = pp_eval_condition(pp, toks, n, name.loc);
        push_cond(pp, name.loc, live, taken);
        return false;
    }
    case DK_IFDEF:
    case DK_IFNDEF: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        bool taken = false;
        if (n == 0 || toks[0].kind != PPTOK_IDENT) {
            if (live)
                pp_diag_at(pp, DIAG_ERROR, name.loc, name.len,
                           "#%s expects an identifier", name.spelling);
        } else if (live) {
            bool def = pp_macro_lookup(pp, toks[0].spelling) != NULL;
            taken = (k == DK_IFDEF) ? def : !def;
            if (n > 1)
                pp_diag_at(pp, DIAG_WARNING, toks[1].loc, toks[1].len,
                           "extra tokens at end of #%s directive",
                           name.spelling);
        }
        push_cond(pp, name.loc, live, taken);
        return false;
    }
    case DK_ELIF: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        PpCond *c;
        if (pp->nconds <= f->cond_base) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len, "#elif without #if");
            return false;
        }
        c = &pp->conds[pp->nconds - 1];
        if (c->seen_else) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len, "#elif after #else");
            pp_diag_at(pp, DIAG_NOTE, c->loc, 1, "conditional started here");
            return false;
        }
        if (c->parent_live && !c->taken_any) {
            bool taken = pp_eval_condition(pp, toks, n, name.loc);
            c->live = taken;
            c->taken_any = taken;
        } else {
            c->live = false;
        }
        return false;
    }
    case DK_ELSE: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        PpCond *c;
        if (pp->nconds <= f->cond_base) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len, "#else without #if");
            return false;
        }
        c = &pp->conds[pp->nconds - 1];
        if (c->seen_else) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len, "#else after #else");
            pp_diag_at(pp, DIAG_NOTE, c->loc, 1, "conditional started here");
            return false;
        }
        c->seen_else = true;
        c->live = c->parent_live && !c->taken_any;
        c->taken_any = true;
        if (n != 0 && c->parent_live)
            pp_diag_at(pp, DIAG_WARNING, toks[0].loc, toks[0].len,
                       "extra tokens at end of #else directive");
        return false;
    }
    case DK_ENDIF: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        if (pp->nconds <= f->cond_base) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len,
                       "#endif without #if");
            return false;
        }
        pp->nconds--;
        if (n != 0 && in_live_region(pp))
            pp_diag_at(pp, DIAG_WARNING, toks[0].loc, toks[0].len,
                       "extra tokens at end of #endif directive");
        return false;
    }
    case DK_INCLUDE:
    case DK_INCLUDE_NEXT:
        directive_include(pp, name.loc, k == DK_INCLUDE_NEXT);
        return false;
    case DK_DEFINE: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        pp_macro_define_line(pp, toks, n);
        return false;
    }
    case DK_UNDEF: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        if (n == 0 || toks[0].kind != PPTOK_IDENT) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len,
                       "#undef expects an identifier");
            return false;
        }
        if (n > 1)
            pp_diag_at(pp, DIAG_WARNING, toks[1].loc, toks[1].len,
                       "extra tokens at end of #undef directive");
        pp_macro_undef(pp, toks[0].spelling, toks[0].loc);
        return false;
    }
    case DK_LINEMARKER: {
        /* gcc `# 1 "file" [flags]`: #line synonym; flags ignored. */
        PpToken *rest;
        u32 nrest = read_line(pp, &rest);
        PpToken *all = arena_alloc(pp->arena, (nrest + 1) * sizeof(PpToken),
                                   _Alignof(PpToken));
        u32 next_phys;
        all[0] = name;
        if (nrest)
            memcpy(all + 1, rest, nrest * sizeof(PpToken));
        {
            FileId fid;
            u32 line, col;
            pp_loc_resolve(&pp->loc, name.loc, &fid, &line, &col);
            next_phys = line + 1;
        }
        directive_line(pp, all, nrest + 1, name.loc, next_phys);
        return false;
    }
    case DK_LINE: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        FileId fid;
        u32 line, col;
        pp_loc_resolve(&pp->loc, name.loc, &fid, &line, &col);
        directive_line(pp, toks, n, name.loc, line + 1);
        return false;
    }
    case DK_ERROR:
    case DK_WARNING: {
        /* Original spelling, never expanded. gcc continues preprocessing
         * after #error; the exit code carries the failure. #warning is a
         * GNU extension until C23 (pedwarn hook: Sprint 37). */
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        pp_diag_at(pp, k == DK_ERROR ? DIAG_ERROR : DIAG_WARNING, name.loc,
                   name.len, "#%s%s%s", name.spelling, n ? " " : "",
                   n ? spell_line(pp, toks, n) : "");
        return false;
    }
    case DK_PRAGMA: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        if (directive_pragma(pp, toks, n)) {
            /* Pass the whole line through to -E: rebuild `# pragma ...`
             * tokens for the output stream. */
            PpToken *out = arena_alloc(pp->arena, (n + 2) * sizeof(PpToken),
                                       _Alignof(PpToken));
            out[0] = *hash;
            out[1] = name;
            if (n)
                memcpy(out + 2, toks, n * sizeof(PpToken));
            *passthrough = out;
            *npassthrough = n + 2;
            return true;
        }
        return false;
    }
    case DK_NULL:
        return false;
    case DK_UNKNOWN:
        pp_diag_at(pp, DIAG_ERROR, name.loc, name.len,
                   "invalid preprocessing directive #%s",
                   name.kind == PPTOK_IDENT ? name.spelling : "");
        {
            PpToken *toks;
            read_line(pp, &toks);
        }
        return false;
    }
    return false;
}

/* --- engine ------------------------------------------------------------- */

void pp_begin(Preprocessor *pp, SourceFile *main_file, SourceFile *cmdline)
{
    push_frame(pp, main_file, -1);
    if (cmdline)
        push_frame(pp, cmdline, -1); /* processed first, popped first */
}

void pp_end(Preprocessor *pp)
{
    while (pp->nframes)
        pop_frame(pp);
}

bool pp_next(Preprocessor *pp, PpToken *out)
{
    for (;;) {
        PpToken t;

        if (pp->nframes == 0)
            return false;
        if (pp->fatal) {
            pp_end(pp);
            return false;
        }

        /* Pending passthrough tokens (#pragma under -E)? */
        if (pp->npass) {
            *out = pp->pass[pp->pass_pos++];
            if (pp->pass_pos == pp->npass) {
                pp->npass = 0;
                pp->pass_pos = 0;
            }
            return true;
        }

        if (!frame_next(pp, &t)) {
            pop_frame(pp);
            continue;
        }

        if (t.kind == PPTOK_PUNCT && t.punct == PUNCT_HASH &&
            (t.flags & PPTOK_F_BOL)) {
            PpToken *pass;
            u32 npass;
            if (handle_directive(pp, &t, &pass, &npass) && npass) {
                pp->pass = pass;
                pp->npass = npass;
                pp->pass_pos = 0;
            }
            continue;
        }

        if (!in_live_region(pp))
            continue; /* skipped group: discard text tokens */

        /* Text token. Defined macro names route through the expansion
         * seam; everything else passes untouched. */
        if (t.kind == PPTOK_IDENT && pp_macro_lookup(pp, t.spelling)) {
            pp_expansion_needed(pp, &t, 1, "program text");
            continue;
        }
        *out = t;
        return true;
    }
}
