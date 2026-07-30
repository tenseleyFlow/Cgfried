#ifndef CGF_DIAG_H
#define CGF_DIAG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "util/base.h"

/* 1-based line/col; col counts bytes. file_id 0 = no source location (driver-
 * level diagnostics render as "cgfried: error: ..."). */
typedef struct {
    u32 file_id;
    u32 line;
    u32 col;
    u32 len;
} Span;

typedef enum {
    DIAG_NOTE,
    DIAG_WARNING,
    DIAG_ERROR,
    DIAG_FATAL,
} DiagLevel;

typedef struct {
    DiagLevel level;
    Span span;
    const char *message; /* formatted, arena-owned */
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
void     diag_set_sink(DiagCtx *dc, DiagSink sink);

/* Registers a source buffer; returns its file_id (>= 1). The path is copied;
 * src is borrowed and must outlive the context. */
u32  diag_add_file(DiagCtx *dc, const char *path, const char *src, size_t len);
void diag_emit(DiagCtx *dc, DiagLevel lvl, Span sp, const char *fmt, ...);
bool diag_had_error(const DiagCtx *dc);
u32  diag_error_count(const DiagCtx *dc);

/* Default sink; renders per the format contract (path:line:col, source line,
 * caret line mirroring pre-caret tabs). Color per diag_use_color(). */
void diag_render_stderr(void *user, const Diag *d, const DiagCtx *dc);
/* Same renderer with explicit stream and color choice, for byte-exact tests. */
void diag_render(FILE *f, const Diag *d, const DiagCtx *dc, bool color);

/* Colors iff stderr is a tty and NO_COLOR is unset-or-empty (no-color.org);
 * NO_COLOR wins over CLICOLOR_FORCE; non-empty CLICOLOR_FORCE forces color
 * without a tty. Computed once. */
bool diag_use_color(void);

/* ICE: structured report to stderr, then exit 4 — the only exit() call in
 * src/ (the exit-code contract stays auditable). Never a bare crash. */
_Noreturn void cgf_ice(const char *src_file, int src_line, const char *fmt,
                       ...);
#define CGF_ICE(...) cgf_ice(__FILE__, __LINE__, __VA_ARGS__)

/* Names the input in ICE reports ("while compiling <input>"). */
void cgf_ice_set_input(const char *path);

#endif
