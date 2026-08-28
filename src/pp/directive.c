#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "pp/pp.h"
#include "warn/warn.h"

/* The directive engine: an include-stack of lexers under a single token
 * stream. pp_next yields text tokens; every `#` directive line (BOL # or
 * %:) is consumed internally. Skipped conditional groups honor ISO
 * 6.10p... semantics: only #if*/ /* family directives are even looked at. */

/* --- frame / stack helpers --------------------------------------------- */

static void fcache_record(Preprocessor *pp, u64 dev, u64 ino, const char *guard,
                          const char *path);

static PpFrame *cur_frame(Preprocessor *pp)
{
    return pp->nframes ? &pp->frames[pp->nframes - 1] : NULL;
}

static void push_frame(Preprocessor *pp, SourceFile *sf,
                       PpSearchKind found_kind, int found_index,
                       bool search_angled, const char *search_includer_dir,
                       PpDirIdentity search_includer_identity)
{
    PpFrame *f;

    pp_source_finalize(pp, sf);

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
    f->found_kind = found_kind;
    f->found_index = found_index;
    f->search_angled = search_angled;
    f->search_includer_dir = search_includer_dir;
    f->search_includer_identity = search_includer_identity;
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
    /* Finalize include-guard shape (no behavior change this sprint:
     * Sprint 7's fast path is the consumer). */
    if (f->guard_state == GUARD_AFTER_ENDIF && f->guard_macro) {
        f->lx.sf->guard_macro = f->guard_macro;
        fcache_record(pp, f->lx.sf->st_dev, f->lx.sf->st_ino, f->guard_macro,
                      f->lx.sf->path);
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

/* --- include-guard shape detection (Sprint 7 consumes guard_macro) ------
 * Qualifying shape: the file's FIRST directive is #ifndef X (or
 * #if !defined X / #if !defined(X)), immediately followed by #define X,
 * whose matching #endif is the last token-producing thing in the file, and
 * no tokens live outside that conditional. Note for Sprint 7: a body that
 * does #undef X still QUALIFIES for shape, so the fast path must re-check
 * that X is still defined at reuse time. */

static void guard_saw_text(PpFrame *f)
{
    /* Any text token outside the guard conditional disqualifies. */
    if (f->guard_state == GUARD_EXPECT_IFNDEF ||
        f->guard_state == GUARD_EXPECT_DEFINE ||
        f->guard_state == GUARD_AFTER_ENDIF)
        f->guard_state = GUARD_DISQUALIFIED;
}

/* Recognizes `!defined X` / `!defined ( X )`; returns the interned name. */
static const char *guard_not_defined_name(const PpToken *toks, u32 n)
{
    if (n >= 3 && toks[0].kind == PPTOK_PUNCT && toks[0].punct == PUNCT_BANG &&
        toks[1].kind == PPTOK_IDENT &&
        strcmp(toks[1].spelling, "defined") == 0) {
        if (n == 3 && toks[2].kind == PPTOK_IDENT)
            return toks[2].spelling;
        if (n == 5 && toks[2].kind == PPTOK_PUNCT &&
            toks[2].punct == PUNCT_LPAREN && toks[3].kind == PPTOK_IDENT &&
            toks[4].kind == PPTOK_PUNCT && toks[4].punct == PUNCT_RPAREN)
            return toks[3].spelling;
    }
    return NULL;
}

/* --- raw token flow ----------------------------------------------------- */

/* Next token from the CURRENT frame only; false at that frame's EOF. */
static bool frame_next(Preprocessor *pp, PpToken *out)
{
    return pp_lex_token(&cur_frame(pp)->lx, out);
}

/* --- rescan-stack token flow (expansion output above the lexer) --------- */

static void push_buf(Preprocessor *pp, PpToken *toks, u32 n)
{
    PpTokBuf *b;

    if (n == 0)
        return;
    if (pp->nbufs == pp->bufs_cap) {
        pp->bufs_cap = pp->bufs_cap ? pp->bufs_cap * 2 : 8;
        pp->bufs = cgf_xrealloc(pp->bufs, pp->bufs_cap * sizeof(PpTokBuf));
    }
    b = &pp->bufs[pp->nbufs++];
    b->toks = toks;
    b->n = n;
    b->pos = 0;
}

/* Pending slot > rescan stack > file lexer. False ONLY at the current
 * frame's EOF with everything else drained (the caller pops the frame). */
static bool raw_next(Preprocessor *pp, PpToken *out, bool *from_file)
{
    if (pp->has_pending) {
        *out = pp->pending;
        *from_file = pp->pending_from_file;
        pp->has_pending = false;
        pp_loc_mark(&pp->loc, out->loc);
        return true;
    }
    while (pp->nbufs) {
        PpTokBuf *b = &pp->bufs[pp->nbufs - 1];
        if (b->pos < b->n) {
            *out = b->toks[b->pos++];
            *from_file = false;
            if (b->pos == b->n)
                pp->nbufs--;
            pp_loc_mark(&pp->loc, out->loc);
            return true;
        }
        pp->nbufs--;
    }
    *from_file = true;
    if (!frame_next(pp, out))
        return false;
    pp_loc_mark(&pp->loc, out->loc);
    return true;
}

static void unget_raw(Preprocessor *pp, const PpToken *t, bool from_file)
{
    if (pp->has_pending)
        CGF_ICE("pp: double unget in raw stream");
    pp->pending = *t;
    pp->pending_from_file = from_file;
    pp->has_pending = true;
}

/* --- stream-side macro expansion --------------------------------------- */

VEC_DECL(PpStreamVec, PpToken);

/* Collects a function-like invocation's arguments from the RAW STREAM (the
 * `(` already consumed). Directives inside argument lists are UB
 * (6.10.3p11): conditionals error, others warn and are processed (gcc
 * parity). Returns false on unterminated list (diagnosed). */
static bool handle_directive(Preprocessor *pp, const PpToken *hash,
                             PpToken **passthrough, u32 *npassthrough);

static bool collect_args_stream(Preprocessor *pp, const MacroDef *m, SrcLoc inv,
                                MacroArg **out_args, u32 *out_nargs,
                                HideSet **rparen_hs)
{
    u32 max_args = m->nparams + (m->is_variadic ? 1u : 0u);
    MacroArg *args =
        arena_alloc(pp->arena, (max_args ? max_args : 1) * sizeof(MacroArg),
                    _Alignof(MacroArg));
    PpStreamVec cur = {NULL, 0, 0};
    u32 depth = 0, nargs = 0;

    memset(args, 0, (max_args ? max_args : 1) * sizeof(MacroArg));

    for (;;) {
        PpToken t;
        bool from_file;

        if (!raw_next(pp, &t, &from_file)) {
            pp_diag_at(pp, DIAG_ERROR, inv, 1,
                       "unterminated argument list invoking macro '%s'",
                       m->name);
            PpStreamVec_free(&cur);
            return false;
        }
        if (from_file && t.kind == PPTOK_PUNCT && t.punct == PUNCT_HASH &&
            (t.flags & PPTOK_F_BOL)) {
            /* Directive while collecting arguments: UB (6.10.3p11); gcc
             * warns, processes it, and honors conditionals — verified
             * against gcc on tinycc pp/22 (the sprint file predicted an
             * error for the #if family; gcc disagrees). */
            PpToken *pass;
            u32 npass;
            pp_warn_at(pp, WARN_PEDANTIC, t.loc, t.len,
                       "embedding a directive within macro arguments is "
                       "not portable");
            pp->collecting_args = true;
            handle_directive(pp, &t, &pass, &npass);
            pp->collecting_args = false;
            continue;
        }
        if (from_file && !in_live_region(pp))
            continue; /* skipped conditional group inside the arg list */
        if (t.kind == PPTOK_PUNCT && t.punct == PUNCT_LPAREN)
            depth++;
        if (t.kind == PPTOK_PUNCT && t.punct == PUNCT_RPAREN) {
            if (depth == 0) {
                if (rparen_hs)
                    *rparen_hs = t.hideset;
                break;
            }
            depth--;
        }
        if (t.kind == PPTOK_PUNCT && t.punct == PUNCT_COMMA && depth == 0 &&
            !(m->is_variadic && nargs >= m->nparams)) {
            if (nargs < max_args) {
                PpToken *copy = NULL;
                if (cur.len) {
                    copy = arena_alloc(pp->arena, cur.len * sizeof(PpToken),
                                       _Alignof(PpToken));
                    memcpy(copy, cur.data, cur.len * sizeof(PpToken));
                }
                args[nargs].raw = copy;
                args[nargs].nraw = (u32)cur.len;
            }
            nargs++;
            cur.len = 0;
            continue;
        }
        PpStreamVec_push(&cur, t);
    }

    if (nargs < max_args) {
        PpToken *copy = NULL;
        if (cur.len) {
            copy = arena_alloc(pp->arena, cur.len * sizeof(PpToken),
                               _Alignof(PpToken));
            memcpy(copy, cur.data, cur.len * sizeof(PpToken));
        }
        args[nargs].raw = copy;
        args[nargs].nraw = (u32)cur.len;
        nargs++;
    } else {
        nargs++;
    }

    if (nargs == 1 && args[0].nraw == 0 && m->nparams == 0 && !m->is_variadic)
        nargs = 0;
    if (m->is_variadic && nargs == m->nparams) {
        args[nargs].raw = NULL;
        args[nargs].nraw = 0;
        nargs++;
    }

    PpStreamVec_free(&cur);
    *out_args = args;
    *out_nargs = nargs;
    return true;
}

bool pp_try_expand(Preprocessor *pp, const PpToken *t)
{
    const MacroDef *m;

    if (pp_hs_contains(t->hideset, t->spelling))
        return false;
    m = pp_macro_lookup(pp, t->spelling);
    if (!m)
        return false;

    if (m->builtin_kind != MACRO_BUILTIN_NONE) {
        PpToken *one =
            arena_alloc(pp->arena, sizeof(PpToken), _Alignof(PpToken));
        *one = pp_builtin_token(pp, m, t->loc);
        one->flags |= t->flags & (PPTOK_F_SPACE | PPTOK_F_BOL);
        push_buf(pp, one, 1);
        return true;
    }

    if (!m->is_function) {
        HideSet *hs = pp_hs_insert(pp->arena, t->hideset, m->name);
        u32 sn;
        PpToken *sub = pp_macro_subst(pp, m, NULL, 0, hs, t->loc, &sn);
        if (sn)
            sub[0].flags =
                (sub[0].flags & (u8) ~(PPTOK_F_SPACE | PPTOK_F_BOL)) |
                (t->flags & (PPTOK_F_SPACE | PPTOK_F_BOL));
        push_buf(pp, sub, sn);
        return true;
    }

    /* Function-like: only an immediate `(` (whitespace/newlines allowed)
     * makes an invocation; a directive line or frame EOF blocks it. */
    {
        PpToken nx;
        bool from_file;

        if (!raw_next(pp, &nx, &from_file))
            return false; /* frame EOF: plain identifier */
        if (nx.kind != PPTOK_PUNCT || nx.punct != PUNCT_LPAREN ||
            (from_file && (nx.flags & PPTOK_F_BOL) && nx.punct == PUNCT_HASH)) {
            unget_raw(pp, &nx, from_file);
            return false;
        }
        {
            MacroArg *args;
            u32 nargs, sn;
            HideSet *rp_hs = NULL, *hs;
            PpToken *sub;

            if (!collect_args_stream(pp, m, t->loc, &args, &nargs, &rp_hs))
                return true; /* diagnosed; invocation swallowed */
            if (!pp_macro_check_args(pp, m, nargs, t->loc))
                return true;
            hs = pp_hs_intersect(pp->arena, t->hideset, rp_hs);
            hs = pp_hs_insert(pp->arena, hs, m->name);
            sub = pp_macro_subst(pp, m, args, nargs, hs, t->loc, &sn);
            if (sn)
                sub[0].flags =
                    (sub[0].flags & (u8) ~(PPTOK_F_SPACE | PPTOK_F_BOL)) |
                    (t->flags & (PPTOK_F_SPACE | PPTOK_F_BOL));
            push_buf(pp, sub, sn);
            return true;
        }
    }
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

/* --- #pragma once identity set ------------------------------------------ */

static bool once_seen(const Preprocessor *pp, u64 dev, u64 ino)
{
    size_t i;

    if (dev == 0 && ino == 0)
        return false; /* buffers have no identity */
    for (i = 0; i < pp->nonce; i++)
        if (pp->once[i].dev == dev && pp->once[i].ino == ino)
            return true;
    return false;
}

static void once_record(Preprocessor *pp, u64 dev, u64 ino)
{
    if ((dev == 0 && ino == 0) || once_seen(pp, dev, ino))
        return;
    if (pp->nonce == pp->once_cap) {
        size_t cap = pp->once_cap ? pp->once_cap * 2 : 16;
        void *grown =
            arena_alloc(pp->arena, cap * sizeof(pp->once[0]), _Alignof(u64));
        if (pp->nonce)
            memcpy(grown, pp->once, pp->nonce * sizeof(pp->once[0]));
        pp->once = grown;
        pp->once_cap = cap;
    }
    pp->once[pp->nonce].dev = dev;
    pp->once[pp->nonce].ino = ino;
    pp->nonce++;
}

/* --- include-guard fast-path cache -------------------------------------- */

/* Path-keyed shortcut: a repeat #include of the SAME path string skips
 * the fopen+fstat entirely. Identity (dev,ino) remains the authority for
 * correctness — this only avoids re-deriving it for a path we already
 * resolved this TU. (A path whose identity changes mid-compile would be
 * missed; gcc caches the same way. Findings row F18.) */
static const char *fcache_guard_by_path(const Preprocessor *pp,
                                        const char *path)
{
    size_t i;

    for (i = 0; i < pp->nfcache; i++)
        if (pp->fcache[i].path && strcmp(pp->fcache[i].path, path) == 0)
            return pp->fcache[i].guard_macro;
    return NULL;
}

static const char *fcache_guard(const Preprocessor *pp, u64 dev, u64 ino)
{
    size_t i;

    if (dev == 0 && ino == 0)
        return NULL;
    for (i = 0; i < pp->nfcache; i++)
        if (pp->fcache[i].dev == dev && pp->fcache[i].ino == ino)
            return pp->fcache[i].guard_macro;
    return NULL;
}

static void fcache_record(Preprocessor *pp, u64 dev, u64 ino, const char *guard,
                          const char *path)
{
    size_t i;

    if ((dev == 0 && ino == 0) || !guard)
        return;
    for (i = 0; i < pp->nfcache; i++)
        if (pp->fcache[i].dev == dev && pp->fcache[i].ino == ino)
            return;
    if (pp->nfcache == pp->fcache_cap) {
        size_t cap = pp->fcache_cap ? pp->fcache_cap * 2 : 32;
        void *grown =
            arena_alloc(pp->arena, cap * sizeof(pp->fcache[0]), _Alignof(u64));
        if (pp->nfcache)
            memcpy(grown, pp->fcache, pp->nfcache * sizeof(pp->fcache[0]));
        pp->fcache = grown;
        pp->fcache_cap = cap;
    }
    pp->fcache[pp->nfcache].dev = dev;
    pp->fcache[pp->nfcache].ino = ino;
    pp->fcache[pp->nfcache].guard_macro = guard;
    pp->fcache[pp->nfcache].path = path;
    pp->nfcache++;
}

/* --- #include resolution ------------------------------------------------ */

static const char *dir_of(Preprocessor *pp, const char *path)
{
    const char *slash = strrchr(path, '/');

    if (!slash)
        return ".";
    return arena_strndup(pp->arena, path, (size_t)(slash - path));
}

static PpDirIdentity dir_identity(const char *dir)
{
    struct stat st;
    PpDirIdentity id = {0};

    if (stat(dir, &st) == 0 && S_ISDIR(st.st_mode)) {
        id.dev = (u64)st.st_dev;
        id.ino = (u64)st.st_ino;
        id.valid = true;
    }
    return id;
}

static void source_dir_info(Preprocessor *pp, SourceFile *sf, const char **dir,
                            PpDirIdentity *identity)
{
    if (!sf->include_dir_ready) {
        sf->include_dir = dir_of(pp, sf->path);
        sf->include_dir_identity = dir_identity(sf->include_dir);
        sf->include_dir_ready = true;
    }
    *dir = sf->include_dir;
    *identity = sf->include_dir_identity;
}

static void prepare_configured_dir_identities(Preprocessor *pp)
{
    size_t i;

    for (i = 0; i < pp->n_iquote; i++)
        pp->iquote_dir_identities[i] = dir_identity(pp->iquote_dirs[i]);
    for (i = 0; i < pp->n_include; i++)
        pp->include_dir_identities[i] = dir_identity(pp->include_dirs[i]);
    for (i = 0; i < pp->n_system; i++)
        pp->system_dir_identities[i] = dir_identity(pp->system_dirs[i]);
}

/* Builds the search chain for one lookup. Chain entries are directory
 * identities, not just path spellings: command-line aliases of one directory
 * are searched once while retaining every option identity that names it. */
typedef struct {
    const char *dir;
    PpSearchKind kind;
    int index;
    int identity_index[PP_SEARCH_SYSTEM + 1];
    PpDirIdentity identity;
    bool is_system;
} PpSearchDir;

static void chain_append(PpSearchDir *chain, size_t *n, size_t max,
                         const char *dir, PpSearchKind kind, int index,
                         bool is_system, PpDirIdentity identity)
{
    size_t i;

    if (*n >= max)
        return;
    /* GCC searches a directory once even when the command line repeats it,
     * spells it through lexical/absolute/symlink aliases, or lists it in
     * multiple option classes. First occurrence fixes priority; all option
     * identities and system classification are retained. */
    for (i = 0; i < *n; i++) {
        bool same = identity.valid && chain[i].identity.valid
                        ? chain[i].identity.dev == identity.dev &&
                              chain[i].identity.ino == identity.ino
                        : strcmp(chain[i].dir, dir) == 0;

        if (same) {
            if (chain[i].identity_index[kind] < 0)
                chain[i].identity_index[kind] = index;
            /* The implicit source-adjacent entry wins quote lookup before
             * configured directories. An aliasing -isystem entry must not
             * retroactively turn that local include into a system header. */
            if (chain[i].kind != PP_SEARCH_INCLUDER)
                chain[i].is_system |= is_system;
            return;
        }
    }
    memset(&chain[*n], 0, sizeof(chain[*n]));
    chain[*n].dir = dir;
    chain[*n].kind = kind;
    chain[*n].index = index;
    chain[*n].identity = identity;
    chain[*n].is_system = is_system;
    for (i = 0; i < CGF_ARRAY_LEN(chain[*n].identity_index); i++)
        chain[*n].identity_index[i] = -1;
    chain[*n].identity_index[kind] = index;
    (*n)++;
}

static size_t build_chain(Preprocessor *pp, bool angled,
                          const char *includer_dir,
                          PpDirIdentity includer_identity, PpSearchDir *chain,
                          size_t max)
{
    size_t n = 0, i;

    if (!angled) {
        chain_append(chain, &n, max, includer_dir, PP_SEARCH_INCLUDER, 0, false,
                     includer_identity);
        for (i = 0; i < pp->n_iquote && n < max; i++)
            chain_append(chain, &n, max, pp->iquote_dirs[i], PP_SEARCH_IQUOTE,
                         (int)i, false, pp->iquote_dir_identities[i]);
    }
    for (i = 0; i < pp->n_include && n < max; i++)
        chain_append(chain, &n, max, pp->include_dirs[i], PP_SEARCH_INCLUDE,
                     (int)i, false, pp->include_dir_identities[i]);
    for (i = 0; i < pp->n_system && n < max; i++)
        chain_append(chain, &n, max, pp->system_dirs[i], PP_SEARCH_SYSTEM,
                     (int)i, true, pp->system_dir_identities[i]);
    return n;
}

static size_t include_next_start(const PpFrame *frame, const PpSearchDir *chain,
                                 size_t n)
{
    size_t i;

    /* PP-H-01: resume by stable search-entry identity. The caller rebuilds
     * the same effective chain that located the current frame. */
    if (!frame || frame->found_kind == PP_SEARCH_NONE)
        return 0;
    for (i = 0; i < n; i++)
        if (chain[i].identity_index[frame->found_kind] == frame->found_index)
            return i + 1;
    /* Configured identities and a frame's saved original chain are immutable
     * for the TU, and the fixed chain buffer holds every possible entry. A
     * miss therefore means internal state corruption; restarting at zero
     * would silently recreate the include_next search-origin bug. */
    CGF_ICE("pp: #include_next origin missing from reconstructed chain "
            "(kind=%d index=%d)",
            (int)frame->found_kind, frame->found_index);
}

static SourceFile *try_open(Preprocessor *pp, const char *dir, const char *name,
                            bool *once_skipped)
{
    size_t dlen = strlen(dir), nlen = strlen(name);
    char *path = arena_alloc(pp->arena, dlen + nlen + 2, 1);
    FILE *probe;
    struct stat st;

    if (strcmp(dir, ".") == 0) {
        /* A cwd includer: gcc spells the header bare ("h.h", never
         * "./h.h") — depfiles and diagnostics must match. */
        memcpy(path, name, nlen + 1);
    } else {
        memcpy(path, dir, dlen);
        path[dlen] = '/';
        memcpy(path + dlen + 1, name, nlen + 1);
    }
    if (pp->guard_fastpath) {
        const char *g = fcache_guard_by_path(pp, path);
        if (g && pp_macro_lookup(pp, g)) {
            pp->inc_guard_skipped++;
            *once_skipped = true;
            return NULL; /* guarded, still defined: no syscall at all */
        }
    }
    probe = fopen(path, "rb");
    if (!probe)
        return NULL;
    /* fopen succeeds on DIRECTORIES: an empty header name (from an
     * unterminated `#include <x`) would otherwise "open" the include dir
     * itself and spin. Only regular files are headers. (ppfuzz seed
     * 1123.) */
    {
        struct stat probe_st;
        if (fstat(fileno(probe), &probe_st) != 0 ||
            !S_ISREG(probe_st.st_mode)) {
            fclose(probe);
            return NULL;
        }
    }
    /* #pragma once identity is (st_dev, st_ino) of the OPEN file, never
     * the pathname: symlinked/hardlinked includes dedupe, same-named files
     * in different dirs do not. Known hole (gcc shares it; findings
     * table): bind mounts/overlayfs can show one file as two identities. */
    if (fstat(fileno(probe), &st) == 0) {
        u64 dev = (u64)st.st_dev, ino = (u64)st.st_ino;

        if (once_seen(pp, dev, ino)) {
            fclose(probe);
            pp->inc_once_skipped++;
            *once_skipped = true;
            return NULL;
        }
        if (pp->guard_fastpath) {
            /* Guarded and the guard macro is STILL defined: the file would
             * produce nothing. Skip without reading or tokenizing. The
             * re-check (not a "seen" boolean) is what makes #undef GUARD
             * between inclusions correctly re-include. */
            const char *g = fcache_guard(pp, dev, ino);
            if (g && pp_macro_lookup(pp, g)) {
                fclose(probe);
                pp->inc_guard_skipped++;
                *once_skipped = true;
                return NULL;
            }
        }
    }
    fclose(probe);
    pp->inc_opened++;
    return pp_source_load(pp, path);
}

static void do_include(Preprocessor *pp, const char *name, bool angled,
                       bool is_next, SrcLoc loc)
{
    PpSearchDir chain[2 + 3 * PP_MAX_DIRS];
    size_t n, start = 0, i;
    SourceFile *sf = NULL;
    int found = -1;
    bool once_skipped = false;
    bool search_angled = angled;
    const char *search_includer_dir = NULL;
    PpDirIdentity search_includer_identity = {0};

    if (!angled)
        source_dir_info(pp, cur_frame(pp)->lx.sf, &search_includer_dir,
                        &search_includer_identity);

    if (pp->nframes >= PP_INCLUDE_DEPTH_MAX) {
        pp_diag_at(pp, DIAG_FATAL, loc, 1,
                   "#include nested depth %d exceeds maximum",
                   PP_INCLUDE_DEPTH_MAX);
        pp->fatal = true;
        return;
    }

    if (is_next) {
        /* GNU #include_next: resume AFTER the dir the current file was
         * found in (glibc's own headers require this). Pedwarn hook is
         * Sprint 37. A frame with no search origin starts at the front. */
        if (cur_frame(pp)->found_kind != PP_SEARCH_NONE) {
            search_angled = cur_frame(pp)->search_angled;
            search_includer_dir = cur_frame(pp)->search_includer_dir;
            search_includer_identity = cur_frame(pp)->search_includer_identity;
        }
    }
    n = build_chain(pp, search_angled, search_includer_dir,
                    search_includer_identity, chain, CGF_ARRAY_LEN(chain));
    if (is_next) {
        start = include_next_start(cur_frame(pp), chain, n);
    }

    for (i = start; i < n; i++) {
        sf = try_open(pp, chain[i].dir, name, &once_skipped);
        if (sf || once_skipped) {
            found = (int)i;
            break;
        }
    }
    /* Absolute paths bypass the chain. */
    if (!sf && !once_skipped && name[0] == '/') {
        struct stat st;
        FILE *probe = fopen(name, "rb");
        if (probe) {
            if (fstat(fileno(probe), &st) == 0 &&
                once_seen(pp, (u64)st.st_dev, (u64)st.st_ino))
                once_skipped = true;
            fclose(probe);
            if (!once_skipped)
                sf = pp_source_load(pp, name);
            found = -1;
        }
    }
    if (once_skipped)
        return; /* #pragma once: silently not re-entered */

    if (pp->verbose) {
        fprintf(stderr, "#include %c%s%c search starts here:\n",
                angled ? '<' : '"', name, angled ? '>' : '"');
        for (i = start; i < n; i++)
            fprintf(stderr, " %s\n", chain[i].dir);
        fprintf(stderr, "End of search list.\n");
    }

    if (!sf) {
        pp_diag_at(pp, DIAG_FATAL, loc, 1, "%s: No such file or directory",
                   name);
        pp->fatal = true; /* include errors are fatal (gcc parity) */
        return;
    }
    /* System classification: resolved from a system dir, or included FROM
     * a system header (transitivity is what makes -MM omit a system
     * header's own quote-form includes). Absolute paths (found -1) only
     * inherit. */
    sf->is_system =
        (found >= 0 && chain[found].is_system) || pp_loc_is_system(pp, loc);
    push_frame(pp, sf, found >= 0 ? chain[found].kind : PP_SEARCH_NONE,
               found >= 0 ? chain[found].index : -1, search_angled,
               search_includer_dir, search_includer_identity);
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

        if (h.len < 3) { /* <> or unterminated: no name between the
                            delimiters (len - 2 would also underflow) */
            pp_diag_at(pp, DIAG_ERROR, h.loc, h.len,
                       "empty filename in "
                       "#include");
            return;
        }
        if (nrest != 0)
            pp_warn_at(pp, WARN_CPP, rest[0].loc, rest[0].len,
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
        {
            PpToken *ex;
            n = pp_expand_list(pp, toks, n, &ex);
            toks = ex;
        }
        if (n == 0) {
            pp_diag_at(pp, DIAG_ERROR, dloc, 1,
                       "#include expects <FILENAME> or \"FILENAME\"");
            return;
        }
        if (toks[0].kind == PPTOK_STRLIT && toks[0].len >= 2) {
            if (n > 1)
                pp_warn_at(pp, WARN_CPP, toks[1].loc, toks[1].len,
                           "extra tokens at end of #include directive");
            {
                char *name = arena_strndup(pp->arena, toks[0].spelling + 1,
                                           toks[0].len - 2);
                do_include(pp, name, false, is_next, dloc);
            }
            return;
        }
        /* < h-chars > assembled from several expanded tokens: rebuild the
         * header name from their spellings, preserving single spaces where
         * PPTOK_F_SPACE says there was whitespace (6.10.2p4). */
        if (toks[0].kind == PPTOK_PUNCT && toks[0].punct == PUNCT_LT) {
            Buf b;
            u32 k;
            bool closed = false;

            buf_init(&b);
            for (k = 1; k < n; k++) {
                if (toks[k].kind == PPTOK_PUNCT && toks[k].punct == PUNCT_GT) {
                    closed = true;
                    if (k + 1 < n)
                        pp_warn_at(pp, WARN_CPP, toks[k + 1].loc,
                                   toks[k + 1].len,
                                   "extra tokens at end of #include "
                                   "directive");
                    break;
                }
                if (b.len && (toks[k].flags & PPTOK_F_SPACE))
                    buf_push_u8(&b, ' ');
                buf_append(&b, toks[k].spelling, toks[k].len);
            }
            if (closed && b.len) {
                char *name =
                    arena_strndup(pp->arena, (const char *)b.data, b.len);
                buf_free(&b);
                do_include(pp, name, true, is_next, dloc);
                return;
            }
            buf_free(&b);
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
    {
        PpToken *ex;
        n = pp_expand_list(pp, toks, n, &ex);
        toks = ex;
    }
    if (n == 0) {
        pp_diag_at(pp, DIAG_ERROR, dloc, 1, "#line expects a line number");
        return;
    }
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
        if (toks[1].kind != PPTOK_STRLIT || toks[1].len < 2) {
            /* len < 2 means an UNTERMINATED literal (just the opening
             * quote, already diagnosed by the lexer): len - 2 would
             * underflow to ~4G. Found by ppfuzz seed 957. */
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

static const char *pragma_warning_option(Preprocessor *pp, const PpToken *t)
{
    const char *s;
    u32 n;

    if (t->kind != PPTOK_STRLIT)
        return NULL;
    s = t->spelling;
    n = t->len;
    if (n < 4 || s[0] != '"' || s[n - 1] != '"' || s[1] != '-' || s[2] != 'W')
        return NULL;
    return arena_strndup(pp->arena, s + 1, n - 2);
}

static u32 pragma_seq(Preprocessor *pp, SrcLoc anchor)
{
    return pp_span(pp, anchor, 1).seq;
}

static void pragma_system_header(Preprocessor *pp, SrcLoc anchor)
{
    FileId f = 0;
    u32 line = 0;

    pp_loc_resolve(&pp->loc, anchor, &f, &line, NULL);
    if (f && (size_t)f <= pp->nfiles) {
        SourceFile *sf = pp->files[f - 1];
        u32 from = line + 1;

        if (sf == pp->main_file) {
            pp_warn_at(
                pp, WARN_PRAGMAS, anchor, 1,
                "#pragma GCC system_header ignored outside include file");
            return;
        }
        if (!sf->system_from_line || from < sf->system_from_line)
            sf->system_from_line = from;
    }
}

static void pragma_diagnostic(Preprocessor *pp, PpToken *toks, u32 n,
                              SrcLoc anchor)
{
    const char *action;
    u32 seq = pragma_seq(pp, anchor);

    if (!pp->warn)
        return;

    if (n < 3 || toks[2].kind != PPTOK_IDENT) {
        pp_warn_at(pp, WARN_PRAGMAS, anchor, 1,
                   "missing [error|warning|ignored|push|pop] after "
                   "#pragma GCC diagnostic");
        return;
    }
    action = toks[2].spelling;
    if (strcmp(action, "push") == 0 || strcmp(action, "pop") == 0) {
        if (n != 3) {
            pp_warn_at(pp, WARN_PRAGMAS, toks[3].loc, toks[3].len,
                       "junk at end of #pragma GCC diagnostic %s", action);
            return;
        }
        if (strcmp(action, "push") == 0)
            warn_pragma_push(pp->warn, seq);
        else
            (void)warn_pragma_pop(pp->warn, seq,
                                  pp_span(pp, toks[2].loc, toks[2].len));
        return;
    }
    if (strcmp(action, "ignored") == 0 || strcmp(action, "warning") == 0 ||
        strcmp(action, "error") == 0) {
        const char *opt;
        WarnPragmaClass cls = WARN_PRAGMA_IGNORED;

        if (n != 4 || !(opt = pragma_warning_option(pp, &toks[3]))) {
            SrcLoc loc = n >= 4 ? toks[3].loc : toks[2].loc;
            u32 len = n >= 4 ? toks[3].len : toks[2].len;
            pp_warn_at(pp, WARN_PRAGMAS, loc, len,
                       "missing warning option after #pragma GCC diagnostic %s",
                       action);
            return;
        }
        if (strcmp(action, "warning") == 0)
            cls = WARN_PRAGMA_WARNING;
        else if (strcmp(action, "error") == 0)
            cls = WARN_PRAGMA_ERROR;
        {
            WarnId id = warn_pragma_option_id(opt);

            /* GCC diagnostic pragmas accept a canonical positive option,
             * never command-line conveniences such as -Wno-foo or
             * parameterized -Wfoo=N spellings. Those must diagnose and
             * leave the current classification untouched. */
            if (id == WARN_NONE) {
                pp_warn_at(
                    pp, WARN_PRAGMAS, toks[3].loc, toks[3].len,
                    "unknown warning option '%s' after #pragma GCC diagnostic",
                    opt);
                return;
            }
            warn_pragma_set(pp->warn, seq, id, cls);
        }
        return;
    }
    pp_warn_at(pp, WARN_PRAGMAS, toks[2].loc, toks[2].len,
               "expected [error|warning|ignored|push|pop] after "
               "#pragma GCC diagnostic");
}

/* Returns true if the pragma line should pass through to -E output. */
static bool directive_pragma(Preprocessor *pp, PpToken *toks, u32 n,
                             SrcLoc anchor)
{
    if (n >= 1 && toks[0].kind == PPTOK_IDENT &&
        strcmp(toks[0].spelling, "once") == 0) {
        /* Record the CURRENT file's identity; later includes of the same
         * (dev,ino) are skipped before entry. In the main file: legal,
         * pointless, works. Consumed (gcc -E emits nothing for it). */
        const SourceFile *sf = cur_frame(pp)->lx.sf;
        once_record(pp, sf->st_dev, sf->st_ino);
        if (n > 1)
            pp_warn_at(pp, WARN_PRAGMAS, toks[1].loc, toks[1].len,
                       "extra tokens at end of #pragma once");
        return false;
    }
    if (n >= 2 && toks[0].kind == PPTOK_IDENT &&
        strcmp(toks[0].spelling, "GCC") == 0 && toks[1].kind == PPTOK_IDENT) {
        if (strcmp(toks[1].spelling, "diagnostic") == 0) {
            pragma_diagnostic(pp, toks, n, anchor);
            return true;
        }
        if (strcmp(toks[1].spelling, "system_header") == 0) {
            if (n != 2)
                pp_warn_at(pp, WARN_PRAGMAS, toks[2].loc, toks[2].len,
                           "junk at end of #pragma GCC system_header");
            else
                pragma_system_header(pp, anchor);
            return false;
        }
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
    if (n)
        pp_warn_at(pp, WARN_UNKNOWN_PRAGMAS, toks[0].loc, toks[0].len,
                   "ignoring unknown pragma '%s'", toks[0].spelling);
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
    DK_IDENT,
    DK_SCCS,
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
    if (strcmp(s, "ident") == 0)
        return DK_IDENT;
    if (strcmp(s, "sccs") == 0)
        return DK_SCCS;
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
        if (f->guard_state == GUARD_EXPECT_IFNDEF) {
            /* gcc recognizes `#if !defined X` as a guard too. */
            const char *g = guard_not_defined_name(toks, n);
            if (g) {
                f->guard_state = GUARD_EXPECT_DEFINE;
                f->guard_macro = g;
                f->guard_cond = pp->nconds;
            } else {
                f->guard_state = GUARD_DISQUALIFIED;
            }
        } else if (f->guard_state == GUARD_EXPECT_DEFINE ||
                   f->guard_state == GUARD_AFTER_ENDIF) {
            f->guard_state = GUARD_DISQUALIFIED;
        }
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
        if (f->guard_state == GUARD_EXPECT_IFNDEF) {
            if (k == DK_IFNDEF && n == 1 && toks[0].kind == PPTOK_IDENT) {
                f->guard_state = GUARD_EXPECT_DEFINE;
                f->guard_macro = toks[0].spelling;
                f->guard_cond = pp->nconds;
            } else {
                f->guard_state = GUARD_DISQUALIFIED;
            }
        } else if (f->guard_state == GUARD_EXPECT_DEFINE ||
                   f->guard_state == GUARD_AFTER_ENDIF) {
            f->guard_state = GUARD_DISQUALIFIED;
        }
        if (n == 0 || toks[0].kind != PPTOK_IDENT) {
            if (live)
                pp_diag_at(pp, DIAG_ERROR, name.loc, name.len,
                           "#%s expects an identifier", name.spelling);
        } else if (live) {
            bool def = pp_name_is_defined(pp, toks[0].spelling);
            taken = (k == DK_IFDEF) ? def : !def;
            if (n > 1)
                pp_warn_at(pp, WARN_CPP, toks[1].loc, toks[1].len,
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
        if (f->guard_macro && pp->nconds - 1 == f->guard_cond)
            f->guard_state = GUARD_DISQUALIFIED; /* on the guard */
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
        if (f->guard_macro && pp->nconds - 1 == f->guard_cond)
            f->guard_state = GUARD_DISQUALIFIED; /* on the guard */
        if (c->seen_else) {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len, "#else after #else");
            pp_diag_at(pp, DIAG_NOTE, c->loc, 1, "conditional started here");
            return false;
        }
        c->seen_else = true;
        c->live = c->parent_live && !c->taken_any;
        c->taken_any = true;
        if (n != 0 && c->parent_live)
            pp_warn_at(pp, WARN_CPP, toks[0].loc, toks[0].len,
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
        if (f->guard_state == GUARD_IN_BODY && pp->nconds == f->guard_cond)
            f->guard_state = GUARD_AFTER_ENDIF;
        if (n != 0 && in_live_region(pp))
            pp_warn_at(pp, WARN_CPP, toks[0].loc, toks[0].len,
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
        if (f->guard_state == GUARD_EXPECT_DEFINE) {
            f->guard_state = (n >= 1 && toks[0].kind == PPTOK_IDENT &&
                              toks[0].spelling == f->guard_macro)
                                 ? GUARD_IN_BODY
                                 : GUARD_DISQUALIFIED;
        } else if (f->guard_state == GUARD_AFTER_ENDIF) {
            f->guard_state = GUARD_DISQUALIFIED;
        }
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
            pp_warn_at(pp, WARN_CPP, toks[1].loc, toks[1].len,
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
        if (k == DK_ERROR)
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len, "#%s%s%s",
                       name.spelling, n ? " " : "",
                       n ? spell_line(pp, toks, n) : "");
        else
            pp_warn_at(pp, WARN_CPP, name.loc, name.len, "#%s%s%s",
                       name.spelling, n ? " " : "",
                       n ? spell_line(pp, toks, n) : "");
        return false;
    }
    case DK_PRAGMA: {
        PpToken *toks;
        u32 n = read_line(pp, &toks);
        if (directive_pragma(pp, toks, n, name.loc)) {
            /* Pass the whole line through to -E: rebuild `# pragma ...`
             * tokens for the output stream. */
            PpToken *out = arena_alloc(pp->arena, (n + 2) * sizeof(PpToken),
                                       _Alignof(PpToken));
            out[0] = *hash;
            out[1] = name;
            out[1].flags &= (u8)~PPTOK_F_SPACE; /* "#pragma" (gcc) */
            if (n)
                memcpy(out + 2, toks, n * sizeof(PpToken));
            *passthrough = out;
            *npassthrough = n + 2;
            return true;
        }
        return false;
    }
    case DK_IDENT:
    case DK_SCCS: {
        PpToken *toks;
        PpToken *expanded;
        PpToken *out;
        u32 n = read_line(pp, &toks);

        n = pp_expand_list(pp, toks, n, &expanded);
        pp_pedwarn_at(pp, WARN_PEDANTIC, name.loc, name.len,
                      "'#%s' is a GCC extension", name.spelling);
        /* GCC accepts one ordinary string literal. Encoding-prefixed strings
         * are a different directive shape, not an alternate object encoding. */
        if (n == 0 || expanded[0].kind != PPTOK_STRLIT ||
            expanded[0].len == 0 || expanded[0].spelling[0] != '"') {
            pp_diag_at(pp, DIAG_ERROR, name.loc, name.len,
                       "invalid #%s directive", name.spelling);
            return false;
        }
        if (n > 1)
            pp_warn_at(pp, WARN_CPP, expanded[1].loc, expanded[1].len,
                       "extra tokens at end of #%s directive", name.spelling);
        if (pp->nidents == pp->cap_idents) {
            u32 cap = pp->cap_idents ? pp->cap_idents * 2 : 4;
            PpToken *grown = arena_alloc(pp->arena, cap * sizeof(PpToken),
                                         _Alignof(PpToken));

            if (pp->nidents)
                memcpy(grown, pp->idents, pp->nidents * sizeof(PpToken));
            pp->idents = grown;
            pp->cap_idents = cap;
        }
        pp->idents[pp->nidents++] = expanded[0];

        /* GCC canonicalizes deprecated #sccs to #ident under -E and prints
         * only the accepted first argument when trailing tokens were warned. */
        out = arena_alloc(pp->arena, 3 * sizeof(PpToken), _Alignof(PpToken));
        out[0] = *hash;
        out[1] = name;
        out[1].spelling =
            intern_str(pp->interner, intern(pp->interner, "ident", 5));
        out[1].len = 5;
        out[1].flags &= (u8)~PPTOK_F_SPACE;
        out[2] = expanded[0];
        out[2].flags &= (u8)~PPTOK_F_BOL;
        out[2].flags |= PPTOK_F_SPACE;
        *passthrough = out;
        *npassthrough = 3;
        return true;
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

/* The _Pragma operator (6.10.9): a preprocessing OPERATOR, legal anywhere
 * a pp-token is — crucially inside macro replacement lists (the reason it
 * exists: #pragma cannot be produced by a macro). Destringize the operand
 * (\\" -> \", \\\\ -> \\, exactly those two per ISO), re-lex, process as a
 * #pragma line at the operator's location. Under -E a surviving pragma
 * re-emits as a `# pragma ...` line (gcc parity). */
static void pragma_operator(Preprocessor *pp, const PpToken *op)
{
    PpToken t;
    bool ff;
    const char *body;
    u32 blen;

    if (!raw_next(pp, &t, &ff) || t.kind != PPTOK_PUNCT ||
        t.punct != PUNCT_LPAREN) {
        pp_diag_at(pp, DIAG_ERROR, op->loc, op->len,
                   "_Pragma takes a parenthesized string literal");
        return;
    }
    if (!raw_next(pp, &t, &ff) || t.kind != PPTOK_STRLIT) {
        pp_diag_at(pp, DIAG_ERROR, op->loc, op->len,
                   "_Pragma takes a parenthesized string literal");
        return;
    }
    {
        /* Destringize: strip encoding prefix + quotes, undo \" and \\. */
        const char *sp = t.spelling;
        u32 len = t.len, i = 0;
        Buf b;

        while (i < len && sp[i] != '"')
            i++; /* skip L / u / U / u8 prefix */
        i++;     /* opening quote */
        buf_init(&b);
        while (i + 1 < len) { /* stop before the closing quote */
            if (sp[i] == '\\' && i + 2 < len &&
                (sp[i + 1] == '"' || sp[i + 1] == '\\')) {
                buf_push_u8(&b, (u8)sp[i + 1]);
                i += 2;
            } else {
                buf_push_u8(&b, (u8)sp[i]);
                i++;
            }
        }
        body = arena_strndup(pp->arena, (const char *)b.data, b.len);
        blen = (u32)b.len;
        buf_free(&b);
    }
    if (!raw_next(pp, &t, &ff) || t.kind != PPTOK_PUNCT ||
        t.punct != PUNCT_RPAREN) {
        pp_diag_at(pp, DIAG_ERROR, op->loc, op->len,
                   "_Pragma takes a parenthesized string literal");
        return;
    }

    {
        /* Re-lex the destringized text and process like a #pragma line.
         * An empty operand is a null pragma: no-op, no error (gcc). */
        SourceFile *sf = pp_source_add_buffer(pp, "<_Pragma>", body, blen);
        PpLexer lx;
        PpToken toks[64];
        u32 n = 0;

        pp_lexer_init(&lx, pp, sf);
        while (n < 64 && pp_lex_token(&lx, &toks[n]))
            n++;
        buf_free(&lx.scratch);

        if (directive_pragma(pp, toks, n, op->loc) && pp->emit_pragmas) {
            /* Re-emit as a directive line for -E. */
            PpToken *out = arena_alloc(pp->arena, (n + 2) * sizeof(PpToken),
                                       _Alignof(PpToken));
            u32 k;
            PpToken hash;
            memset(&hash, 0, sizeof(hash));
            hash.kind = PPTOK_PUNCT;
            hash.punct = PUNCT_HASH;
            hash.spelling =
                intern_str(pp->interner, intern(pp->interner, "#", 1));
            hash.len = 1;
            hash.loc = op->loc;
            hash.flags = PPTOK_F_BOL;
            out[0] = hash;
            memset(&out[1], 0, sizeof(PpToken));
            out[1].kind = PPTOK_IDENT;
            out[1].spelling =
                intern_str(pp->interner, intern(pp->interner, "pragma", 6));
            out[1].len = 6;
            out[1].loc = op->loc;
            out[1].flags = 0; /* gcc prints "#pragma", not "# pragma" */
            for (k = 0; k < n; k++) {
                out[2 + k] = toks[k];
                out[2 + k].flags &= (u8)~PPTOK_F_BOL;
                if (k == 0) /* separate from "pragma"; rest keep their own */
                    out[2 + k].flags |= PPTOK_F_SPACE;
            }
            /* Ensure the NEXT text token starts a fresh line under -E. */
            pp->pass = out;
            pp->npass = n + 2;
            pp->pass_pos = 0;
        }
    }
}

void pp_begin(Preprocessor *pp, SourceFile *main_file, SourceFile *cmdline)
{
    SourceFile *builtin;
    PpDirIdentity no_identity = {0};

    pp->main_file = main_file;
    prepare_configured_dir_identities(pp);
    push_frame(pp, main_file, PP_SEARCH_NONE, -1, false, NULL, no_identity);
    if (cmdline)
        push_frame(pp, cmdline, PP_SEARCH_NONE, -1, false, NULL, no_identity);
    /* Topmost = processed first: builtins, then -D/-U, then the TU. */
    builtin = pp_predefine_all(pp);
    push_frame(pp, builtin, PP_SEARCH_NONE, -1, false, NULL, no_identity);
}

void pp_end(Preprocessor *pp)
{
    while (pp->nframes)
        pop_frame(pp);
    if (pp->stats) {
        printf("includes: %u opened, %u guard-skipped, %u once-skipped\n",
               (unsigned)pp->inc_opened, (unsigned)pp->inc_guard_skipped,
               (unsigned)pp->inc_once_skipped);
        pp->stats = false; /* pp_end may be called twice (fatal path) */
    }
    free(pp->bufs);
    pp->bufs = NULL;
    pp->nbufs = 0;
    pp->bufs_cap = 0;
}

bool pp_next(Preprocessor *pp, PpToken *out)
{
    for (;;) {
        PpToken t;
        bool from_file;

        if (pp->nframes == 0)
            return false;
        if (pp->fatal) {
            pp_end(pp);
            return false;
        }

        /* Pending passthrough tokens (#pragma under -E)? */
        if (pp->emit_pragmas && pp->npass) {
            *out = pp->pass[pp->pass_pos++];
            if (pp->pass_pos == pp->npass) {
                pp->npass = 0;
                pp->pass_pos = 0;
            }
            pp->tokens_emitted++;
            return true;
        }

        if (!raw_next(pp, &t, &from_file)) {
            pop_frame(pp);
            continue;
        }

        /* Directives are recognized only in tokens straight from the file
         * lexer — expansion output is NEVER reinterpreted as directives. */
        if (from_file && t.kind == PPTOK_PUNCT && t.punct == PUNCT_HASH &&
            (t.flags & PPTOK_F_BOL)) {
            PpToken *pass;
            u32 npass;
            if (handle_directive(pp, &t, &pass, &npass) && npass &&
                pp->emit_pragmas) {
                pp->pass = pass;
                pp->npass = npass;
                pp->pass_pos = 0;
            }
            continue;
        }

        if (from_file)
            guard_saw_text(cur_frame(pp));
        if (!in_live_region(pp))
            continue; /* skipped group: discard text tokens */

        if (t.kind == PPTOK_IDENT) {
            if (strcmp(t.spelling, "_Pragma") == 0) {
                pragma_operator(pp, &t);
                continue;
            }
            if (pp_try_expand(pp, &t))
                continue;
        }
        *out = t;
        pp->tokens_emitted++;
        return true;
    }
}
