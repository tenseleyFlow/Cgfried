#ifndef CGF_DIAG_H
#define CGF_DIAG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "util/base.h"

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
} Span;

typedef enum {
    DIAG_NOTE,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_FATAL,
} DiagLevel;

/* A machine-applicable edit. RECORDED here, RENDERED in Sprint 37 (which
 * owns -fdiagnostics-*): a fix-it that is stored but not printed still lets
 * tests assert the compiler knew the repair, without committing to an
 * output format we have not designed yet. `insert == NULL` means absent. */
typedef struct DiagFixit {
    Span where;         /* the point (len 0) or range to replace */
    const char *insert; /* text to insert there; NULL = no fix-it */
} DiagFixit;

typedef struct {
    DiagLevel level;
    Span span;
    const char *message; /* formatted, arena-owned */
    DiagFixit fixit;
} Diag;

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
/* Resolve a registered physical file id, or NULL for id 0/out of range. */
const char *diag_file_path(const DiagCtx *dc, u32 file_id);
/* The displayed path for a span: its #line name when present, otherwise
 * the registered physical path. NULL means the span has no resolvable file. */
const char *diag_span_path(const DiagCtx *dc, Span sp);
/* Intern and resolve execution-attribution spans. Diagnostics continue to
 * use the original Span; DWARF consumers opt into the debug location. */
u32 diag_add_debug_span(DiagCtx *dc, Span sp);
Span diag_span_for_debug(const DiagCtx *dc, Span sp);
void diag_emit(DiagCtx *dc, DiagLevel lvl, Span sp, const char *fmt, ...);
/* Same, carrying a fix-it. `insert` is borrowed and must outlive the sink
 * call (string literals and interned names qualify). */
void diag_emit_fixit(DiagCtx *dc, DiagLevel lvl, Span sp, Span fix_where,
                     const char *insert, const char *fmt, ...);
/* The insertion point immediately after `sp`, clamped to the end of that
 * PHYSICAL line. A token whose spelling was assembled across line splices
 * starts on one line and ends on another, so span.col + span.len can point
 * past the line it names; clamping here keeps every emitted span
 * renderable. Found by the frontend fuzzer's span-bounds invariant. */
Span diag_point_after(const DiagCtx *dc, Span sp);
bool diag_had_error(const DiagCtx *dc);
u32 diag_error_count(const DiagCtx *dc);

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
