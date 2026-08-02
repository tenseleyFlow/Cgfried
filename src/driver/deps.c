#include <string.h>

#include "driver/deps.h"
#include "util/arena.h"

/* One prerequisite, gcc mkdeps-shaped: wrap BEFORE the item when the
 * space plus item would pass column 72; a continuation line starts with
 * one leading space (from the normal " %s" emission after the wrap). */
static size_t emit_prereq(Buf *out, size_t col, const char *s)
{
    size_t len = strlen(s);

    if (col + len + 1 > 72) {
        buf_printf(out, " \\\n");
        col = 0;
    }
    buf_printf(out, " %s", s);
    return col + len + 1;
}

void cgf_deps_write(Buf *out, const DriverArgs *a, struct Arena *ar,
                    const char *default_target, const Preprocessor *pp)
{
    VecStr prereqs = {0};
    size_t col = 0, i, j;

    for (i = 0; i < pp->nfiles; i++) {
        const SourceFile *sf = pp->files[i];
        bool dup = false;

        if (sf->path[0] == '<')
            continue; /* <command-line>, <built-in>, <stdin> */
        if (a->dep_mode == DEP_MM &&
            (sf->is_system || sf->system_from_line != 0))
            continue;
        for (j = 0; j < prereqs.len && !dup; j++)
            dup = strcmp(prereqs.data[j], sf->path) == 0;
        if (!dup)
            VecStr_push(&prereqs, sf->path);
    }
    for (i = 0; i < prereqs.len; i++)
        prereqs.data[i] = cgf_make_quote(ar, prereqs.data[i]);

    if (a->dep_targets.len) {
        /* -MT verbatim / -MQ pre-quoted at parse time, space-joined. */
        for (i = 0; i < a->dep_targets.len; i++) {
            if (i) {
                buf_printf(out, " ");
                col++;
            }
            buf_printf(out, "%s", a->dep_targets.data[i]);
            col += strlen(a->dep_targets.data[i]);
        }
    } else {
        const char *t = cgf_make_quote(ar, default_target);

        buf_printf(out, "%s", t);
        col = strlen(t);
    }
    buf_printf(out, ":");
    col++;
    for (i = 0; i < prereqs.len; i++)
        col = emit_prereq(out, col, prereqs.data[i]);
    buf_printf(out, "\n");

    /* -MP: a phony (empty) rule per header — never for the source
     * itself — so a deleted header does not break the build. */
    if (a->dep_phony)
        for (i = 1; i < prereqs.len; i++)
            buf_printf(out, "%s:\n", prereqs.data[i]);
    VecStr_free(&prereqs);
}
