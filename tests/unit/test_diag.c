#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/id.h"

/* The capture-sink pattern all future diagnostic tests copy: tests assert on
 * Diag fields structurally, never by scraping rendered text. */
typedef struct {
    Diag last;
    int count;
} Capture;

static void capture_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    Capture *c = user;

    (void)dc;
    c->last = *d;
    c->count++;
}

void test_diag_sink_structural(TestCtx *t)
{
    Arena a;
    DiagCtx *dc;
    Capture cap;
    DiagSink sink;
    Span sp = {0};

    memset(&cap, 0, sizeof(cap));
    arena_init(&a);
    dc = diag_ctx_new(&a);
    sink.handle = capture_sink;
    sink.user = &cap;
    diag_set_sink(dc, sink);

    diag_emit(dc, DIAG_WARNING, sp, "w=%d", 7);
    T_ASSERT_EQ_INT(t, cap.count, 1);
    T_ASSERT(t, cap.last.level == DIAG_WARNING);
    T_ASSERT_EQ_STR(t, cap.last.message, "w=7");
    T_ASSERT_EQ_INT(t, cap.last.warn_id, WARN_NONE);
    T_ASSERT(t, !diag_had_error(dc)); /* warnings are not errors */

    diag_emit(dc, DIAG_ERROR, sp, "x=%d", 42);
    T_ASSERT_EQ_INT(t, cap.count, 2);
    T_ASSERT(t, cap.last.level == DIAG_ERROR);
    T_ASSERT_EQ_INT(t, cap.last.span.file_id, 0);
    T_ASSERT_EQ_STR(t, cap.last.message, "x=42");
    T_ASSERT(t, diag_had_error(dc));
    T_ASSERT_EQ_INT(t, diag_error_count(dc), 1);
    arena_free_all(&a);
}

void test_diag_path_resolution(TestCtx *t)
{
    Arena a;
    DiagCtx *dc;
    Span sp = {0};

    arena_init(&a);
    dc = diag_ctx_new(&a);
    sp.file_id = diag_add_file(dc, "physical.c", "", 0);
    T_ASSERT_EQ_STR(t, diag_file_path(dc, sp.file_id), "physical.c");
    T_ASSERT_EQ_STR(t, diag_span_path(dc, sp), "physical.c");
    sp.presumed_path = "logical.c";
    T_ASSERT_EQ_STR(t, diag_span_path(dc, sp), "logical.c");
    T_ASSERT(t, diag_file_path(dc, 0) == NULL);
    T_ASSERT(t, diag_file_path(dc, 2) == NULL);
    arena_free_all(&a);
}

void test_diag_debug_span_registry(TestCtx *t)
{
    Arena a;
    DiagCtx *dc;
    Span spelling = {0}, invocation = {0}, got;
    u32 i;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    spelling.file_id = diag_add_file(dc, "macro.h", "#define M 1\n", 12);
    spelling.line = 1;
    spelling.col = 11;
    spelling.len = 1;
    invocation.file_id = diag_add_file(dc, "main.c", "M\n", 2);
    invocation.line = 1;
    invocation.col = 1;
    invocation.len = 1;
    spelling.debug_loc = diag_add_debug_span(dc, invocation);
    T_ASSERT(t, spelling.debug_loc != 0);
    T_ASSERT_EQ_INT(t, diag_add_debug_span(dc, invocation), spelling.debug_loc);
    got = diag_span_for_debug(dc, spelling);
    T_ASSERT_EQ_INT(t, got.file_id, invocation.file_id);
    T_ASSERT_EQ_INT(t, got.line, 1);
    T_ASSERT_EQ_STR(t, diag_span_path(dc, got), "main.c");
    T_ASSERT_EQ_INT(t, diag_span_for_debug(dc, invocation).file_id,
                    invocation.file_id);
    /* Cross several hash-table growth points, then prove every id remains
     * stable after reinsertion. This also guards against regressing to the
     * quadratic linear-interning path on macro-heavy translation units. */
    for (i = 2; i <= 2048; i++) {
        invocation.line = i;
        T_ASSERT_EQ_INT(t, diag_add_debug_span(dc, invocation), i);
    }
    for (i = 1; i <= 2048; i++) {
        invocation.line = i;
        T_ASSERT_EQ_INT(t, diag_add_debug_span(dc, invocation), i);
    }
    arena_free_all(&a);
}

static void render_to_string(const Diag *d, const DiagCtx *dc, char *out,
                             size_t out_size, TestCtx *t)
{
    FILE *f = tmpfile();
    size_t n;

    T_ASSERT(t, f != NULL);
    diag_render(f, d, dc, false);
    rewind(f);
    n = fread(out, 1, out_size - 1, f);
    out[n] = '\0';
    fclose(f);
}

void test_diag_render_caret_tabs(TestCtx *t)
{
    /* Line 2 is tab-indented; the caret line must mirror the tab so the
     * caret aligns at any tab width. Col 6 = 'x' (1-based bytes). */
    static const char src[] = "int main;\n\tint x = y;\n";
    static const char expect[] = "t.c:2:6: error: no clue about 'x'\n"
                                 "\tint x = y;\n"
                                 "\t    ^~~\n";
    Arena a;
    DiagCtx *dc;
    u32 fid;
    Diag d;
    char got[256];

    arena_init(&a);
    dc = diag_ctx_new(&a);
    fid = diag_add_file(dc, "t.c", src, sizeof(src) - 1);
    T_ASSERT_EQ_INT(t, fid, 1);

    memset(&d, 0, sizeof(d)); /* Span contract: zero-init (presumed_*) */
    d.level = DIAG_ERROR;
    d.span.file_id = fid;
    d.span.line = 2;
    d.span.col = 6;
    d.span.len = 3;
    d.message = "no clue about 'x'";
    render_to_string(&d, dc, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, expect);

    /* Driver-level (no-span) rendering. */
    d.span.file_id = 0;
    render_to_string(&d, dc, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, "cgfried: error: no clue about 'x'\n");
    arena_free_all(&a);
}
