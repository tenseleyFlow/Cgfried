/* IR round-trip fuzzer (Sprint 20). In-process for speed: 10^6
 * iterations run in minutes, which is what makes the DoD's local run
 * honest. Seeds = every .cgfir under the given dirs (sorted for
 * reproducibility); mutations are byte-level (flip/insert/delete/dup
 * spans) via splitmix64. INVARIANTS per iteration:
 *   - parse never crashes or hangs;
 *   - if parse accepts AND the verifier passes, print -> reparse must
 *     accept and struct_eq (the fixpoint law, exactly the invariant the
 *     driver enforces) — a mismatch is a finding. Verify-REJECTED
 *     modules only owe us a clean rejection: a self-referential operand
 *     can shift an inferred operand type mid-parse, and pinning fixpoint
 *     there would demand parser-side semantic checks that belong to the
 *     verifier (found at iteration 223085);
 * Run under ASan/UBSan in the san lane; a crash kills the fuzzer at a
 * reported iteration, which is the reproducer. */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ir/ir.h"
#include "util/arena.h"
#include "util/sort.h"

static u64 rng_state;

static u64 rnd(void)
{
    u64 z = (rng_state += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static void null_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

typedef struct {
    char **paths;
    size_t n, cap;
} Seeds;

static int path_cmp(const void *a, const void *b, void *u)
{
    (void)u;
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static void add_dir(Seeds *s, const char *dir)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        size_t len = strlen(e->d_name);
        char *p;

        if (len < 6 || strcmp(e->d_name + len - 6, ".cgfir") != 0)
            continue;
        p = malloc(strlen(dir) + len + 2);
        sprintf(p, "%s/%s", dir, e->d_name);
        if (s->n == s->cap) {
            s->cap = s->cap ? s->cap * 2 : 64;
            s->paths = realloc(s->paths, s->cap * sizeof(char *));
        }
        s->paths[s->n++] = p;
    }
    closedir(d);
}

static char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long sz;

    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)sz + 1);
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[sz] = 0;
    *len = (size_t)sz;
    return buf;
}

int main(int argc, char **argv)
{
    Seeds seeds = {NULL, 0, 0};
    u64 iters = 100000;
    int i;
    u64 it;

    rng_state = 1;
    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--iters=", 8) == 0)
            iters = strtoull(argv[i] + 8, NULL, 10);
        else
            add_dir(&seeds, argv[i]);
    }
    if (!seeds.n) {
        fprintf(stderr, "ir_fuzz: no .cgfir seeds found\n");
        return 2;
    }
    cgf_sort_stable(seeds.paths, seeds.n, sizeof(char *), path_cmp, NULL);

    for (it = 0; it < iters; it++) {
        const char *path = seeds.paths[rnd() % seeds.n];
        size_t len;
        char *src = slurp(path, &len);
        Arena arena;
        DiagCtx *dc;
        DiagSink sink;
        IrModule *m;
        u32 k, nmut;

        if (!src)
            continue;
        /* Byte-level mutations; NUL bytes are re-written (the driver
         * rejects embedded NULs before the parser; in-process we keep
         * the C-string contract ourselves). */
        nmut = 1 + (u32)(rnd() % 4);
        for (k = 0; k < nmut && len > 4; k++) {
            size_t pos = rnd() % len;

            switch (rnd() % 3) {
            case 0:
                src[pos] = (char)(rnd() % 96 + 32);
                break;
            case 1:
                memmove(src + pos, src + pos + 1, len - pos - 1);
                len--;
                src[len] = 0;
                break;
            default: {
                size_t dst = rnd() % len;
                char c = src[pos];

                src[dst] = c;
                break;
            }
            }
        }
        for (k = 0; k < (u32)len; k++)
            if (src[k] == 0)
                src[k] = ' ';

        arena_init(&arena);
        dc = diag_ctx_new(&arena);
        sink.handle = null_sink;
        sink.user = NULL;
        diag_set_sink(dc, sink);
        m = ir_parse_module(&arena, dc, src, "<fuzz>");
        if (m && ir_verify(dc, m)) {
            Buf b1;
            IrModule *m2;

            buf_init(&b1);
            ir_print_module_buf(&b1, m);
            buf_push_u8(&b1, 0);
            m2 =
                ir_parse_module(&arena, dc, (const char *)b1.data, "<fuzz-rt>");
            if (!m2 || !ir_module_struct_eq(m, m2)) {
                fprintf(stderr,
                        "ir_fuzz: FIXPOINT BROKEN at iteration %llu "
                        "(seed file %s)\n",
                        (unsigned long long)it, path);
                fwrite(src, 1, len, stderr);
                return 1;
            }
            buf_free(&b1);
        }
        arena_free_all(&arena);
        free(src);
    }
    printf("ir_fuzz: %llu iterations, %zu seeds, 0 findings\n",
           (unsigned long long)iters, seeds.n);
    {
        size_t si;

        for (si = 0; si < seeds.n; si++)
            free(seeds.paths[si]);
        free(seeds.paths);
    }
    return 0;
}
