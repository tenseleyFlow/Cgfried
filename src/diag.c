#include "diag.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "util/arena.h"

typedef struct {
    const char *path; /* arena copy */
    const char *src;  /* borrowed */
    size_t len;
} DiagFile;

struct DiagCtx {
    Arena *arena;
    DiagFile *files;
    size_t files_len;
    size_t files_cap;
    DiagSink sink;
    u32 error_count;
    u32 warning_count;
    u32 max_errors;       /* 0 = unlimited (gcc's default) */
    u32 suppressed_count; /* diagnostics deliberately not emitted */
    bool limit_reached;
};

static const char *ice_input; /* set once by the driver; read only by cgf_ice */

DiagCtx *diag_ctx_new(Arena *arena)
{
    DiagCtx *dc = arena_alloc(arena, sizeof(DiagCtx), _Alignof(DiagCtx));

    memset(dc, 0, sizeof(*dc));
    dc->arena = arena;
    dc->sink.handle = diag_render_stderr;
    dc->sink.user = NULL;
    return dc;
}

void diag_set_sink(DiagCtx *dc, DiagSink sink)
{
    dc->sink = sink;
}

DiagSink diag_swap_sink(DiagCtx *dc, DiagSink sink)
{
    DiagSink prev = dc->sink;

    dc->sink = sink;
    return prev;
}

u32 diag_add_file(DiagCtx *dc, const char *path, const char *src, size_t len)
{
    if (dc->files_len == dc->files_cap) {
        size_t cap = dc->files_cap ? dc->files_cap * 2 : 8;
        DiagFile *grown =
            arena_alloc(dc->arena, cap * sizeof(DiagFile), _Alignof(DiagFile));
        if (dc->files_len) /* memcpy from NULL is UB even for 0 */
            memcpy(grown, dc->files, dc->files_len * sizeof(DiagFile));
        dc->files = grown;
        dc->files_cap = cap;
    }
    dc->files[dc->files_len].path = arena_strdup(dc->arena, path);
    dc->files[dc->files_len].src = src;
    dc->files[dc->files_len].len = len;
    dc->files_len++;
    return (u32)dc->files_len; /* ids are 1-based; 0 = no location */
}

/* The single emission path. `fix_where`/`insert` may be a zero Span and
 * NULL for the (usual) no-fix-it case. */
static void emit_v(DiagCtx *dc, DiagLevel lvl, Span sp, Span fix_where,
                   const char *insert, const char *fmt, va_list ap)
{
    va_list ap2;
    int need;
    char *msg;
    Diag d;

    /* Once the cap has latched, everything after it is noise: the whole
     * point of the cap is to bound output volume. */
    if (dc->limit_reached)
        return;

    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    if (need < 0)
        CGF_ICE("diag_emit: vsnprintf failed on format \"%s\"", fmt);
    msg = arena_alloc(dc->arena, (size_t)need + 1, 1);
    vsnprintf(msg, (size_t)need + 1, fmt, ap2);
    va_end(ap2);

    if (lvl == DIAG_ERROR || lvl == DIAG_FATAL)
        dc->error_count++;
    else if (lvl == DIAG_WARNING)
        dc->warning_count++;

    memset(&d, 0, sizeof(d));
    d.level = lvl;
    d.span = sp;
    d.message = msg;
    d.fixit.where = fix_where;
    d.fixit.insert = insert;
    dc->sink.handle(dc->sink.user, &d, dc);

    /* The cap message is itself a diagnostic, so latch AFTER emitting the
     * one that hit the limit and emit the notice through the same sink —
     * a capturing sink in a test sees it exactly as stderr would. */
    if (dc->max_errors && dc->error_count >= dc->max_errors &&
        (lvl == DIAG_ERROR || lvl == DIAG_FATAL)) {
        Diag stop;

        memset(&stop, 0, sizeof(stop));
        stop.level = DIAG_FATAL;
        stop.message = "too many errors, stopping";
        dc->sink.handle(dc->sink.user, &stop, dc);
        dc->limit_reached = true;
    }
}

void diag_emit(DiagCtx *dc, DiagLevel lvl, Span sp, const char *fmt, ...)
{
    va_list ap;
    Span nofix = {0};

    va_start(ap, fmt);
    emit_v(dc, lvl, sp, nofix, NULL, fmt, ap);
    va_end(ap);
}

void diag_emit_fixit(DiagCtx *dc, DiagLevel lvl, Span sp, Span fix_where,
                     const char *insert, const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit_v(dc, lvl, sp, fix_where, insert, fmt, ap);
    va_end(ap);
}

void diag_set_max_errors(DiagCtx *dc, u32 max_errors)
{
    dc->max_errors = max_errors;
}

bool diag_error_limit_reached(const DiagCtx *dc)
{
    return dc->limit_reached;
}

u32 diag_suppressed_count(const DiagCtx *dc)
{
    return dc->suppressed_count;
}

void diag_note_suppressed(DiagCtx *dc)
{
    dc->suppressed_count++;
}

bool diag_had_error(const DiagCtx *dc)
{
    return dc->error_count > 0;
}

u32 diag_error_count(const DiagCtx *dc)
{
    return dc->error_count;
}

bool diag_use_color(void)
{
    static int cached = -1;

    if (cached < 0) {
        const char *no = getenv("NO_COLOR");
        const char *force = getenv("CLICOLOR_FORCE");

        if (no && no[0] != '\0')
            cached = 0; /* any non-empty value disables (no-color.org) */
        else if (force && force[0] != '\0')
            cached = 1;
        else
            cached = isatty(STDERR_FILENO) ? 1 : 0;
    }
    return cached == 1;
}

static const char *level_str(DiagLevel lvl)
{
    switch (lvl) {
    case DIAG_NOTE:
        return "note";
    case DIAG_WARNING:
        return "warning";
    case DIAG_ERROR:
        return "error";
    case DIAG_FATAL:
        return "fatal error";
    }
    CGF_ICE("level_str: bad level %d", (int)lvl);
}

static const char *level_color(DiagLevel lvl)
{
    switch (lvl) {
    case DIAG_NOTE:
        return "\033[1;36m"; /* bold cyan, matched to clang's defaults */
    case DIAG_WARNING:
        return "\033[1;35m"; /* bold magenta */
    case DIAG_ERROR:
    case DIAG_FATAL:
        return "\033[1;31m"; /* bold red */
    }
    CGF_ICE("level_color: bad level %d", (int)lvl);
}

/* Locates 1-based line `line` of file `file_id`; false if out of range. */
static bool file_line(const DiagCtx *dc, u32 file_id, u32 line,
                      const char **out, size_t *out_len)
{
    const DiagFile *f;
    const char *p, *end, *start;
    u32 cur = 1;

    if (file_id == 0 || file_id > dc->files_len)
        return false;
    f = &dc->files[file_id - 1];
    p = f->src;
    end = f->src + f->len;
    while (cur < line) {
        while (p < end && *p != '\n')
            p++;
        if (p == end)
            return false;
        p++;
        cur++;
    }
    start = p;
    while (p < end && *p != '\n')
        p++;
    *out = start;
    *out_len = (size_t)(p - start);
    return true;
}

void diag_render(FILE *f, const Diag *d, const DiagCtx *dc, bool color)
{
    const char *bold = color ? "\033[1m" : "";
    const char *lvl_col = color ? level_color(d->level) : "";
    const char *reset = color ? "\033[0m" : "";

    if (d->span.file_id == 0) {
        fprintf(f, "%scgfried:%s %s%s:%s %s\n", bold, reset, lvl_col,
                level_str(d->level), reset, d->message);
        return;
    }

    /* #line presumed path/line override the HEADER; the caret snippet
     * below always comes from the physical location. */
    fprintf(f, "%s%s:%u:%u:%s %s%s:%s %s\n", bold,
            d->span.presumed_path ? d->span.presumed_path
                                  : dc->files[d->span.file_id - 1].path,
            (unsigned)(d->span.presumed_line ? d->span.presumed_line
                                             : d->span.line),
            (unsigned)d->span.col, reset, lvl_col, level_str(d->level), reset,
            d->message);

    {
        const char *src_line;
        size_t line_len;

        if (!file_line(dc, d->span.file_id, d->span.line, &src_line, &line_len))
            return; /* span beyond the buffer: header already printed */

        fwrite(src_line, 1, line_len, f);
        fputc('\n', f);

        /* Caret line: mirror each pre-caret tab with a tab so the caret
         * aligns at any tab-width; '^' at col, '~' continuation for len. */
        {
            size_t i;
            size_t caret_col = d->span.col ? d->span.col : 1;
            size_t n_tilde;

            for (i = 0; i + 1 < caret_col && i < line_len; i++)
                fputc(src_line[i] == '\t' ? '\t' : ' ', f);
            fprintf(f, "%s^", lvl_col);
            n_tilde = d->span.len > 1 ? (size_t)d->span.len - 1 : 0;
            for (i = 0; i < n_tilde && caret_col + i < line_len; i++)
                fputc('~', f);
            fprintf(f, "%s\n", reset);
        }
    }
}

void diag_render_stderr(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    diag_render(stderr, d, dc, diag_use_color());
}

void cgf_ice_set_input(const char *path)
{
    ice_input = path;
}

_Noreturn void cgf_ice(const char *src_file, int src_line, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "cgfried: internal compiler error at %s:%d: ", src_file,
            src_line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    if (ice_input)
        fprintf(stderr, "cgfried: while compiling '%s'\n", ice_input);
    fprintf(stderr, "cgfried: this is a bug in cgfried, not in your code; "
                    "please report it with the input that triggered it\n");
    /* Exit-code contract: 4 = ICE. The single exit() call in src/. */
    exit(4);
}
