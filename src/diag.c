#include "diag.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "driver/toolchain.h"
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
    Span *debug_spans;
    size_t debug_spans_len;
    size_t debug_spans_cap;
    u32 *debug_span_slots;
    size_t debug_span_slot_count;
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

const char *diag_file_path(const DiagCtx *dc, u32 file_id)
{
    if (!dc || file_id == 0 || file_id > dc->files_len)
        return NULL;
    return dc->files[file_id - 1].path;
}

const char *diag_span_path(const DiagCtx *dc, Span sp)
{
    if (sp.presumed_path)
        return sp.presumed_path;
    return diag_file_path(dc, sp.file_id);
}

static bool same_path(const char *a, const char *b)
{
    return a == b || (a && b && strcmp(a, b) == 0);
}

static bool same_debug_span(Span a, Span b)
{
    return a.file_id == b.file_id && a.line == b.line && a.col == b.col &&
           a.len == b.len && a.presumed_line == b.presumed_line &&
           same_path(a.presumed_path, b.presumed_path);
}

static u64 debug_span_hash(Span sp)
{
    const unsigned char *p = (const unsigned char *)sp.presumed_path;
    const u32 fields[] = {sp.file_id, sp.line, sp.col, sp.len,
                          sp.presumed_line};
    u64 h = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < CGF_ARRAY_LEN(fields); i++) {
        h ^= fields[i];
        h *= 1099511628211ULL;
    }
    while (p && *p) {
        h ^= *p++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Find the slot holding sp, or the empty slot where it belongs. */
static size_t debug_span_probe(const DiagCtx *dc, Span sp)
{
    size_t mask = dc->debug_span_slot_count - 1;
    size_t slot = (size_t)debug_span_hash(sp) & mask;

    for (;;) {
        u32 idx = dc->debug_span_slots[slot];

        if (!idx || same_debug_span(dc->debug_spans[idx - 1], sp))
            return slot;
        slot = (slot + 1) & mask;
    }
}

static void debug_span_grow_slots(DiagCtx *dc)
{
    size_t count =
        dc->debug_span_slot_count ? dc->debug_span_slot_count * 2 : 64;
    size_t i;

    dc->debug_span_slots =
        arena_alloc(dc->arena, count * sizeof(u32), _Alignof(u32));
    memset(dc->debug_span_slots, 0, count * sizeof(u32));
    dc->debug_span_slot_count = count;
    for (i = 0; i < dc->debug_spans_len; i++)
        dc->debug_span_slots[debug_span_probe(dc, dc->debug_spans[i])] =
            (u32)i + 1;
}

u32 diag_add_debug_span(DiagCtx *dc, Span sp)
{
    size_t slot;

    sp.debug_loc = 0;
    if (!sp.file_id)
        return 0;
    if (!dc->debug_span_slot_count ||
        (dc->debug_spans_len + 1) * 10 >= dc->debug_span_slot_count * 7)
        debug_span_grow_slots(dc);
    slot = debug_span_probe(dc, sp);
    if (dc->debug_span_slots[slot])
        return dc->debug_span_slots[slot];
    if (dc->debug_spans_len == dc->debug_spans_cap) {
        size_t cap = dc->debug_spans_cap ? dc->debug_spans_cap * 2 : 16;
        Span *grown =
            arena_alloc(dc->arena, cap * sizeof(Span), _Alignof(Span));

        if (dc->debug_spans_len)
            memcpy(grown, dc->debug_spans, dc->debug_spans_len * sizeof(Span));
        dc->debug_spans = grown;
        dc->debug_spans_cap = cap;
    }
    dc->debug_spans[dc->debug_spans_len] = sp;
    dc->debug_spans_len++;
    dc->debug_span_slots[slot] = (u32)dc->debug_spans_len;
    return (u32)dc->debug_spans_len;
}

Span diag_span_for_debug(const DiagCtx *dc, Span sp)
{
    if (!dc || sp.debug_loc == 0 || sp.debug_loc > dc->debug_spans_len)
        return sp;
    return dc->debug_spans[sp.debug_loc - 1];
}

/* Span validation, on only under CGF_FUZZ=1. A diagnostic whose Span
 * points outside its file renders garbage or reads out of bounds, and
 * those bugs are invisible in normal testing because the span is usually
 * right — the fuzzer's whole value here is finding the cases where it is
 * not. An ICE (exit 4, structured report) is the correct outcome: the
 * fuzzer treats it as a finding, and a user would rather see a compiler
 * bug report than a corrupted caret. */
static bool fuzz_span_checks(void)
{
    static int on = -1;

    if (on < 0) {
        const char *v = cgf_env("CGF_FUZZ");
        on = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
    }
    return on == 1;
}

static void check_span(const DiagCtx *dc, Span sp)
{
    const DiagFile *f;
    size_t line = 1, col = 1, i;

    if (!fuzz_span_checks() || sp.file_id == 0)
        return;
    if (sp.file_id > dc->files_len)
        CGF_ICE("diagnostic span names file_id %u, only %zu registered",
                sp.file_id, dc->files_len);
    f = &dc->files[sp.file_id - 1];
    if (sp.line == 0 || sp.col == 0)
        CGF_ICE("diagnostic span has zero line/col (%u:%u)", sp.line, sp.col);
    /* Walk to the named line and check the column fits it. Cheap enough:
     * this path only runs under the fuzzer. */
    for (i = 0; i < f->len && line < sp.line; i++)
        if (f->src[i] == '\n')
            line++;
    if (line != sp.line)
        CGF_ICE("diagnostic span names line %u, file '%s' has %zu", sp.line,
                f->path, line);
    for (; i < f->len && f->src[i] != '\n'; i++)
        col++;
    if (sp.col > col)
        CGF_ICE("diagnostic span col %u past end of line %u (len %zu) in '%s'",
                sp.col, sp.line, col - 1, f->path);
}

Span diag_point_after(const DiagCtx *dc, Span sp)
{
    const DiagFile *f;
    size_t line = 1, cols = 1, i;
    Span out = sp;

    out.len = 1;
    if (sp.file_id == 0 || sp.file_id > dc->files_len) {
        out.col = sp.col + sp.len;
        return out;
    }
    f = &dc->files[sp.file_id - 1];
    for (i = 0; i < f->len && line < sp.line; i++)
        if (f->src[i] == '\n')
            line++;
    if (line != sp.line) {
        out.col = sp.col + sp.len;
        return out;
    }
    for (; i < f->len && f->src[i] != '\n'; i++)
        cols++;
    out.col = sp.col + sp.len;
    if (out.col > cols)
        out.col = cols; /* one past the last character of the line */
    return out;
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

    check_span(dc, sp);

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
