#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "diag.h"
#include "unit.h"
#include "util/arena.h"
#include "warn/id.h"

#define FIX_SPAN(fid, ln, column, length)                                      \
    {.file_id = (fid), .line = (ln), .col = (column), .len = (length)}

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

void test_diag_multiple_fixits_recorded(TestCtx *t)
{
    static const char src[] = "int x;\n";
    Arena a;
    DiagCtx *dc;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    DiagFixit edits[2] = {{{0}, "long", true}, {{0}, "y", false}};
    u32 fid;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    fid = diag_add_file(dc, "multi.c", src, sizeof(src) - 1);
    edits[0].where = (Span)FIX_SPAN(fid, 1, 1, 3);
    edits[1].where = (Span)FIX_SPAN(fid, 1, 5, 1);
    diag_set_sink(dc, sink);
    diag_emit_with_fixits(dc, DIAG_NOTE, edits[0].where, WARN_NONE, edits, 2,
                          "two edits");
    T_ASSERT_EQ_INT(t, cap.last.fixit_count, 2);
    T_ASSERT_EQ_STR(t, cap.last.fixit.insert, "long");
    T_ASSERT_EQ_STR(t, cap.last.fixits[1].insert, "y");
    T_ASSERT(t, cap.last.fixits[0].machine_applicable);
    T_ASSERT(t, !cap.last.fixits[1].machine_applicable);
    T_ASSERT_EQ_INT(t, diag_fixit_count(dc), 2);
    T_ASSERT_EQ_STR(t, diag_fixit_at(dc, 1)->insert, "y");
    diag_clear_fixits(dc);
    T_ASSERT_EQ_INT(t, diag_fixit_count(dc), 0);
    arena_free_all(&a);
}

void test_diag_parseable_fixit_format(TestCtx *t)
{
    static const char src[] = "\303\251x\nabc\n";
    static const struct {
        Span where;
        const char *replacement;
        const char *tail;
    } cases[] = {
        {FIX_SPAN(1, 1, 1, 0), "", "{1:1-1:1}:\"\"\n"},
        {FIX_SPAN(1, 1, 1, 2), "e", "{1:1-1:3}:\"e\"\n"},
        {FIX_SPAN(1, 1, 3, 2), "z", "{1:3-2:1}:\"z\"\n"},
        {FIX_SPAN(1, 2, 1, 1), "A", "{2:1-2:2}:\"A\"\n"},
        {FIX_SPAN(1, 2, 2, 1), "\\", "{2:2-2:3}:\"\\\\\"\n"},
        {FIX_SPAN(1, 2, 2, 1), "\"", "{2:2-2:3}:\"\\\"\"\n"},
        {FIX_SPAN(1, 2, 2, 1), "\n", "{2:2-2:3}:\"\\n\"\n"},
        {FIX_SPAN(1, 2, 2, 1), "\r", "{2:2-2:3}:\"\\015\"\n"},
        {FIX_SPAN(1, 2, 2, 1), "\t", "{2:2-2:3}:\"\\t\"\n"},
        {FIX_SPAN(1, 2, 2, 1), "\001", "{2:2-2:3}:\"\\001\"\n"},
        {FIX_SPAN(1, 2, 3, 0), "xyz", "{2:3-2:3}:\"xyz\"\n"},
        {FIX_SPAN(1, 2, 4, 0), "\303\251", "{2:4-2:4}:\"\303\251\"\n"},
    };
    Arena a;
    DiagCtx *dc;
    size_t i;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_add_file(dc, "weird\"\\x.c", src, sizeof(src) - 1);
    for (i = 0; i < CGF_ARRAY_LEN(cases); i++) {
        DiagFixit fixit = {cases[i].where, cases[i].replacement, true};
        Diag d = {0};
        char got[256], expect[256];
        FILE *f = tmpfile();
        size_t n;

        T_ASSERT(t, f != NULL);
        d.fixits = &fixit;
        d.fixit_count = 1;
        diag_render_parseable_fixits(f, &d, dc);
        rewind(f);
        n = fread(got, 1, sizeof(got) - 1, f);
        got[n] = '\0';
        fclose(f);
        snprintf(expect, sizeof(expect), "fix-it:\"weird\\\"\\\\x.c\":%s",
                 cases[i].tail);
        T_ASSERT_EQ_STR(t, got, expect);
    }
    arena_free_all(&a);
}

static void read_whole_file(const char *path, char *out, size_t cap, TestCtx *t)
{
    FILE *f = fopen(path, "rb");
    size_t n;

    T_ASSERT(t, f != NULL);
    if (!f) {
        out[0] = '\0';
        return;
    }
    n = fread(out, 1, cap - 1, f);
    out[n] = '\0';
    fclose(f);
}

void test_diag_apply_all_copy_only_utf8(TestCtx *t)
{
    static const char src[] = "/*\317\200*/ int a;\n";
    Arena a;
    DiagCtx *dc;
    DiagFixit edits[2] = {{{0}, "value", true}, {{0}, "/* advisory */", false}};
    DiagFixitApplyReport report;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    char path[128], fixed[144], got[128];
    FILE *f;
    u32 fid;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-%ld.c", (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    f = fopen(path, "wb");
    T_ASSERT(t, f != NULL);
    if (f) {
        fwrite(src, 1, sizeof(src) - 1, f);
        fclose(f);
    }
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid = diag_add_file(dc, path, src, sizeof(src) - 1);
    edits[0].where = (Span)FIX_SPAN(fid, 1, 12, 1);
    edits[1].where = (Span)FIX_SPAN(fid, 1, 1, 0);
    diag_emit_with_fixits(dc, DIAG_NOTE, edits[0].where, WARN_NONE, edits, 2,
                          "edits");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    T_ASSERT_EQ_INT(t, report.applied, 1);
    T_ASSERT_EQ_INT(t, report.advisory_skipped, 1);
    T_ASSERT_EQ_INT(t, report.files_written, 1);
    read_whole_file(path, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, src);
    read_whole_file(fixed, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, "/*\317\200*/ int value;\n");
    unlink(fixed);
    unlink(path);
    arena_free_all(&a);
}

void test_diag_apply_preserves_original_crlf_bytes(TestCtx *t)
{
    static const char normalized[] = "int a;\nint b;\n";
    static const char original[] = "int a;\r\nint b;\r\n";
    Arena a;
    DiagCtx *dc;
    DiagFixit edit = {{0}, "value", true};
    DiagFixitApplyReport report;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    char path[128], fixed[144], got[128];
    u32 fid;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-crlf-%ld.c", (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid = diag_add_file_with_original(dc, path, normalized,
                                      sizeof(normalized) - 1, original,
                                      sizeof(original) - 1, true);
    edit.where = (Span)FIX_SPAN(fid, 2, 5, 1);
    diag_emit_with_fixits(dc, DIAG_NOTE, edit.where, WARN_NONE, &edit, 1,
                          "CRLF edit");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    read_whole_file(fixed, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, "int a;\r\nint value;\r\n");
    unlink(fixed);
    arena_free_all(&a);
}

void test_diag_apply_replaces_destination_symlink_not_target(TestCtx *t)
{
    static const char src[] = "int a;\n";
    Arena a;
    DiagCtx *dc;
    DiagFixit edit = {{0}, "value", true};
    DiagFixitApplyReport report;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    char path[128], fixed[144], got[128];
    FILE *f;
    u32 fid;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-link-%ld.c", (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    f = fopen(path, "wb");
    T_ASSERT(t, f != NULL);
    if (f) {
        fwrite(src, 1, sizeof(src) - 1, f);
        fclose(f);
    }
    T_ASSERT_EQ_INT(t, symlink(path, fixed), 0);
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid = diag_add_file(dc, path, src, sizeof(src) - 1);
    edit.where = (Span)FIX_SPAN(fid, 1, 5, 1);
    diag_emit_with_fixits(dc, DIAG_NOTE, edit.where, WARN_NONE, &edit, 1,
                          "symlink-safe edit");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    read_whole_file(path, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, src);
    read_whole_file(fixed, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, "int value;\n");
    unlink(fixed);
    unlink(path);
    arena_free_all(&a);
}

void test_diag_apply_overlap_suppresses_both(TestCtx *t)
{
    static const char src[] = "abcd\n";
    Arena a;
    DiagCtx *dc;
    DiagFixit edits[2] = {{{0}, "X", true}, {{0}, "Y", true}};
    DiagFixitApplyReport report;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    char path[128], fixed[144];
    u32 fid;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-overlap-%ld.c", (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid = diag_add_file(dc, path, src, sizeof(src) - 1);
    edits[0].where = (Span)FIX_SPAN(fid, 1, 2, 2);
    edits[1].where = (Span)FIX_SPAN(fid, 1, 3, 1);
    diag_emit_with_fixits(dc, DIAG_NOTE, edits[0].where, WARN_NONE, edits, 2,
                          "conflict");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    T_ASSERT_EQ_INT(t, report.conflicts, 2);
    T_ASSERT_EQ_STR(t, report.first_conflict_path, path);
    T_ASSERT_EQ_INT(t, report.first_conflict_line, 1);
    T_ASSERT_EQ_INT(t, report.first_conflict_col, 2);
    T_ASSERT_EQ_INT(t, report.applied, 0);
    T_ASSERT_EQ_INT(t, access(fixed, F_OK), -1);
    arena_free_all(&a);
}

void test_diag_apply_adjacent_ranges_conflict(TestCtx *t)
{
    static const char src[] = "ab\n";
    Arena a;
    DiagCtx *dc;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    DiagFixit edits[2] = {{{0}, "A", true}, {{0}, "B", true}};
    DiagFixitApplyReport report;
    char path[128], fixed[144];
    u32 fid;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-adjacent-%ld.c",
             (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid = diag_add_file(dc, path, src, sizeof(src) - 1);
    edits[0].where = (Span)FIX_SPAN(fid, 1, 1, 1);
    edits[1].where = (Span)FIX_SPAN(fid, 1, 2, 1);
    diag_emit_with_fixits(dc, DIAG_NOTE, edits[0].where, WARN_NONE, edits, 2,
                          "adjacent conflict");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    T_ASSERT_EQ_INT(t, report.conflicts, 2);
    T_ASSERT_EQ_INT(t, report.applied, 0);
    T_ASSERT_EQ_INT(t, access(fixed, F_OK), -1);
    arena_free_all(&a);
}

void test_diag_apply_duplicate_physical_path_once(TestCtx *t)
{
    static const char src[] = "abc\n";
    Arena a;
    DiagCtx *dc;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    DiagFixit first = {{0}, "A", true}, second = {{0}, "C", true};
    DiagFixitApplyReport report;
    char path[128], fixed[144], got[32];
    u32 fid1, fid2;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-duplicate-%ld.c",
             (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid1 = diag_add_file(dc, path, src, sizeof(src) - 1);
    fid2 = diag_add_file(dc, path, src, sizeof(src) - 1);
    first.where = (Span)FIX_SPAN(fid1, 1, 1, 1);
    second.where = (Span)FIX_SPAN(fid2, 1, 3, 1);
    diag_emit_with_fixits(dc, DIAG_NOTE, first.where, WARN_NONE, &first, 1,
                          "first TU");
    diag_emit_with_fixits(dc, DIAG_NOTE, second.where, WARN_NONE, &second, 1,
                          "second TU");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    T_ASSERT_EQ_INT(t, report.applied, 2);
    T_ASSERT_EQ_INT(t, report.files_written, 1);
    read_whole_file(fixed, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, "AbC\n");
    unlink(fixed);
    arena_free_all(&a);
}

void test_diag_apply_alias_spellings_share_one_output(TestCtx *t)
{
    static const char src[] = "abc\n";
    Arena a;
    DiagCtx *dc;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    DiagFixit first = {{0}, "A", true}, second = {{0}, "C", true};
    DiagFixitApplyReport report;
    char path[128], alias[144], fixed[144], got[32];
    FILE *f;
    u32 fid1, fid2;

    snprintf(path, sizeof(path), "/tmp/cgf-diag-alias-%ld.c", (long)getpid());
    snprintf(alias, sizeof(alias), "/tmp/./cgf-diag-alias-%ld.c",
             (long)getpid());
    snprintf(fixed, sizeof(fixed), "%s.cgf-fixed", path);
    unlink(fixed);
    f = fopen(path, "wb");
    T_ASSERT(t, f != NULL);
    if (f) {
        fwrite(src, 1, sizeof(src) - 1, f);
        fclose(f);
    }
    arena_init(&a);
    dc = diag_ctx_new(&a);
    diag_set_sink(dc, sink);
    fid1 = diag_add_file(dc, path, src, sizeof(src) - 1);
    fid2 = diag_add_file(dc, alias, src, sizeof(src) - 1);
    first.where = (Span)FIX_SPAN(fid1, 1, 1, 1);
    second.where = (Span)FIX_SPAN(fid2, 1, 3, 1);
    diag_emit_with_fixits(dc, DIAG_NOTE, first.where, WARN_NONE, &first, 1,
                          "first spelling");
    diag_emit_with_fixits(dc, DIAG_NOTE, second.where, WARN_NONE, &second, 1,
                          "second spelling");
    T_ASSERT(t, diag_apply_fixits(dc, DIAG_FIXITS_ALL, NULL, NULL, &report));
    T_ASSERT_EQ_INT(t, report.applied, 2);
    T_ASSERT_EQ_INT(t, report.files_written, 1);
    read_whole_file(fixed, got, sizeof(got), t);
    T_ASSERT_EQ_STR(t, got, "AbC\n");
    unlink(fixed);
    unlink(path);
    arena_free_all(&a);
}

void test_diag_macro_fixit_is_advisory(TestCtx *t)
{
    static const char src[] = "M\n";
    Arena a;
    DiagCtx *dc;
    Capture cap = {0};
    DiagSink sink = {capture_sink, &cap};
    DiagFixit edit = {{0}, "replacement", true};
    u32 fid;

    arena_init(&a);
    dc = diag_ctx_new(&a);
    fid = diag_add_file(dc, "macro.c", src, sizeof(src) - 1);
    edit.where = (Span)FIX_SPAN(fid, 1, 1, 1);
    edit.where.origin = SPAN_ORIGIN_ANY_MACRO;
    diag_set_sink(dc, sink);
    diag_emit_with_fixits(dc, DIAG_NOTE, edit.where, WARN_NONE, &edit, 1,
                          "macro edit");
    T_ASSERT(t, !cap.last.fixit.machine_applicable);
    T_ASSERT(t, !diag_fixit_at(dc, 0)->machine_applicable);
    arena_free_all(&a);
}

void test_diag_interactive_requires_tty(TestCtx *t)
{
    Arena a;
    DiagCtx *dc;
    DiagFixitApplyReport report;
    FILE *input = tmpfile();

    arena_init(&a);
    dc = diag_ctx_new(&a);
    T_ASSERT(t, input != NULL);
    T_ASSERT(t, !diag_apply_fixits(dc, DIAG_FIXITS_INTERACTIVE, input, NULL,
                                   &report));
    T_ASSERT(t, report.non_tty);
    if (input)
        fclose(input);
    arena_free_all(&a);
}
