#ifndef CGF_DIAG_H
#define CGF_DIAG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "util/base.h"
#include "warn/id.h"

/* 1-based line/col; col counts bytes. file_id 0 = no source location (driver-
 * level diagnostics render as "cgfried: error: ..."). presumed_* (from
 * #line) override the DISPLAYED path/line only; the caret snippet always
 * comes from the physical file_id/line. debug_loc is an optional DiagCtx
 * registry id for the user-visible execution location (notably the outermost
 * macro invocation rather than the macro body spelling). Initialize with
 * {0}. */
typedef struct {
    u32 file_id;
    u32 line;
    u32 col;
    u32 len;
    const char *presumed_path; /* NULL = use the file's real path */
    u32 presumed_line;         /* 0 = use the physical line */
    u32 debug_loc;             /* 0 = this span; otherwise DiagCtx id */
    u32 seq;                   /* lexical event sequence (warning pragmas) */
    u8 origin;                 /* immutable SPAN_ORIGIN_* provenance */
} Span;

enum {
    SPAN_ORIGIN_SYSTEM_SPELLING = 1u << 0,
    SPAN_ORIGIN_SYSTEM_MACRO = 1u << 1,
    SPAN_ORIGIN_ANY_MACRO = 1u << 2
};

typedef enum {
    DIAG_NOTE,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_FATAL,
} DiagLevel;

/* A source edit. `where` is a physical, end-exclusive replacement range:
 * len 0 inserts, otherwise len bytes are replaced. Fix-it columns are bytes,
 * as in gcc's parseable format. Macro spelling is never safe to rewrite
 * mechanically; emission automatically clears machine_applicable whenever
 * where has SPAN_ORIGIN_ANY_MACRO. `insert == NULL` means absent. */
typedef struct DiagFixit {
    Span where;
    const char *insert;
    bool machine_applicable;
} DiagFixit;

typedef struct {
    DiagLevel level;
    Span span;
    const char *message; /* formatted, arena-owned */
    DiagFixit fixit;     /* compatibility view: first fix-it, or zeroed */
    const DiagFixit *fixits;
    size_t fixit_count;
    WarnId warn_id; /* WARN_NONE for raw diagnostics and notes */
} Diag;

typedef enum {
    DIAG_FIXITS_ALL,
    DIAG_FIXITS_INTERACTIVE,
} DiagFixitApplyMode;

typedef struct {
    size_t considered;
    size_t applied;
    size_t advisory_skipped;
    size_t conflicts;
    size_t files_written;
    const char *first_conflict_path; /* physical path, borrowed from DiagCtx */
    u32 first_conflict_line;
    u32 first_conflict_col;
    bool non_tty;
    bool io_error;
} DiagFixitApplyReport;

typedef struct DiagCtx DiagCtx;

/* The sink is why tests never scrape rendered text: install a capturing sink
 * and assert on Diag fields structurally. */
typedef struct DiagSink {
    void (*handle)(void *user, const Diag *d, const DiagCtx *dc);
    void *user;
} DiagSink;

struct Arena; /* util/arena.h */

DiagCtx *diag_ctx_new(struct Arena *arena); /* default sink: stderr renderer */
void diag_set_sink(DiagCtx *dc, DiagSink sink);
/* Installs `sink`, returning the previous one (scoped suppression — e.g.
 * the macro engine's silent paste re-lex probe). */
DiagSink diag_swap_sink(DiagCtx *dc, DiagSink sink);

/* Registers a source buffer; returns its file_id (>= 1). The path is copied;
 * src is borrowed and must outlive the context. */
u32 diag_add_file(DiagCtx *dc, const char *path, const char *src, size_t len);
/* Registers the phase-normalized diagnostic view together with the original
 * bytes used by copy-only fix-it application. `original` is borrowed. When
 * normalization changed token columns, safely_editable is false and edits
 * for this file are neither rendered nor applied. */
u32 diag_add_file_with_original(DiagCtx *dc, const char *path, const char *src,
                                size_t len, const char *original,
                                size_t original_len, bool safely_editable);
/* Resolve a registered physical file id, or NULL for id 0/out of range. */
const char *diag_file_path(const DiagCtx *dc, u32 file_id);
size_t diag_file_count(const DiagCtx *dc);
/* Borrowed source buffer and byte length for a registered physical file. */
const char *diag_file_source(const DiagCtx *dc, u32 file_id, size_t *len);
/* The displayed path for a span: its #line name when present, otherwise
 * the registered physical path. NULL means the span has no resolvable file. */
const char *diag_span_path(const DiagCtx *dc, Span sp);
/* Intern and resolve execution-attribution spans. Diagnostics continue to
 * use the original Span; DWARF consumers opt into the debug location. */
u32 diag_add_debug_span(DiagCtx *dc, Span sp);
Span diag_span_for_debug(const DiagCtx *dc, Span sp);
void diag_emit(DiagCtx *dc, DiagLevel lvl, Span sp, const char *fmt, ...);
/* Warning-aware path. Raw diag_emit semantics remain unchanged. warn_id must
 * name a row in warn/warnings.def; a DIAG_ERROR level denotes promotion. */
void diag_emit_warn(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                    const char *fmt, ...);
void diag_emit_warn_v(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                      const char *fmt, va_list ap);
/* General emission path carrying zero or more edits. The fix-it array and
 * replacement strings are copied into the diagnostic arena. */
void diag_emit_with_fixits(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                           const DiagFixit *fixits, size_t fixit_count,
                           const char *fmt, ...);
void diag_emit_warn_fixits(DiagCtx *dc, DiagLevel lvl, Span sp, WarnId warn_id,
                           const DiagFixit *fixits, size_t fixit_count,
                           const char *fmt, ...);
/* Compatibility wrapper for one machine-applicable fix-it. */
void diag_emit_fixit(DiagCtx *dc, DiagLevel lvl, Span sp, Span fix_where,
                     const char *insert, const char *fmt, ...);
/* Flat, emission-ordered collection used by copy-only apply modes. */
size_t diag_fixit_count(const DiagCtx *dc);
const DiagFixit *diag_fixit_at(const DiagCtx *dc, size_t index);
void diag_clear_fixits(DiagCtx *dc);
/* Applies selected edits to `<physical-path>.cgf-fixed`; originals are never
 * opened for writing. `interactive` requires a TTY input stream. */
bool diag_apply_fixits(DiagCtx *dc, DiagFixitApplyMode mode, FILE *input,
                       FILE *prompt, DiagFixitApplyReport *report);
/* The insertion point immediately after `sp`, clamped to the end of that
 * PHYSICAL line. A token whose spelling was assembled across line splices
 * starts on one line and ends on another, so span.col + span.len can point
 * past the line it names; clamping here keeps every emitted span
 * renderable. Found by the frontend fuzzer's span-bounds invariant. */
Span diag_point_after(const DiagCtx *dc, Span sp);
bool diag_had_error(const DiagCtx *dc);
u32 diag_error_count(const DiagCtx *dc);
u32 diag_warning_count(const DiagCtx *dc);

/* Error cap (-fmax-errors=N, alias -ferror-limit=N). 0 = unlimited, which
 * is gcc's default. Counts ERRORS only — warnings and notes never move it.
 *
 * Reaching the cap does NOT exit here: cgf_ice is the only process-exit in
 * src/ besides main returning, and that invariant is what keeps the
 * exit-code contract auditable. Instead the cap latches a flag, emits the
 * final message once, and every subsequent diagnostic is dropped; the
 * parser's loops poll diag_error_limit_reached() and unwind, and the
 * driver turns it into exit 1. */
void diag_set_max_errors(DiagCtx *dc, u32 max_errors);
bool diag_error_limit_reached(const DiagCtx *dc);

/* Diagnostics the front end deliberately did NOT emit because they would
 * have been about a poisoned (already-diagnosed) construct. Tests assert
 * this moved, which is the only way to tell real suppression apart from
 * an error that never happened. */
u32 diag_suppressed_count(const DiagCtx *dc);
void diag_note_suppressed(DiagCtx *dc);

/* "did you mean 'uint32_t'?" — needs symbol tables, so Sprint 12 owns it.
 * Declared and NOT defined on purpose: a stub that silently emitted
 * nothing would be indistinguishable from a working implementation with no
 * candidates, and calling this today is a link error rather than a lie. */
void diag_note_suggest(DiagCtx *dc, Span sp, const char *unknown,
                       const char *const *candidates, size_t ncandidates);

/* Default sink; renders per the format contract (path:line:col, source line,
 * caret line mirroring pre-caret tabs). Color per diag_use_color(). */
void diag_render_stderr(void *user, const Diag *d, const DiagCtx *dc);
/* Same renderer with explicit stream and color choice, for byte-exact tests. */
void diag_render(FILE *f, const Diag *d, const DiagCtx *dc, bool color);
/* Emits gcc-compatible, one-line-per-edit machine-readable records. */
void diag_render_parseable_fixits(FILE *f, const Diag *d, const DiagCtx *dc);

/* Colors iff stderr is a tty and NO_COLOR is unset-or-empty (no-color.org);
 * NO_COLOR wins over CLICOLOR_FORCE; non-empty CLICOLOR_FORCE forces color
 * without a tty. Computed once. */
bool diag_use_color(void);

/* ICE: structured report to stderr, then terminates with code 4 — the one
 * place in src/ allowed to end the process besides main returning (the
 * exit-code contract stays auditable). Never a bare crash. */
_Noreturn void cgf_ice(const char *src_file, int src_line, const char *fmt,
                       ...);
#define CGF_ICE(...) cgf_ice(__FILE__, __LINE__, __VA_ARGS__)

/* Names the input in ICE reports ("while compiling <input>"). */
void cgf_ice_set_input(const char *path);

#endif
