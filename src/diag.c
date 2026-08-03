#include "diag.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "driver/toolchain.h"
#include "util/arena.h"
#include "warn/warn.h"

typedef struct {
    const char *path; /* arena copy */
    const char *src;  /* normalized diagnostic view, borrowed */
    size_t len;
    const char *original; /* exact copy-only edit input, borrowed */
    size_t original_len;
    bool safely_editable;
    bool have_identity;
    dev_t device;
    ino_t inode;
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
    DiagFixit *fixits;
    size_t fixits_len;
    size_t fixits_cap;
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

u32 diag_add_file_with_original(DiagCtx *dc, const char *path, const char *src,
                                size_t len, const char *original,
                                size_t original_len, bool safely_editable)
{
    struct stat st;

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
    dc->files[dc->files_len].original = original;
    dc->files[dc->files_len].original_len = original_len;
    dc->files[dc->files_len].safely_editable = safely_editable;
    dc->files[dc->files_len].have_identity = stat(path, &st) == 0;
    if (dc->files[dc->files_len].have_identity) {
        dc->files[dc->files_len].device = st.st_dev;
        dc->files[dc->files_len].inode = st.st_ino;
    }
    dc->files_len++;
    return (u32)dc->files_len; /* ids are 1-based; 0 = no location */
}

u32 diag_add_file(DiagCtx *dc, const char *path, const char *src, size_t len)
{
    return diag_add_file_with_original(dc, path, src, len, src, len, true);
}

const char *diag_file_path(const DiagCtx *dc, u32 file_id)
{
    if (!dc || file_id == 0 || file_id > dc->files_len)
        return NULL;
    return dc->files[file_id - 1].path;
}

size_t diag_file_count(const DiagCtx *dc)
{
    return dc ? dc->files_len : 0;
}

const char *diag_file_source(const DiagCtx *dc, u32 file_id, size_t *len)
{
    if (len)
        *len = 0;
    if (!dc || file_id == 0 || file_id > dc->files_len)
        return NULL;
    if (len)
        *len = dc->files[file_id - 1].len;
    return dc->files[file_id - 1].src;
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
           a.seq == b.seq && a.origin == b.origin &&
           same_path(a.presumed_path, b.presumed_path);
}

static u64 debug_span_hash(Span sp)
{
    const unsigned char *p = (const unsigned char *)sp.presumed_path;
    const u32 fields[] = {sp.file_id,       sp.line, sp.col,   sp.len,
                          sp.presumed_line, sp.seq,  sp.origin};
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

static const DiagFixit *record_fixits(DiagCtx *dc, const DiagFixit *fixits,
                                      size_t count)
{
    DiagFixit *out;
    size_t i;

    if (!count)
        return NULL;
    if (dc->fixits_len + count > dc->fixits_cap) {
        size_t cap = dc->fixits_cap ? dc->fixits_cap * 2 : 16;
        DiagFixit *grown;

        while (cap < dc->fixits_len + count)
            cap *= 2;
        grown =
            arena_alloc(dc->arena, cap * sizeof(*grown), _Alignof(DiagFixit));
        if (dc->fixits_len)
            memcpy(grown, dc->fixits, dc->fixits_len * sizeof(*grown));
        dc->fixits = grown;
        dc->fixits_cap = cap;
    }
    out = dc->fixits + dc->fixits_len;
    for (i = 0; i < count; i++) {
        out[i] = fixits[i];
        if (out[i].insert)
            out[i].insert = arena_strdup(dc->arena, out[i].insert);
        if ((out[i].where.origin & SPAN_ORIGIN_ANY_MACRO) ||
            out[i].where.file_id == 0 || out[i].where.file_id > dc->files_len ||
            !dc->files[out[i].where.file_id - 1].safely_editable)
            out[i].machine_applicable = false;
    }
    dc->fixits_len += count;
    return out;
}

/* The single emission path. The edit array may be NULL when count is zero. */
static void emit_v(DiagCtx *dc, DiagLevel lvl, Span sp, const DiagFixit *fixits,
                   size_t fixit_count, WarnId warn_id, const char *fmt,
                   va_list ap)
{
    va_list ap2;
    int need;
    char *msg;
    Diag d;
    const DiagFixit *recorded;

    /* Once the cap has latched, everything after it is noise: the whole
     * point of the cap is to bound output volume. */
    if (dc->limit_reached)
        return;

    check_span(dc, sp);
    for (size_t i = 0; i < fixit_count; i++)
        check_span(dc, fixits[i].where);

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
    recorded = record_fixits(dc, fixits, fixit_count);
    d.fixits = recorded;
    d.fixit_count = fixit_count;
    if (fixit_count)
        d.fixit = recorded[0];
    d.warn_id = warn_id;
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

    va_start(ap, fmt);
    emit_v(dc, lvl, sp, NULL, 0, WARN_NONE, fmt, ap);
    va_end(ap);
}

void diag_emit_fixit(DiagCtx *dc, DiagLevel lvl, Span sp, Span fix_where,
                     const char *insert, const char *fmt, ...)
{
    va_list ap;
    DiagFixit fixit;

    memset(&fixit, 0, sizeof(fixit));
    fixit.where = fix_where;
    fixit.insert = insert;
    fixit.machine_applicable = true;
    va_start(ap, fmt);
    emit_v(dc, lvl, sp, &fixit, 1, WARN_NONE, fmt, ap);
    va_end(ap);
}

void diag_emit_with_fixits(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                           const DiagFixit *fixits, size_t fixit_count,
                           const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit_v(dc, lvl, sp, fixits, fixit_count, warn_id, fmt, ap);
    va_end(ap);
}

void diag_emit_warn_fixits(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                           const DiagFixit *fixits, size_t fixit_count,
                           const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    emit_v(dc, lvl, sp, fixits, fixit_count, warn_id, fmt, ap);
    va_end(ap);
}

void diag_emit_warn_v(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                      const char *fmt, va_list ap)
{
    emit_v(dc, lvl, sp, NULL, 0, warn_id, fmt, ap);
}

size_t diag_fixit_count(const DiagCtx *dc)
{
    return dc ? dc->fixits_len : 0;
}

const DiagFixit *diag_fixit_at(const DiagCtx *dc, size_t index)
{
    if (!dc || index >= dc->fixits_len)
        return NULL;
    return &dc->fixits[index];
}

void diag_clear_fixits(DiagCtx *dc)
{
    if (dc)
        dc->fixits_len = 0;
}

void diag_emit_warn(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                    const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    diag_emit_warn_v(dc, lvl, sp, warn_id, fmt, ap);
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

u32 diag_warning_count(const DiagCtx *dc)
{
    return dc->warning_count;
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

    const char *warn_flag = warn_flag_name(d->warn_id);
    const char *warn_eq = d->level == DIAG_ERROR ? "error=" : "";

    if (d->span.file_id == 0) {
        fprintf(f, "%scgfried:%s %s%s:%s %s", bold, reset, lvl_col,
                level_str(d->level), reset, d->message);
        if (warn_flag)
            fprintf(f, " [-W%s%s]", warn_eq, warn_flag);
        fputc('\n', f);
        return;
    }

    /* #line presumed path/line override the HEADER; the caret snippet
     * below always comes from the physical location. */
    fprintf(f, "%s%s:%u:%u:%s %s%s:%s %s", bold,
            d->span.presumed_path ? d->span.presumed_path
                                  : dc->files[d->span.file_id - 1].path,
            (unsigned)(d->span.presumed_line ? d->span.presumed_line
                                             : d->span.line),
            (unsigned)d->span.col, reset, lvl_col, level_str(d->level), reset,
            d->message);
    if (warn_flag)
        fprintf(f, " [-W%s%s]", warn_eq, warn_flag);
    fputc('\n', f);

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

static void print_escaped(FILE *f, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (p && *p) {
        switch (*p) {
        case '\\':
            fputs("\\\\", f);
            break;
        case '"':
            fputs("\\\"", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if (*p < 0x20 || *p == 0x7f)
                fprintf(f, "\\%03o", (unsigned)*p);
            else
                fputc(*p, f);
            break;
        }
        p++;
    }
}

static bool span_end(const DiagCtx *dc, Span sp, u32 *end_line, u32 *end_col)
{
    const DiagFile *file;
    size_t pos = 0;
    u32 line = 1, col = 1;

    if (sp.file_id == 0 || sp.file_id > dc->files_len || !sp.line || !sp.col)
        return false;
    file = &dc->files[sp.file_id - 1];
    while (pos < file->len && line < sp.line) {
        if (file->src[pos++] == '\n')
            line++;
    }
    if (line != sp.line)
        return false;
    while (pos < file->len && file->src[pos] != '\n' && col < sp.col) {
        pos++;
        col++;
    }
    if (col != sp.col)
        return false;
    for (u32 i = 0; i < sp.len; i++) {
        if (pos == file->len)
            return false;
        if (file->src[pos++] == '\n') {
            line++;
            col = 1;
        } else {
            col++;
        }
    }
    *end_line = line;
    *end_col = col;
    return true;
}

void diag_render_parseable_fixits(FILE *f, const Diag *d, const DiagCtx *dc)
{
    size_t i;

    for (i = 0; i < d->fixit_count; i++) {
        const DiagFixit *fixit = &d->fixits[i];
        const char *path = diag_file_path(dc, fixit->where.file_id);
        u32 end_line, end_col;

        if (!fixit->insert || !path || fixit->where.file_id == 0 ||
            fixit->where.file_id > dc->files_len ||
            !dc->files[fixit->where.file_id - 1].safely_editable ||
            !span_end(dc, fixit->where, &end_line, &end_col))
            continue;
        fputs("fix-it:\"", f);
        print_escaped(f, path);
        fprintf(f, "\":{%u:%u-%u:%u}:\"", (unsigned)fixit->where.line,
                (unsigned)fixit->where.col, (unsigned)end_line,
                (unsigned)end_col);
        print_escaped(f, fixit->insert);
        fputs("\"\n", f);
    }
}

typedef struct {
    const DiagFixit *fixit;
    const char *path;
    size_t ordinal;
    size_t start;
    size_t end;
    bool conflict;
    bool selected;
} FixWork;

static bool same_edit_file(const DiagCtx *dc, u32 a, u32 b)
{
    const DiagFile *left, *right;

    if (!a || !b || a > dc->files_len || b > dc->files_len)
        return false;
    left = &dc->files[a - 1];
    right = &dc->files[b - 1];
    if (left->have_identity && right->have_identity)
        return left->device == right->device && left->inode == right->inode;
    return same_path(left->path, right->path);
}

static bool original_line_col_offset(const DiagFile *file, u32 wanted_line,
                                     u32 wanted_col, size_t *offset)
{
    size_t pos = 0;
    u32 line = 1, col = 1;

    if (!wanted_line || !wanted_col)
        return false;
    while (pos < file->original_len && line < wanted_line) {
        if (file->original[pos] == '\r') {
            pos++;
            if (pos < file->original_len && file->original[pos] == '\n')
                pos++;
            line++;
        } else if (file->original[pos++] == '\n') {
            line++;
        }
    }
    if (line != wanted_line)
        return false;
    while (pos < file->original_len && file->original[pos] != '\n' &&
           file->original[pos] != '\r' && col < wanted_col) {
        pos++;
        col++;
    }
    if (col != wanted_col)
        return false;
    *offset = pos;
    return true;
}

static bool span_offsets(const DiagCtx *dc, Span sp, size_t *start, size_t *end)
{
    const DiagFile *file;
    u32 end_line, end_col;

    if (sp.file_id == 0 || sp.file_id > dc->files_len)
        return false;
    file = &dc->files[sp.file_id - 1];
    return file->safely_editable && span_end(dc, sp, &end_line, &end_col) &&
           original_line_col_offset(file, sp.line, sp.col, start) &&
           original_line_col_offset(file, end_line, end_col, end);
}

static bool edit_conflicts(const DiagCtx *dc, const FixWork *a,
                           const FixWork *b)
{
    if (!same_edit_file(dc, a->fixit->where.file_id, b->fixit->where.file_id))
        return false;
    if (a->start == a->end && b->start == b->end)
        return a->start == b->start;
    if (a->start == a->end)
        return a->start >= b->start && a->start <= b->end;
    if (b->start == b->end)
        return b->start >= a->start && b->start <= a->end;
    /* Adjacent replacements also conflict. Applying either could invalidate
     * the other's intended token boundary even though byte intervals are
     * half-open. The conservative rule preserves tooling trust. */
    return a->start <= b->end && b->start <= a->end;
}

static int prompt_choice(const DiagCtx *dc, FILE *input, FILE *prompt,
                         const DiagFixit *fixit)
{
    int ch, tail;
    const char *path = diag_file_path(dc, fixit->where.file_id);

    fprintf(prompt, "apply fix-it at %s:%u:%u (%s)? [y/n/a/q] ",
            path ? path : "<unknown>", (unsigned)fixit->where.line,
            (unsigned)fixit->where.col,
            fixit->machine_applicable ? "machine-applicable" : "advisory");
    fflush(prompt);
    ch = fgetc(input);
    if (ch == EOF)
        return 'q';
    do {
        tail = fgetc(input);
    } while (tail != '\n' && tail != EOF);
    return ch;
}

static void sort_reverse(FixWork **items, size_t count)
{
    size_t i;

    for (i = 1; i < count; i++) {
        FixWork *item = items[i];
        size_t j = i;

        while (j > 0 && (items[j - 1]->start < item->start ||
                         (items[j - 1]->start == item->start &&
                          items[j - 1]->ordinal < item->ordinal))) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = item;
    }
}

static bool write_fixed_file(DiagCtx *dc, u32 file_id, FixWork *work,
                             size_t count)
{
    const DiagFile *file = &dc->files[file_id - 1];
    FixWork **selected;
    size_t selected_count = 0, final_len = file->original_len, i;
    char *result, *out_path, *temp_path;
    FILE *out;
    int fd;

    selected =
        arena_alloc(dc->arena, count * sizeof(*selected), _Alignof(FixWork *));
    for (i = 0; i < count; i++) {
        size_t insert_len;

        if (!work[i].selected ||
            !same_edit_file(dc, work[i].fixit->where.file_id, file_id))
            continue;
        {
            const DiagFile *edit_file =
                &dc->files[work[i].fixit->where.file_id - 1];

            if (edit_file->original_len != file->original_len ||
                (file->original_len &&
                 memcmp(edit_file->original, file->original,
                        file->original_len)))
                return false;
        }
        insert_len = strlen(work[i].fixit->insert);
        final_len -= work[i].end - work[i].start;
        final_len += insert_len;
        selected[selected_count++] = &work[i];
    }
    if (!selected_count)
        return true;
    sort_reverse(selected, selected_count);
    result = arena_alloc(dc->arena, final_len + 1, 1);
    memcpy(result, file->original, file->original_len);
    {
        size_t current_len = file->original_len;

        for (i = 0; i < selected_count; i++) {
            FixWork *edit = selected[i];
            size_t old_len = edit->end - edit->start;
            size_t new_len = strlen(edit->fixit->insert);

            memmove(result + edit->start + new_len, result + edit->end,
                    current_len - edit->end);
            memcpy(result + edit->start, edit->fixit->insert, new_len);
            current_len = current_len - old_len + new_len;
        }
    }
    result[final_len] = '\0';
    {
        static const char suffix[] = ".cgf-fixed";
        static const char temp_suffix[] = ".tmp.XXXXXX";

        out_path =
            arena_alloc(dc->arena, strlen(file->path) + sizeof(suffix), 1);
        sprintf(out_path, "%s%s", file->path, suffix);
        temp_path = arena_alloc(
            dc->arena,
            strlen(file->path) + sizeof(suffix) + sizeof(temp_suffix), 1);
        sprintf(temp_path, "%s%s%s", file->path, suffix, temp_suffix);
    }
    fd = mkstemp(temp_path);
    if (fd < 0)
        return false;
    out = fdopen(fd, "wb");
    if (!out) {
        close(fd);
        unlink(temp_path);
        return false;
    }
    if (fwrite(result, 1, final_len, out) != final_len) {
        fclose(out);
        unlink(temp_path);
        return false;
    }
    if (fclose(out) != 0) {
        unlink(temp_path);
        return false;
    }
    /* Atomic replacement never follows an existing destination symlink, so
     * even a hostile stale .cgf-fixed entry cannot redirect this write into
     * the source file or another target. */
    if (rename(temp_path, out_path) != 0) {
        unlink(temp_path);
        return false;
    }
    return true;
}

bool diag_apply_fixits(DiagCtx *dc, DiagFixitApplyMode mode, FILE *input,
                       FILE *prompt, DiagFixitApplyReport *report)
{
    DiagFixitApplyReport local = {0};
    FixWork *work;
    size_t valid_count = 0, i, j;
    bool accept_all = mode == DIAG_FIXITS_ALL;
    bool quit = false;

    if (!report)
        report = &local;
    memset(report, 0, sizeof(*report));
    if (mode == DIAG_FIXITS_INTERACTIVE && (!input || !isatty(fileno(input)))) {
        report->non_tty = true;
        return false;
    }
    if (!prompt)
        prompt = stderr;
    work = arena_alloc(dc->arena,
                       (dc->fixits_len ? dc->fixits_len : 1) * sizeof(*work),
                       _Alignof(FixWork));
    memset(work, 0, dc->fixits_len * sizeof(*work));
    for (i = 0; i < dc->fixits_len; i++) {
        const DiagFixit *fixit = &dc->fixits[i];

        if (!fixit->insert ||
            !span_offsets(dc, fixit->where, &work[valid_count].start,
                          &work[valid_count].end))
            continue;
        work[valid_count].fixit = fixit;
        work[valid_count].path = diag_file_path(dc, fixit->where.file_id);
        work[valid_count].ordinal = i;
        valid_count++;
    }
    report->considered = valid_count;
    for (i = 0; i < valid_count; i++) {
        for (j = i + 1; j < valid_count; j++) {
            if (edit_conflicts(dc, &work[i], &work[j])) {
                work[i].conflict = true;
                work[j].conflict = true;
            }
        }
        if (work[i].conflict)
            report->conflicts++;
        if (work[i].conflict && !report->first_conflict_path) {
            report->first_conflict_path =
                diag_file_path(dc, work[i].fixit->where.file_id);
            report->first_conflict_line = work[i].fixit->where.line;
            report->first_conflict_col = work[i].fixit->where.col;
        }
    }
    for (i = 0; i < valid_count && !quit; i++) {
        int choice;

        if (work[i].conflict)
            continue;
        if (mode == DIAG_FIXITS_ALL && !work[i].fixit->machine_applicable) {
            report->advisory_skipped++;
            continue;
        }
        if (accept_all) {
            work[i].selected = true;
            continue;
        }
        choice = prompt_choice(dc, input, prompt, work[i].fixit);
        switch (choice) {
        case 'y':
        case 'Y':
            work[i].selected = true;
            break;
        case 'a':
        case 'A':
            work[i].selected = true;
            accept_all = true;
            break;
        case 'q':
        case 'Q':
            quit = true;
            break;
        default:
            break;
        }
    }
    for (u32 file_id = 1; file_id <= dc->files_len; file_id++) {
        size_t before = report->applied;
        bool already_handled = false;

        for (u32 prior = 1; prior < file_id; prior++)
            if (same_edit_file(dc, prior, file_id)) {
                already_handled = true;
                break;
            }
        if (already_handled)
            continue;

        for (i = 0; i < valid_count; i++)
            if (work[i].selected &&
                same_edit_file(dc, work[i].fixit->where.file_id, file_id))
                report->applied++;
        if (report->applied == before)
            continue;
        if (!write_fixed_file(dc, file_id, work, valid_count)) {
            report->io_error = true;
            return false;
        }
        report->files_written++;
    }
    return true;
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
