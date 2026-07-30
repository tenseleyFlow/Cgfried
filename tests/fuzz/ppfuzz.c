/* ppfuzz — dependency-free mutational fuzzer for the preprocessor.
 *
 * Determinism is the whole design: seeds are fixed integers, the PRNG is
 * splitmix64 (never libc rand — banned repo-wide), and a finding is
 * reproduced from its seed alone on a clean checkout. No wall clock enters
 * the output; the ONE timer is the child's hang cap, which never affects
 * what we print.
 *
 * Modes:
 *   (default)  crash/hang hunt: exit codes 0/1 are fine (compile error is
 *              a legitimate answer to garbage); >=2, a signal, or a
 *              timeout is a finding.
 *   -diff      differential: only inputs where BOTH cgf -E and gcc -E
 *              exit 0 are compared, token-normalized. Divergence = finding.
 *
 * Usage: ppfuzz [-diff] [--iters N] [--seed S] <cgfried> <corpus-dir>...
 * Env:   CGF_FUZZ_SEED, CGF_FUZZ_ITERS.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "driver/toolchain.h"
#include "spawn.h"
#include "util/base.h"
#include "util/buf.h"
#include "util/sort.h"
#include "util/vec.h"

typedef struct {
    u64 s;
} Prng;

static u64 prng_next(Prng *p)
{
    u64 z = (p->s += 0x9E3779B97F4A7C15ull);

    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

static u32 prng_below(Prng *p, u32 n)
{
    return n ? (u32)(prng_next(p) % n) : 0;
}

VEC_DECL(StrVec, char *);

static void collect_seeds(const char *dir, StrVec *out)
{
    DIR *d = opendir(dir);
    struct dirent *e;

    if (!d)
        return;
    while ((e = readdir(d)) != NULL) {
        char path[1024];
        struct stat st;

        if (e->d_name[0] == '.')
            continue;
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (stat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            collect_seeds(path, out);
        } else if (S_ISREG(st.st_mode)) {
            size_t n = strlen(e->d_name);
            if (n > 2 && strcmp(e->d_name + n - 2, ".c") == 0) {
                char *copy = cgf_xmalloc(strlen(path) + 1);
                memcpy(copy, path, strlen(path) + 1);
                StrVec_push(out, copy);
            }
        }
    }
    closedir(d);
}

static int seed_path_cmp(const void *a, const void *b, void *ctx)
{
    (void)ctx;
    return strcmp(*(char *const *)a, *(char *const *)b);
}

static bool read_file(const char *path, Buf *out)
{
    FILE *f = fopen(path, "rb");
    char chunk[8192];
    size_t n;

    if (!f)
        return false;
    buf_init(out);
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0)
        buf_append(out, chunk, n);
    fclose(f);
    return true;
}

/* Mutators. Each is deliberately PP-aware: random byte noise mostly makes
 * uninteresting garbage, while splicing directives, unbalancing parens,
 * and injecting paste operators hits the machinery we care about. */
static void mutate(Prng *rng, const Buf *in, Buf *out)
{
    static const char *const injections[] = {
        "##",
        "#",
        "_Pragma(\"once\")",
        "__VA_ARGS__",
        "\\\n",
        "defined",
        "#define X(",
        ")",
        "(",
        "#include <no_such_header_xyz.h>",
        "#if",
        "#endif",
        "#else",
        "__LINE__",
        "__FILE__",
        "#undef",
    };
    u32 op = prng_below(rng, 7);
    size_t len = in->len;

    buf_init(out);
    if (len == 0) {
        buf_append(out, "int x;\n", 7);
        return;
    }

    switch (op) {
    case 0: { /* byte flip */
        size_t pos = prng_below(rng, (u32)len);
        buf_append(out, in->data, len);
        out->data[pos] = (u8)(prng_next(rng) & 0x7F);
        break;
    }
    case 1: { /* delete a span */
        size_t a = prng_below(rng, (u32)len);
        size_t b = a + 1 + prng_below(rng, 64);
        if (b > len)
            b = len;
        buf_append(out, in->data, a);
        buf_append(out, in->data + b, len - b);
        break;
    }
    case 2: { /* duplicate a span (rescan/recursion pressure) */
        size_t a = prng_below(rng, (u32)len);
        size_t b = a + 1 + prng_below(rng, 128);
        if (b > len)
            b = len;
        buf_append(out, in->data, b);
        buf_append(out, in->data + a, b - a);
        buf_append(out, in->data + b, len - b);
        break;
    }
    case 3: { /* inject a PP-significant token */
        size_t pos = prng_below(rng, (u32)len);
        const char *inj =
            injections[prng_below(rng, (u32)CGF_ARRAY_LEN(injections))];
        buf_append(out, in->data, pos);
        buf_append(out, inj, strlen(inj));
        buf_append(out, in->data + pos, len - pos);
        break;
    }
    case 4: { /* line shuffle: swap two lines (directive reordering) */
        size_t a = prng_below(rng, (u32)len), b = prng_below(rng, (u32)len);
        size_t ae = a, be = b;
        while (ae < len && in->data[ae] != '\n')
            ae++;
        while (be < len && in->data[be] != '\n')
            be++;
        buf_append(out, in->data, len);
        if (ae > a && be > b && a != b) {
            size_t n = (ae - a) < (be - b) ? (ae - a) : (be - b);
            size_t k;
            for (k = 0; k < n; k++) {
                u8 t = out->data[a + k];
                out->data[a + k] = out->data[b + k];
                out->data[b + k] = t;
            }
        }
        break;
    }
    case 5: { /* splice-backslash injection at a line end */
        size_t pos = prng_below(rng, (u32)len);
        while (pos < len && in->data[pos] != '\n')
            pos++;
        buf_append(out, in->data, pos);
        buf_append(out, "\\", 1);
        buf_append(out, in->data + pos, len - pos);
        break;
    }
    default: { /* truncate (unterminated everything) */
        size_t keep = prng_below(rng, (u32)len);
        buf_append(out, in->data, keep);
        break;
    }
    }
}

static bool buf_mentions(const Buf *b, const char *needle)
{
    size_t n = strlen(needle), i;

    if (b->len < n)
        return false;
    for (i = 0; i + n <= b->len; i++)
        if (memcmp(b->data + i, needle, n) == 0)
            return true;
    return false;
}

static int run_pp(const char *tool, const char *file, Buf *out, bool *ok,
                  bool *timed_out, int *sig)
{
    const char *argv[6];
    SpawnResult r;
    int code = -1;

    argv[0] = tool;
    argv[1] = "-E";
    argv[2] = "-P";
    argv[3] = file;
    argv[4] = NULL;
    spawn_capture((char *const *)argv, 5, &r); /* the one permitted timer */
    *ok = r.spawned;
    *timed_out = r.timed_out;
    *sig = r.exited ? 0 : r.term_signal;
    if (r.spawned && r.exited)
        code = r.exit_code;
    buf_init(out);
    buf_append(out, r.out.data, r.out.len);
    spawn_result_free(&r);
    return code;
}

/* Whitespace-collapsed comparison; the full token-level differ lives in
 * cgf-ppdiff (this is the cheap screen, findings get promoted there).
 *
 * Extended identifiers are folded to a single sentinel byte: gcc respells
 * them as UCNs (caf\U000000e9) while we pass raw UTF-8 through — findings
 * row F7, a representational difference, not a semantic one. Folding both
 * spellings keeps the fuzzer honest about everything else. */
static void normalize(const Buf *in, Buf *out)
{
    size_t i;
    bool sp = false;

    buf_init(out);
    for (i = 0; i < in->len; i++) {
        u8 c = in->data[i];

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            sp = true;
            continue;
        }
        /* Whitespace is dropped ENTIRELY, not collapsed: cosmetic spacing
         * in -E output is conforming and differs harmlessly between
         * implementations. SEMANTIC spacing (where adjacency would re-lex
         * as one token) is checked by cgf-ppdiff, which tokenizes both
         * sides — that is the gate; this fuzzer screens for content. */
        (void)sp;
        sp = false;
        if (c >= 0x80) { /* raw UTF-8 bytes of an extended identifier */
            if (!out->len || out->data[out->len - 1] != '?')
                buf_push_u8(out, '?'); /* collapse the multi-byte run */
            continue;
        }
        if (c == '\\' && i + 1 < in->len &&
            (in->data[i + 1] == 'U' || in->data[i + 1] == 'u')) {
            /* gcc's UCN respelling: consume \uXXXX / \UXXXXXXXX. */
            size_t want = in->data[i + 1] == 'u' ? 4 : 8, k = 0;
            size_t j = i + 2;
            while (k < want && j < in->len &&
                   ((in->data[j] >= '0' && in->data[j] <= '9') ||
                    (in->data[j] >= 'a' && in->data[j] <= 'f') ||
                    (in->data[j] >= 'A' && in->data[j] <= 'F'))) {
                j++;
                k++;
            }
            if (k == want) {
                if (!out->len || out->data[out->len - 1] != '?')
                    buf_push_u8(out, '?');
                i = j - 1;
                continue;
            }
        }
        buf_push_u8(out, c);
    }
}

int main(int argc, char **argv)
{
    Prng rng;
    StrVec seeds = {NULL, 0, 0};
    const char *cgf = NULL;
    bool diff_mode = false;
    u64 iters = 2000, seed0 = 1;
    u64 it;
    int findings = 0;
    int i;
    char work[512];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-diff") == 0)
            diff_mode = true;
        else if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc)
            iters = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed0 = strtoull(argv[++i], NULL, 10);
        else if (!cgf)
            cgf = argv[i];
        else
            collect_seeds(argv[i], &seeds);
    }
    if (cgf_env("CGF_FUZZ_ITERS"))
        iters = strtoull(cgf_env("CGF_FUZZ_ITERS"), NULL, 10);
    if (cgf_env("CGF_FUZZ_SEED"))
        seed0 = strtoull(cgf_env("CGF_FUZZ_SEED"), NULL, 10);
    /* readdir order is filesystem-dependent: sort so a seed always maps to
     * the same input file on every machine (determinism invariant — a
     * finding must reproduce from its seed alone). Caught when the ASan
     * lane produced different findings from identical seeds. */
    cgf_sort_stable(seeds.data, seeds.len, sizeof(char *), seed_path_cmp, NULL);
    if (!cgf || seeds.len == 0) {
        fprintf(stderr, "usage: ppfuzz [-diff] [--iters N] [--seed S] "
                        "<cgfried> <corpus-dir>...\n");
        return 1;
    }

    /* Pin __DATE__/__TIME__ for BOTH children: without an epoch they
     * use local wall-clock time (gcc parity, findings F21), so a second
     * ticking between our run and the oracle's is a false difference.
     * Inherited by every spawn. */
    setenv("SOURCE_DATE_EPOCH", "1000000000", 1);

    snprintf(work, sizeof(work), "%s/fuzz-work", "build");
    mkdir("build", 0777);
    mkdir(work, 0777);

    for (it = 0; it < iters; it++) {
        u64 seed = seed0 + it;
        Buf src, mutated, ours, theirs;
        char path[600];
        FILE *f;
        int code, sig;
        bool ok, timed_out;

        rng.s = seed;
        if (!read_file(seeds.data[prng_below(&rng, (u32)seeds.len)], &src))
            continue;
        mutate(&rng, &src, &mutated);
        buf_free(&src);

        snprintf(path, sizeof(path), "%s/case.c", work);
        f = fopen(path, "wb");
        if (!f) {
            buf_free(&mutated);
            continue;
        }
        if (mutated.len)
            fwrite(mutated.data, 1, mutated.len, f);
        fclose(f);

        code = run_pp(cgf, path, &ours, &ok, &timed_out, &sig);
        if (!ok || timed_out || sig != 0 || code > 1) {
            char keep[700];
            printf("FUZZ FINDING seed=%llu: %s\n", (unsigned long long)seed,
                   timed_out ? "hang (5s cap)"
                   : sig     ? "killed by signal"
                   : !ok     ? "spawn failed"
                             : "unexpected exit code");
            snprintf(keep, sizeof(keep), "tests/fuzz/findings/seed-%llu.c",
                     (unsigned long long)seed);
            mkdir("tests/fuzz/findings", 0777);
            {
                FILE *kf = fopen(keep, "wb");
                if (kf) {
                    if (mutated.len)
                        fwrite(mutated.data, 1, mutated.len, kf);
                    fclose(kf);
                }
            }
            findings++;
        } else if (diff_mode && code == 0 &&
                   !buf_mentions(&mutated, "__GNUC__")) {
            /* __GNUC__ is a DELIBERATE divergence (we do not define it
             * until Sprint 55 — see pp_predefine_all's policy comment), so
             * inputs testing it are not fuzz findings. */
            bool ok2, to2;
            int sig2;
            int gcode = run_pp("gcc", path, &theirs, &ok2, &to2, &sig2);

            if (ok2 && gcode == 0) {
                Buf na, nb;
                normalize(&ours, &na);
                normalize(&theirs, &nb);
                if (na.len != nb.len ||
                    (na.len && memcmp(na.data, nb.data, na.len) != 0)) {
                    printf("FUZZ DIFF seed=%llu\n", (unsigned long long)seed);
                    findings++;
                }
                buf_free(&na);
                buf_free(&nb);
            }
            buf_free(&theirs);
        }
        buf_free(&ours);
        buf_free(&mutated);
    }

    printf("ppfuzz: %llu iterations from seed %llu, %d findings (%s)\n",
           (unsigned long long)iters, (unsigned long long)seed0, findings,
           diff_mode ? "differential" : "crash/hang");
    for (i = 0; (size_t)i < seeds.len; i++)
        free(seeds.data[i]);
    StrVec_free(&seeds);
    return findings ? 1 : 0;
}
