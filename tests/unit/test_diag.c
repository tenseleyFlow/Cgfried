#include <stdio.h>
#include <string.h>

#include "diag.h"
#include "unit.h"
#include "util/arena.h"

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
    Span sp = {0, 0, 0, 0};

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
