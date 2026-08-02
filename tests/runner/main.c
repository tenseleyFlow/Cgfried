/* cgf-test — the bespoke corpus runner.
 * CLI: cgf-test [--profile <name>] [--filter <substr>] <dir-or-file>...
 * Env: CGF_TEST_CC (compiler under test, default build/cgfried),
 *      CGF_TEST_TIMEOUT (seconds), CGF_TEST_XFAIL_LEDGER (ledger path). */
#include <dirent.h>
#include <errno.h>
#include <regex.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "diag.h"
#include "directive.h"
#include "report.h"
#include "spawn.h"
#include "target.h"
#include "util/arena.h"
#include "util/sort.h"
#include "util/strmap.h"
#include "util/vec.h"

#define DEFAULT_TIMEOUT_SECS 10
#define DETAIL_CAP 512 /* bytes of child output echoed on failure */

typedef struct {
    char *path;  /* as discovered (relative to CWD) */
    char *suite; /* basename of containing dir */
    char *name;  /* file basename without extension */
} TestFile;

VEC_DECL(VecTest, TestFile);
VEC_DECL(VecStr, char *);

typedef struct {
    Arena arena;
    const char *profile;
    const char *filter;
    const char *cc;
    const char *target; /* current target name for selector matching */
    int default_timeout;
    Strmap ledger; /* known XF-ids */
    bool ledger_loaded;
    const char *ledger_path;
    Counts counts;
} Runner;

static char *aprintf(Arena *a, const char *fmt, ...)
{
    va_list ap, ap2;
    int need;
    char *s;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        CGF_ICE("aprintf: vsnprintf failed on format \"%s\"", fmt);
    s = arena_alloc(a, (size_t)need + 1, 1);
    vsnprintf(s, (size_t)need + 1, fmt, ap2);
    va_end(ap2);
    return s;
}

static char *read_file(Arena *a, const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    char *data;
    long sz;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (sz = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    data = arena_alloc(a, (size_t)sz + 1, 1);
    if (sz > 0 && fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        return NULL;
    }
    fclose(f);
    data[sz] = '\0';
    *out_len = (size_t)sz;
    return data;
}

static const char *path_basename(const char *path)
{
    const char *slash = strrchr(path, '/');

    return slash ? slash + 1 : path;
}

static char *make_suite_name(Arena *a, const char *path)
{
    const char *base = path_basename(path);
    size_t dirlen;
    const char *dir_start;

    if (base == path)
        return arena_strdup(a, "."); /* bare filename in CWD */
    /* path = <...>/<dir>/<base>; suite = <dir>. */
    dirlen = (size_t)(base - path) - 1; /* strip trailing '/' */
    dir_start = path;
    {
        size_t i;
        for (i = 0; i < dirlen; i++)
            if (path[i] == '/')
                dir_start = path + i + 1;
    }
    return arena_strndup(a, dir_start, dirlen - (size_t)(dir_start - path));
}

static char *make_test_name(Arena *a, const char *path)
{
    const char *base = path_basename(path);
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);

    return arena_strndup(a, base, len);
}

static bool has_test_ext(const char *name)
{
    const char *dot = strrchr(name, '.');

    return dot && (strcmp(dot, ".c") == 0 || strcmp(dot, ".s") == 0 ||
                   strcmp(dot, ".cgfir") == 0);
}

static int str_cmp_ctx(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Collects test files under path (sorted at every directory level: readdir
 * order is filesystem-dependent and would make output nondeterministic). */
static bool collect(Runner *r, const char *path, VecTest *out)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        fprintf(stderr, "cgf-test: error: no such path: %s\n", path);
        return false;
    }
    if (S_ISREG(st.st_mode)) {
        TestFile t;
        t.path = arena_strdup(&r->arena, path);
        t.suite = make_suite_name(&r->arena, path);
        t.name = make_test_name(&r->arena, path);
        VecTest_push(out, t);
        return true;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "cgf-test: error: not a file or directory: %s\n", path);
        return false;
    }
    {
        DIR *d = opendir(path);
        struct dirent *ent;
        VecStr names = {NULL, 0, 0};
        bool ok = true;
        size_t i;

        if (!d) {
            fprintf(stderr, "cgf-test: error: cannot open directory: %s\n",
                    path);
            return false;
        }
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] == '.')
                continue;
            VecStr_push(&names, arena_strdup(&r->arena, ent->d_name));
        }
        closedir(d);
        cgf_sort_stable(names.data, names.len, sizeof(char *), str_cmp_ctx,
                        NULL);

        for (i = 0; i < names.len && ok; i++) {
            size_t plen = strlen(path), nlen = strlen(names.data[i]);
            char *child = arena_alloc(&r->arena, plen + nlen + 2, 1);
            struct stat cst;

            memcpy(child, path, plen);
            child[plen] = '/';
            memcpy(child + plen + 1, names.data[i], nlen + 1);

            if (stat(child, &cst) != 0)
                continue; /* raced away; ignore */
            if (S_ISDIR(cst.st_mode))
                ok = collect(r, child, out);
            else if (S_ISREG(cst.st_mode) && has_test_ext(child))
                ok = collect(r, child, out);
        }
        VecStr_free(&names);
        return ok;
    }
}

static void load_ledger(Runner *r)
{
    size_t len, pos = 0;
    char *src;

    if (r->ledger_loaded)
        return;
    r->ledger_loaded = true;
    src = read_file(&r->arena, r->ledger_path, &len);
    if (!src)
        return; /* missing ledger caught per-XFAIL below */
    /* Rows look like: | XF-0001 | ... — scan for "| XF-" cells. */
    while (pos + 9 < len) {
        if (src[pos] == '|' && src[pos + 1] == ' ' &&
            memcmp(src + pos + 2, "XF-", 3) == 0) {
            size_t id_start = pos + 2, id_end = id_start + 3;
            while (id_end < len && src[id_end] >= '0' && src[id_end] <= '9')
                id_end++;
            if (id_end - id_start == 7)
                strmap_put(&r->ledger, src + id_start, 7, (void *)1);
            pos = id_end;
        } else {
            pos++;
        }
    }
}

/* Echoes up to DETAIL_CAP bytes of child output, indented, byte counts
 * exact — deterministic (no timing, no pids). */
static void print_detail(const char *label, const Buf *b)
{
    size_t show = b->len < DETAIL_CAP ? b->len : DETAIL_CAP;
    size_t start = 0, i;

    if (b->len == 0)
        return;
    printf("    %s (%zu bytes):\n", label, b->len);
    for (i = 0; i <= show; i++) {
        if (i == show || b->data[i] == '\n') {
            printf("    | ");
            fwrite(b->data + start, 1, i - start, stdout);
            printf("\n");
            start = i + 1;
            if (i == show)
                break;
        }
    }
    if (b->len > show)
        printf("    | ... (truncated)\n");
}

/* In-order CHECK matching: each CHECK must match a later stdout line than
 * the previous one; two CHECKs can never match the same line. Returns the
 * first unmatched CHECK, or NULL. */
static const Directive *match_checks(const DirectiveSet *ds, const Buf *out)
{
    size_t cursor = 0;
    size_t d;

    for (d = 0; d < ds->ndirs; d++) {
        const Directive *dir = &ds->dirs[d];
        bool matched = false;

        if (dir->kind != DIR_CHECK && dir->kind != DIR_IR_CHECK &&
            dir->kind != DIR_MIR_CHECK)
            continue; /* IR_CHECK-NOT is judged by find_forbidden */
        while (cursor < out->len && !matched) {
            size_t eol = cursor;
            while (eol < out->len && out->data[eol] != '\n')
                eol++;
            {
                size_t line_len = eol - cursor;
                size_t sub_len = strlen(dir->value);
                if (sub_len <= line_len) {
                    size_t k;
                    for (k = 0; k + sub_len <= line_len; k++) {
                        if (memcmp(out->data + cursor + k, dir->value,
                                   sub_len) == 0) {
                            matched = true;
                            break;
                        }
                    }
                }
            }
            cursor = eol + 1;
        }
        if (!matched)
            return dir;
    }
    return NULL;
}

static bool rendered_warning_line(const char *line, size_t len,
                                  const char **message, size_t *message_len,
                                  const char **flag, size_t *flag_len)
{
    static const char severity[] = ": warning: ";
    const char *sev = NULL;
    const char *suffix = NULL;
    size_t sev_pos = 0;
    size_t i;

    for (i = 0; i + sizeof(severity) - 1 <= len; i++) {
        if (memcmp(line + i, severity, sizeof(severity) - 1) == 0) {
            sev = line + i + sizeof(severity) - 1;
            sev_pos = i;
            break;
        }
    }
    if (!sev || sev_pos < 4)
        return false;
    if (!(sev_pos == 7 && memcmp(line, "cgfried", 7) == 0)) {
        /* A source diagnostic has path:line:column immediately before the
         * severity. Walk backward so ':' in a path remains harmless. */
        i = sev_pos;
        if (i == 0 || line[i - 1] < '0' || line[i - 1] > '9')
            return false;
        while (i > 0 && line[i - 1] >= '0' && line[i - 1] <= '9')
            i--;
        if (i == 0 || line[--i] != ':' || i == 0 || line[i - 1] < '0' ||
            line[i - 1] > '9')
            return false;
        while (i > 0 && line[i - 1] >= '0' && line[i - 1] <= '9')
            i--;
        if (i == 0 || line[i - 1] != ':' || i == 1)
            return false;
    }

    *message = sev;
    *message_len = len - (size_t)(sev - line);
    *flag = NULL;
    *flag_len = 0;
    if (len < 6 || line[len - 1] != ']')
        return true;
    for (i = (size_t)(sev - line); i + 4 < len; i++) {
        if (memcmp(line + i, " [-W", 4) == 0)
            suffix = line + i;
    }
    if (!suffix || suffix + 4 == line + len - 1)
        return true;
    *message = sev;
    *message_len = (size_t)(suffix - sev);
    *flag = suffix + 4;
    *flag_len = (size_t)((line + len - 1) - *flag);
    if ((*flag)[0] == '-' || (*flag)[0] == '=' ||
        (*flag)[*flag_len - 1] == '-' || (*flag)[*flag_len - 1] == '=') {
        *flag = NULL;
        *flag_len = 0;
        return true;
    }
    for (i = 0; i < *flag_len; i++) {
        char c = (*flag)[i];

        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
              c == '=')) {
            *flag = NULL;
            *flag_len = 0;
            break;
        }
    }
    return true;
}

static bool line_has_substr(const char *line, size_t len, const char *needle)
{
    size_t needle_len = strlen(needle);
    size_t i;

    if (needle_len > len)
        return false;
    for (i = 0; i + needle_len <= len; i++)
        if (memcmp(line + i, needle, needle_len) == 0)
            return true;
    return false;
}

static bool warning_location_matches(const char *line, size_t len,
                                     const char *path, u32 wanted_line)
{
    size_t path_len = strlen(path);
    size_t pos;
    u32 line_no = 0;

    if (path_len + 4 >= len || memcmp(line, path, path_len) != 0 ||
        line[path_len] != ':')
        return false;
    pos = path_len + 1;
    if (line[pos] < '0' || line[pos] > '9')
        return false;
    while (pos < len && line[pos] >= '0' && line[pos] <= '9') {
        line_no = line_no * 10 + (u32)(line[pos] - '0');
        pos++;
    }
    if (pos >= len || line[pos++] != ':' || line_no != wanted_line ||
        pos >= len || line[pos] < '0' || line[pos] > '9')
        return false;
    while (pos < len && line[pos] >= '0' && line[pos] <= '9')
        pos++;
    return pos < len && line[pos] == ':';
}

static bool check_warning_directives(const DirectiveSet *ds, const Buf *err,
                                     const char *path, const Directive **miss,
                                     size_t *actual_count, bool *bad_suffix)
{
    size_t pos = 0;
    size_t d;

    *miss = NULL;
    *actual_count = 0;
    *bad_suffix = false;
    while (pos < err->len) {
        size_t eol = pos;
        const char *line = (const char *)err->data + pos;
        const char *message;
        const char *flag;
        size_t message_len;
        size_t flag_len;

        while (eol < err->len && err->data[eol] != '\n')
            eol++;
        if (rendered_warning_line(line, eol - pos, &message, &message_len,
                                  &flag, &flag_len)) {
            (*actual_count)++;
            if (!flag)
                *bad_suffix = true;
        }
        pos = eol + 1;
    }

    if (ds->has_warn_count) {
        if (*bad_suffix)
            return false;
        if (*actual_count != (size_t)ds->warn_count)
            return false;
    }

    for (d = 0; d < ds->ndirs; d++) {
        const Directive *dir = &ds->dirs[d];
        bool found = false;

        if (dir->kind != DIR_WARN_CHECK)
            continue;
        pos = 0;
        while (pos < err->len && !found) {
            size_t eol = pos;
            const char *line = (const char *)err->data + pos;
            const char *message;
            const char *flag;
            size_t message_len;
            size_t flag_len;

            while (eol < err->len && err->data[eol] != '\n')
                eol++;
            if (rendered_warning_line(line, eol - pos, &message, &message_len,
                                      &flag, &flag_len) &&
                flag &&
                warning_location_matches(line, eol - pos, path,
                                         dir->line + 1) &&
                strlen(dir->warn_flag) == flag_len &&
                memcmp(flag, dir->warn_flag, flag_len) == 0 &&
                line_has_substr(message, message_len, dir->value))
                found = true;
            pos = eol + 1;
        }
        if (!found) {
            *miss = dir;
            return false;
        }
    }
    return true;
}

/* ASM_CHECK matcher (Sprint 25 rules): per-line substring after
 * whitespace normalization (runs of blanks -> one space), `{{...}}`
 * embedding POSIX ERE fragments, ordered (each check matches at or
 * after the previous match's line). Selector-carrying checks apply only
 * when the selector names the current target. */
static void norm_ws(const char *src, size_t len, char *dst, size_t cap)
{
    size_t i, o = 0;
    bool blank = false;

    for (i = 0; i < len && o + 1 < cap; i++) {
        char c = src[i];

        if (c == ' ' || c == '\t') {
            blank = true;
            continue;
        }
        if (blank && o)
            dst[o++] = ' ';
        blank = false;
        dst[o++] = c;
    }
    dst[o] = '\0';
}

/* Build an anchored-nowhere ERE from a normalized pattern: literal
 * segments escaped, {{...}} spliced verbatim. */
static bool asm_pattern_to_ere(const char *pat, char *ere, size_t cap)
{
    size_t o = 0;
    const char *p = pat;

    while (*p && o + 3 < cap) {
        if (p[0] == '{' && p[1] == '{') {
            const char *end = strstr(p + 2, "}}");

            if (!end)
                return false;
            if (o + (size_t)(end - p - 2) + 1 >= cap)
                return false;
            memcpy(ere + o, p + 2, (size_t)(end - p - 2));
            o += (size_t)(end - p - 2);
            p = end + 2;
            continue;
        }
        if (strchr("^.[$()|*+?{\\", *p))
            ere[o++] = '\\';
        ere[o++] = *p++;
    }
    if (*p)
        return false;
    ere[o] = '\0';
    return true;
}

static const Directive *match_asm_checks(const DirectiveSet *ds,
                                         const Buf *text, const char *target)
{
    size_t cursor = 0;
    size_t d;

    for (d = 0; d < ds->ndirs; d++) {
        const Directive *dir = &ds->dirs[d];
        bool matched = false;
        char pat[512], ere[1024];
        regex_t rx;

        if (dir->kind != DIR_ASM_CHECK)
            continue;
        if (dir->selector && !directive_selector_matches(dir->selector, target))
            continue;
        norm_ws(dir->value, strlen(dir->value), pat, sizeof(pat));
        if (!asm_pattern_to_ere(pat, ere, sizeof(ere)))
            return dir; /* malformed {{...}} reads as unmatched */
        if (regcomp(&rx, ere, REG_EXTENDED | REG_NOSUB) != 0)
            return dir;
        while (cursor < text->len && !matched) {
            size_t eol = cursor;
            char line[512];

            while (eol < text->len && text->data[eol] != '\n')
                eol++;
            norm_ws((const char *)text->data + cursor, eol - cursor, line,
                    sizeof(line));
            if (regexec(&rx, line, 0, NULL, 0) == 0)
                matched = true;
            cursor = eol + 1;
        }
        regfree(&rx);
        if (!matched)
            return dir;
    }
    return NULL;
}

static bool buf_contains(const Buf *b, const char *needle)
{
    size_t nlen = strlen(needle);
    size_t i;

    if (nlen == 0 || b->len < nlen)
        return false;
    for (i = 0; i + nlen <= b->len; i++)
        if (memcmp(b->data + i, needle, nlen) == 0)
            return true;
    return false;
}

/* IR_CHECK-NOT: the text must appear NOWHERE in the output. Returns the
 * first violated directive, or NULL. Order-independent by design —
 * an absence has no position. */
static const Directive *find_forbidden(const DirectiveSet *ds, const Buf *out)
{
    size_t d;

    for (d = 0; d < ds->ndirs; d++) {
        const Directive *dir = &ds->dirs[d];

        if (dir->kind != DIR_IR_CHECK_NOT)
            continue;
        if (buf_contains(out, dir->value))
            return dir;
    }
    return NULL;
}

static void mkdir_p2(const char *a, const char *b)
{
    if (mkdir(a, 0777) != 0 && errno != EEXIST)
        CGF_ICE("mkdir %s failed: %s", a, strerror(errno));
    if (mkdir(b, 0777) != 0 && errno != EEXIST)
        CGF_ICE("mkdir %s failed: %s", b, strerror(errno));
}

typedef enum { OUT_PASS, OUT_FAIL } Outcome;

typedef struct {
    bool valid;
    int exit_status;
    Buf stdout_buf;
} RuntimeObservation;

/* Applies // ENV: NAME=VALUE pairs around the compile spawn, restoring the
 * previous environment afterwards (single-threaded; setenv is safe here). */
typedef struct {
    char *name;
    char *old; /* NULL = was unset */
    bool had_old;
} SavedEnv;

static size_t env_apply(Runner *r, const DirectiveSet *ds, SavedEnv **out)
{
    size_t n = 0, i;
    SavedEnv *saved;

    for (i = 0; i < ds->ndirs; i++)
        if (ds->dirs[i].kind == DIR_ENV)
            n++;
    if (n == 0) {
        *out = NULL;
        return 0;
    }
    saved = arena_alloc(&r->arena, n * sizeof(SavedEnv), _Alignof(SavedEnv));
    n = 0;
    for (i = 0; i < ds->ndirs; i++) {
        const char *v;
        size_t eq;

        if (ds->dirs[i].kind != DIR_ENV)
            continue;
        v = ds->dirs[i].value;
        eq = strcspn(v, "=");
        saved[n].name = arena_strndup(&r->arena, v, eq);
        {
            const char *old = getenv(saved[n].name);
            saved[n].had_old = old != NULL;
            saved[n].old = old ? arena_strdup(&r->arena, old) : NULL;
        }
        setenv(saved[n].name, v + eq + 1, 1);
        n++;
    }
    *out = saved;
    return n;
}

static void env_restore(SavedEnv *saved, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        if (saved[i].had_old)
            setenv(saved[i].name, saved[i].old, 1);
        else
            unsetenv(saved[i].name);
    }
}

/* Compile(+run) pipeline; on failure, failure text has been printed.
 * With // FLAGS: containing -E the pipeline stops at the compile step and
 * CHECK/EXIT_CODE apply to the COMPILER's stdout/exit (pp fixtures). */
static Outcome run_pipeline(Runner *r, const TestFile *t,
                            const DirectiveSet *ds, const char *id, int timeout,
                            const char *opt_level, bool quiet,
                            RuntimeObservation *observation)
{
    char *binpath;
    char *spath;
    SpawnResult comp;
    Outcome out = OUT_FAIL;
    bool pp_mode = false;
    bool s_mode = false;
    SavedEnv *saved_env;
    size_t saved_n;

    if (observation) {
        observation->valid = false;
        observation->exit_status = 0;
        buf_init(&observation->stdout_buf);
    }

    binpath =
        aprintf(&r->arena, "build/test-work/%s_%s.bin", t->suite, t->name);
    spath = aprintf(&r->arena, "build/test-work/%s_%s.s", t->suite, t->name);

    /* .cgfir fixtures go through `cgf -emit-ir` (parse -> verify ->
     * reprint); the compiler's stdout is the result under test, matched
     * by IR_CHECK/CHECK in order. ERROR_EXPECTED asserts parse/verify
     * rejection exactly as it does for C fixtures. */
    {
        const char *dot = strrchr(t->path, '.');

        if (dot && strcmp(dot, ".cgfir") == 0) {
            char *argv[32];
            int n = 0;

            argv[n++] = (char *)r->cc;
            argv[n++] = (char *)"-emit-ir";
            if (ds->flags) {
                char *fl = arena_strdup(&r->arena, ds->flags);
                char *piece = fl;

                while (*piece) {
                    char *end = piece + strcspn(piece, " ");

                    if (*end)
                        *end++ = '\0';
                    if (*piece) {
                        if (n >= (opt_level ? 29 : 30)) {
                            if (!quiet)
                                printf("FAIL %s: too many FLAGS\n", id);
                            return OUT_FAIL;
                        }
                        argv[n++] = piece;
                    }
                    piece = end;
                }
            }
            if (opt_level)
                argv[n++] = (char *)opt_level;
            argv[n++] = (char *)t->path;
            argv[n] = NULL;
            saved_n = env_apply(r, ds, &saved_env);
            spawn_capture(argv, timeout, &comp);
            env_restore(saved_env, saved_n);
            pp_mode = true;
            goto have_compile;
        }
    }

    /* `// TU-BREAK` splits a fixture into several TRANSLATION UNITS,
     * each compiled separately with the same FLAGS. Outputs are merged in
     * order, so CHECK/ERROR_EXPECTED apply across the whole set; the
     * compile "exit code" is the first nonzero one. With no linker yet
     * this asserts per-TU behavior only — cross-TU questions (does the
     * external inline definition exist SOMEWHERE?) wait for Sprint 26,
     * which is why the directive lands with multi-TU sema fixtures rather
     * than later, mid-linker. */
    {
        size_t src_len;
        char *src = read_file(&r->arena, t->path, &src_len);

        if (src && strstr(src, "// TU-BREAK")) {
            SpawnResult merged;
            char *cursor = src;
            int tu = 0;
            int first_bad = 0;
            bool spawn_ok = true;

            memset(&merged, 0, sizeof(merged));
            merged.spawned = true;
            merged.exited = true;
            buf_init(&merged.out);
            buf_init(&merged.err);
            while (cursor && spawn_ok) {
                char *brk = strstr(cursor, "// TU-BREAK");
                size_t piece_len =
                    brk ? (size_t)(brk - cursor) : strlen(cursor);
                char *tupath =
                    aprintf(&r->arena, "build/test-work/%s_%s.tu%d.c", t->suite,
                            t->name, tu);
                FILE *tf = fopen(tupath, "wb");
                SpawnResult one;
                char *targv[8];
                int tn = 0;

                if (!tf)
                    break;
                fwrite(cursor, 1, piece_len, tf);
                fclose(tf);
                targv[tn++] = (char *)r->cc;
                targv[tn++] = (char *)"-fsyntax-only";
                targv[tn++] = tupath;
                targv[tn] = NULL;
                spawn_capture(targv, timeout, &one);
                spawn_ok = one.spawned;
                buf_append(&merged.out, one.out.data, one.out.len);
                buf_append(&merged.err, one.err.data, one.err.len);
                if (one.exited && one.exit_code != 0 && first_bad == 0)
                    first_bad = one.exit_code;
                spawn_result_free(&one);
                cursor = brk ? brk + strlen("// TU-BREAK") : NULL;
                tu++;
            }
            merged.spawned = spawn_ok;
            merged.exit_code = first_bad;
            comp = merged;
            /* The split TUs were compiled -fsyntax-only: there is no
             * binary to run, exactly like the dump modes. */
            pp_mode = true;
            goto have_compile;
        }
    }

    {
        char *argv[32];
        int n = 0;

        argv[n++] = (char *)r->cc;
        if (ds->flags) {
            char *fl = arena_strdup(&r->arena, ds->flags);
            char *piece = fl;
            while (*piece) {
                char *end = piece + strcspn(piece, " ");
                if (*end)
                    *end++ = '\0';
                if (*piece) {
                    /* Flags whose OUTPUT is the result under test (the
                     * compiler writes to stdout and produces no object):
                     * the pipeline stops after the compile step and
                     * CHECK/EXIT_CODE apply to the compiler itself. */
                    if (strcmp(piece, "-E") == 0 ||
                        strcmp(piece, "--dump-tokens") == 0 ||
                        strcmp(piece, "--dump-ast") == 0 ||
                        strcmp(piece, "-fdump-sema") == 0 ||
                        strcmp(piece, "-fdump-layout") == 0 ||
                        strcmp(piece, "-fdump-init") == 0 ||
                        strcmp(piece, "-fsyntax-only") == 0 ||
                        strcmp(piece, "-emit-ir") == 0 ||
                        strcmp(piece, "-emit-mir") == 0)
                        pp_mode = true;
                    /* -S writes a FILE; the runner reads it back as
                     * the result under test (ASM_CHECK matches it). */
                    if (strcmp(piece, "-S") == 0) {
                        pp_mode = true;
                        s_mode = true;
                    }
                    if (n >= (opt_level ? 27 : 28)) {
                        if (!quiet)
                            printf("FAIL %s: too many FLAGS\n", id);
                        return OUT_FAIL;
                    }
                    argv[n++] = piece;
                }
                piece = end;
            }
        }
        if (opt_level)
            argv[n++] = (char *)opt_level;
        argv[n++] = t->path;
        if (s_mode) {
            argv[n++] = (char *)"-o";
            argv[n++] = spath;
        } else if (!pp_mode) {
            argv[n++] = (char *)"-o";
            argv[n++] = binpath;
        }
        argv[n] = NULL;

        saved_n = env_apply(r, ds, &saved_env);
        spawn_capture(argv, timeout, &comp);
        env_restore(saved_env, saved_n);
    }

have_compile:
    if (!comp.spawned) {
        if (!quiet)
            printf("FAIL %s: could not spawn compiler '%s'\n", id, r->cc);
        spawn_result_free(&comp);
        return OUT_FAIL;
    }
    if (comp.timed_out) {
        if (!quiet)
            printf("FAIL %s: TIMEOUT after %ds (compile)\n", id, timeout);
        spawn_result_free(&comp);
        return OUT_FAIL;
    }

    if (ds->has_warn_count || ds->has_warn_check) {
        const Directive *miss;
        size_t actual_count;
        bool bad_suffix;

        if (!check_warning_directives(ds, &comp.err, t->path, &miss,
                                      &actual_count, &bad_suffix)) {
            if (!quiet) {
                if (miss) {
                    printf("FAIL %s: WARN_CHECK not satisfied (line %u): "
                           "-W%s %s\n",
                           id, (unsigned)miss->line, miss->warn_flag,
                           miss->value);
                } else if (bad_suffix) {
                    printf("FAIL %s: rendered warning lacks mandatory "
                           "[-W<flag>] suffix\n",
                           id);
                } else {
                    printf("FAIL %s: WARN_COUNT expected %d, got %zu\n", id,
                           ds->warn_count, actual_count);
                }
                print_detail("compiler stderr", &comp.err);
            }
            spawn_result_free(&comp);
            return OUT_FAIL;
        }
    }

    if (ds->has_warning_expected) {
        /* A warning must appear on stderr AND leave the exit code at 0 —
         * asserting both is the point, since a diagnostic that quietly
         * became an error is as much a regression as one that stopped
         * firing. */
        bool ok = comp.exited && comp.exit_code == 0;
        size_t d;

        for (d = 0; d < ds->ndirs && ok; d++) {
            if (ds->dirs[d].kind == DIR_WARNING_EXPECTED &&
                !buf_contains(&comp.err, ds->dirs[d].value))
                ok = false;
        }
        if (ok) {
            out = OUT_PASS;
        } else if (!quiet) {
            printf("FAIL %s: WARNING_EXPECTED not satisfied (compile exit ",
                   id);
            if (comp.exited)
                printf("%d)\n", comp.exit_code);
            else
                printf("signal %d)\n", comp.term_signal);
            print_detail("compiler stderr", &comp.err);
        }
        spawn_result_free(&comp);
        return out;
    }

    if (ds->has_error_expected) {
        /* Compile must exit 1 and stderr must carry every expected text —
         * a diagnostic that stops firing is a regression. */
        bool ok = comp.exited && comp.exit_code == 1;
        size_t d;

        for (d = 0; d < ds->ndirs && ok; d++) {
            if (ds->dirs[d].kind == DIR_ERROR_EXPECTED &&
                !buf_contains(&comp.err, ds->dirs[d].value))
                ok = false;
        }
        if (ok) {
            out = OUT_PASS;
        } else if (!quiet) {
            printf("FAIL %s: ERROR_EXPECTED not satisfied (compile exit ", id);
            if (comp.exited)
                printf("%d)\n", comp.exit_code);
            else
                printf("signal %d)\n", comp.term_signal);
            print_detail("compiler stderr", &comp.err);
        }
        spawn_result_free(&comp);
        return out;
    }

    if (pp_mode) {
        /* The compiler's own output IS the result under test. */
        if (!comp.exited) {
            if (!quiet)
                printf("FAIL %s: compiler killed by signal %d\n", id,
                       comp.term_signal);
        } else if (comp.exit_code != ds->exit_code) {
            if (!quiet) {
                printf("FAIL %s: compiler exit code %d, expected %d\n", id,
                       comp.exit_code, ds->exit_code);
                print_detail("compiler stderr", &comp.err);
            }
        } else {
            const Directive *miss;
            const Directive *hit;

            if (s_mode) {
                /* -S wrote a file: THAT text is the result under test. */
                size_t alen = 0;
                char *atext = read_file(&r->arena, spath, &alen);

                if (!atext) {
                    if (!quiet)
                        printf("FAIL %s: -S produced no output file\n", id);
                    spawn_result_free(&comp);
                    return OUT_FAIL;
                }
                buf_free(&comp.out);
                buf_init(&comp.out);
                buf_append(&comp.out, atext, alen);
            }
            miss = match_checks(ds, &comp.out);
            if (!miss && s_mode)
                miss = match_asm_checks(ds, &comp.out, r->target);
            hit = find_forbidden(ds, &comp.out);
            if (miss) {
                if (!quiet) {
                    printf("FAIL %s: CHECK not matched in order (line %u): "
                           "%s\n",
                           id, (unsigned)miss->line, miss->value);
                    print_detail("compiler stdout", &comp.out);
                }
            } else if (hit) {
                if (!quiet) {
                    printf("FAIL %s: IR_CHECK-NOT text present (line %u): "
                           "%s\n",
                           id, (unsigned)hit->line, hit->value);
                    print_detail("compiler stdout", &comp.out);
                }
            } else {
                out = OUT_PASS;
            }
        }
        spawn_result_free(&comp);
        return out;
    }

    if (!comp.exited || comp.exit_code != 0) {
        if (!quiet) {
            if (comp.exited)
                printf("FAIL %s: compile exited %d\n", id, comp.exit_code);
            else
                printf("FAIL %s: compiler killed by signal %d\n", id,
                       comp.term_signal);
            print_detail("compiler stderr", &comp.err);
        }
        spawn_result_free(&comp);
        return OUT_FAIL;
    }
    spawn_result_free(&comp);

    /* ASM_CHECK on an e2e fixture: a side-band `cc -S` supplies the
     * text (the binary run below still owns CHECK/EXIT_CODE). */
    {
        bool any_asm = false;
        size_t d;

        for (d = 0; d < ds->ndirs; d++)
            if (ds->dirs[d].kind == DIR_ASM_CHECK)
                any_asm = true;
        if (any_asm) {
            char *argv[7];
            int n = 0;
            SpawnResult sres;
            size_t alen = 0;
            char *atext;
            Buf abuf;

            argv[n++] = (char *)r->cc;
            argv[n++] = (char *)"-S";
            if (opt_level)
                argv[n++] = (char *)opt_level;
            argv[n++] = t->path;
            argv[n++] = (char *)"-o";
            argv[n++] = spath;
            argv[n] = NULL;
            spawn_capture(argv, timeout, &sres);
            atext = read_file(&r->arena, spath, &alen);
            spawn_result_free(&sres);
            if (!atext) {
                if (!quiet)
                    printf("FAIL %s: -S side-band produced no output\n", id);
                return OUT_FAIL;
            }
            buf_init(&abuf);
            buf_append(&abuf, atext, alen);
            {
                const Directive *amiss = match_asm_checks(ds, &abuf, r->target);

                if (amiss) {
                    if (!quiet) {
                        printf("FAIL %s: ASM_CHECK not matched (line "
                               "%u): %s\n",
                               id, (unsigned)amiss->line, amiss->value);
                        print_detail("assembly", &abuf);
                    }
                    buf_free(&abuf);
                    return OUT_FAIL;
                }
            }
            buf_free(&abuf);
        }
    }

    /* Run the produced binary. */
    {
        SpawnResult run;
        char *argv[2];

        argv[0] = binpath;
        argv[1] = NULL;
        spawn_capture(argv, timeout, &run);

        if (!run.spawned) {
            if (!quiet)
                printf("FAIL %s: could not run produced binary\n", id);
        } else if (run.timed_out) {
            if (!quiet)
                printf("FAIL %s: TIMEOUT after %ds\n", id, timeout);
        } else if (!run.exited) {
            if (!quiet)
                printf("FAIL %s: killed by signal %d\n", id, run.term_signal);
        } else if (run.exit_code != ds->exit_code) {
            if (!quiet) {
                printf("FAIL %s: exit code %d, expected %d\n", id,
                       run.exit_code, ds->exit_code);
                print_detail("stderr", &run.err);
            }
        } else {
            const Directive *miss = match_checks(ds, &run.out);
            const Directive *hit = find_forbidden(ds, &run.out);
            if (miss) {
                if (!quiet) {
                    printf("FAIL %s: CHECK not matched in order (line %u): "
                           "%s\n",
                           id, (unsigned)miss->line, miss->value);
                    print_detail("stdout", &run.out);
                }
            } else if (hit) {
                if (!quiet) {
                    printf("FAIL %s: IR_CHECK-NOT text present (line %u): "
                           "%s\n",
                           id, (unsigned)hit->line, hit->value);
                    print_detail("stdout", &run.out);
                }
            } else {
                out = OUT_PASS;
                if (observation) {
                    observation->valid = true;
                    observation->exit_status = run.exit_code;
                    buf_append(&observation->stdout_buf, run.out.data,
                               run.out.len);
                }
            }
        }
        spawn_result_free(&run);
    }
    return out;
}

static void run_test(Runner *r, const TestFile *t)
{
    char *id;
    size_t src_len;
    char *src;
    DirectiveSet ds;
    const char *xfail_id = NULL;
    int timeout;

    id = aprintf(&r->arena, "%s/%s", t->suite, t->name);
    if (r->filter && !strstr(id, r->filter))
        return;

    src = read_file(&r->arena, t->path, &src_len);
    if (!src) {
        printf("CONFIG ERROR %s\n    %s: unreadable\n", id, t->path);
        r->counts.config++;
        return;
    }
    directive_parse(&r->arena, src, src_len, &ds);

    /* Ledger validation for XFAIL ids (parse checked the format only). */
    {
        size_t d;
        for (d = 0; d < ds.ndirs; d++) {
            if (ds.dirs[d].kind != DIR_XFAIL)
                continue;
            load_ledger(r);
            if (!strmap_has(&r->ledger, ds.dirs[d].xf_id, 7)) {
                printf("CONFIG ERROR %s\n    %s:%u: XFAIL id '%s' is not in "
                       "the ledger (%s)\n",
                       id, t->path, (unsigned)ds.dirs[d].line, ds.dirs[d].xf_id,
                       r->ledger_path);
                r->counts.config++;
                return;
            }
        }
    }

    if (ds.nerrs > 0) {
        size_t e;
        printf("CONFIG ERROR %s\n", id);
        for (e = 0; e < ds.nerrs; e++)
            printf("    %s:%u: %s\n", t->path, (unsigned)ds.errs[e].line,
                   ds.errs[e].msg);
        r->counts.config++;
        return;
    }

    /* SKIP: never silent — the HARNESS_SKIP line is the record, and CI
     * asserts the exact expected set. count is the planned executions skipped,
     * so an OPT_EQ test contributes one per listed level. */
    {
        size_t d;
        for (d = 0; d < ds.ndirs; d++) {
            if (ds.dirs[d].kind == DIR_SKIP &&
                directive_selector_matches(ds.dirs[d].selector, r->target)) {
                printf(
                    "HARNESS_SKIP suite=%s test=%s count=%zu reason=\"%s\"\n",
                    t->suite, t->name, ds.nopt_levels ? ds.nopt_levels : 1,
                    ds.dirs[d].value);
                r->counts.skip++;
                return;
            }
        }
        for (d = 0; d < ds.ndirs; d++) {
            if (ds.dirs[d].kind == DIR_XFAIL &&
                directive_selector_matches(ds.dirs[d].selector, r->target)) {
                xfail_id = ds.dirs[d].xf_id;
                break;
            }
        }
    }

    timeout = ds.timeout ? ds.timeout : r->default_timeout;

    {
        /* Under XFAIL, failure detail is noise — suppress it (quiet). */
        Outcome out;

        if (ds.nopt_levels == 0) {
            out = run_pipeline(r, t, &ds, id, timeout, NULL, xfail_id != NULL,
                               NULL);
        } else {
            RuntimeObservation observations[6];
            size_t baseline_index = (size_t)-1;
            size_t nrun = 0;
            size_t level;

            memset(observations, 0, sizeof(observations));
            out = OUT_PASS;
            for (level = 0; level < ds.nopt_levels; level++) {
                const char *opt = ds.opt_levels[level];
                char *level_id = aprintf(&r->arena, "%s [%s]", id, opt);
                Outcome level_out =
                    run_pipeline(r, t, &ds, level_id, timeout, opt,
                                 xfail_id != NULL, &observations[level]);

                nrun = level + 1;

                if (level_out == OUT_FAIL) {
                    out = OUT_FAIL;
                    break;
                }
                if (!observations[level].valid) {
                    if (!xfail_id)
                        printf("FAIL %s: OPT_EQ requires an end-to-end "
                               "executable C test\n",
                               id);
                    out = OUT_FAIL;
                    break;
                }
                if (strcmp(opt, "-Ofast") != 0 && baseline_index == (size_t)-1)
                    baseline_index = level;
            }
            if (out == OUT_PASS) {
                const RuntimeObservation *baseline =
                    &observations[baseline_index];
                const char *baseline_opt = ds.opt_levels[baseline_index];

                for (level = 0; level < ds.nopt_levels; level++) {
                    const RuntimeObservation *current = &observations[level];
                    const char *opt = ds.opt_levels[level];
                    bool ofast_stdout_licensed =
                        strcmp(opt, "-Ofast") == 0 &&
                        ds.ofast_divergence_reason != NULL;

                    if (level == baseline_index)
                        continue;
                    if (baseline->exit_status != current->exit_status) {
                        if (!xfail_id)
                            printf("FAIL %s: OPT_EQ exit status mismatch: "
                                   "%s=%d vs %s=%d\n",
                                   id, baseline_opt, baseline->exit_status, opt,
                                   current->exit_status);
                        out = OUT_FAIL;
                    } else if (baseline->stdout_buf.len !=
                                   current->stdout_buf.len ||
                               (baseline->stdout_buf.len != 0 &&
                                memcmp(baseline->stdout_buf.data,
                                       current->stdout_buf.data,
                                       baseline->stdout_buf.len) != 0)) {
                        if (ofast_stdout_licensed)
                            continue;
                        if (!xfail_id) {
                            printf("FAIL %s: OPT_EQ stdout mismatch: %s vs "
                                   "%s\n",
                                   id, baseline_opt, opt);
                            print_detail(baseline_opt, &baseline->stdout_buf);
                            print_detail(opt, &current->stdout_buf);
                        }
                        out = OUT_FAIL;
                    }
                    if (out == OUT_FAIL)
                        break;
                }
            }
            for (level = 0; level < nrun; level++)
                buf_free(&observations[level].stdout_buf);
        }

        if (xfail_id) {
            if (out == OUT_FAIL) {
                printf("XFAIL %s (%s)\n", id, xfail_id);
                r->counts.xfail++;
            } else {
                printf("XPASS %s: expected failure (%s) PASSED — remove the "
                       "XFAIL and close the ledger entry\n",
                       id, xfail_id);
                r->counts.xpass++;
            }
        } else if (out == OUT_PASS) {
            printf("PASS %s\n", id);
            r->counts.pass++;
        } else {
            r->counts.fail++; /* failure text printed by the pipeline */
        }
    }
}

int main(int argc, char **argv)
{
    Runner r;
    VecTest tests = {NULL, 0, 0};
    int i;
    bool any_path = false;

    memset(&r, 0, sizeof(r));
    arena_init(&r.arena);
    strmap_init(&r.ledger);
    r.profile = "linux-x86_64";
    r.cc = getenv("CGF_TEST_CC") ? getenv("CGF_TEST_CC") : "build/cgfried";
    r.target = cgf_target_name(cgf_target_host());
    r.default_timeout = DEFAULT_TIMEOUT_SECS;
    r.ledger_path = getenv("CGF_TEST_XFAIL_LEDGER")
                        ? getenv("CGF_TEST_XFAIL_LEDGER")
                        : ".docs/audits/xfail-debt.md";
    {
        const char *env = getenv("CGF_TEST_TIMEOUT");
        if (env && env[0]) {
            int v = atoi(env);
            if (v > 0)
                r.default_timeout = v;
        }
    }

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) {
            r.profile = argv[++i];
        } else if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc) {
            r.filter = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "cgf-test: error: unknown option '%s'\n", argv[i]);
            return 1;
        } else {
            any_path = true;
            if (!collect(&r, argv[i], &tests))
                return 1;
        }
    }
    if (!any_path) {
        fprintf(stderr,
                "usage: cgf-test [--profile <name>] [--filter <substr>] "
                "<dir-or-file>...\n");
        return 1;
    }

    mkdir_p2("build", "build/test-work");

    for (i = 0; (size_t)i < tests.len; i++)
        run_test(&r, &tests.data[i]);

    report_summary(&r.counts, r.profile);
    VecTest_free(&tests);
    strmap_free(&r.ledger);
    arena_free_all(&r.arena);
    return counts_exit_code(&r.counts);
}
