/* fuzz_frontend — dependency-free mutational fuzzer for pp + lex + parse.
 *
 * Same determinism law as ppfuzz: fixed integer seeds, our own PRNG (libc
 * rand is banned repo-wide), corpus paths sorted so a seed maps to the
 * same file on every machine. A finding reproduces from `--seed N` alone.
 *
 * It checks FIVE invariants per input, and the compiler is not asked to
 * cooperate with any of them — the harness decides:
 *
 *   1. no crash and no sanitizer report (a signal death is a finding);
 *   2. no hang (the spawn layer's cap; a timeout is a finding);
 *   3. exit code in {0, 1}, and a nonzero exit carries at least one
 *      diagnostic — "rejected silently" is as much a bug as a crash;
 *   4. every diagnostic Span is in bounds, enforced INSIDE the compiler
 *      under CGF_FUZZ=1 (which turns a bad span into an ICE, exit 4);
 *   5. -fmax-errors=20 is respected, which catches cap bugs and keeps a
 *      pathological input from producing megabytes of stderr.
 *
 * A finding is minimized by repeatedly deleting chunks while the failure
 * persists, then written to tests/fuzz/crashes/. The build fails while any
 * file sits there — a crash file is a bug that has not been fixed yet.
 *
 * Usage: fuzz_frontend [--iters N] [--seed S] [--hash] <cgfried> <dir>...
 *   --hash  print a digest of the mutation SEQUENCE and exit; CI pins it,
 *           so a change in mutator behavior cannot silently alter what a
 *           given seed tests.
 * Env: CGF_FUZZ_ITERS, CGF_FUZZ_SEED.
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

/* xorshift64* — the sprint's choice; two lines, no state beyond a u64, and
 * it never coincides with libc's sequence on any platform. */
typedef struct {
    u64 s;
} Prng;

static u64 prng_next(Prng *p)
{
    p->s ^= p->s >> 12;
    p->s ^= p->s << 25;
    p->s ^= p->s >> 27;
    return p->s * 0x2545F4914F6CDD1Dull;
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

/* Mutators aimed at the FRONT END rather than at bytes. Random noise
 * mostly produces lexer errors and stops there; swapping delimiters,
 * splicing keywords, and turning identifiers into type names is what
 * reaches the declarator machinery and the typedef ambiguity — the parts
 * with the interesting state. */
static const char *const kw_injections[] = {
    "typedef ", "struct ",       "union ",  "enum ",    "static ",  "const ",
    "sizeof ",  "_Generic",      "switch ", "case ",    "default:", "goto ",
    "return ",  "__attribute__", "typeof ", "_Alignof", "...",      "**",
};

/* Names that are typedefs in a typical corpus file; substituting an
 * identifier for one of these targets the Sprint 9 scope machinery, where
 * the same token stream means two different programs. */
static const char *const type_names[] = {"T", "size_t", "va_list", "u32"};

typedef enum {
    MUT_BYTE_FLIP,
    MUT_CHUNK_DELETE,
    MUT_CHUNK_DUP,
    MUT_CHUNK_SWAP,
    MUT_TRUNCATE,
    MUT_DELIM_SWAP,
    MUT_KEYWORD_SPLICE,
    MUT_FILE_SPLICE,
    MUT_IDENT_TO_TYPE,
    MUT_COUNT
} MutKind;

static void swap_delim(u8 *c)
{
    switch (*c) {
    case '(':
        *c = '{';
        break;
    case '{':
        *c = '(';
        break;
    case ')':
        *c = '}';
        break;
    case '}':
        *c = ')';
        break;
    case '[':
        *c = '(';
        break;
    case ']':
        *c = ')';
        break;
    case ';':
        *c = ',';
        break;
    case ',':
        *c = ';';
        break;
    default:
        break;
    }
}

/* Returns the mutator used, so --hash can digest the SEQUENCE of choices
 * rather than only the outputs. */
static MutKind mutate(Prng *rng, const Buf *in, const Buf *other, Buf *out)
{
    MutKind kind = (MutKind)prng_below(rng, MUT_COUNT);
    size_t n = in->len;
    size_t at, len2;

    buf_init(out);
    if (n == 0) {
        buf_append(out, "int x;\n", 7);
        return kind;
    }

    switch (kind) {
    case MUT_BYTE_FLIP: {
        u32 nflips = 1 + prng_below(rng, 8);
        u32 k;
        buf_append(out, in->data, n);
        for (k = 0; k < nflips; k++) {
            at = prng_below(rng, (u32)n);
            out->data[at] ^= (u8)(1u << prng_below(rng, 8));
        }
        break;
    }
    case MUT_CHUNK_DELETE:
        at = prng_below(rng, (u32)n);
        len2 = 1 + prng_below(rng, (u32)(n - at));
        buf_append(out, in->data, at);
        buf_append(out, in->data + at + len2, n - at - len2);
        break;
    case MUT_CHUNK_DUP:
        at = prng_below(rng, (u32)n);
        len2 = 1 + prng_below(rng, (u32)(n - at) > 64 ? 64 : (u32)(n - at));
        buf_append(out, in->data, at + len2);
        buf_append(out, in->data + at, len2);
        buf_append(out, in->data + at + len2, n - at - len2);
        break;
    case MUT_CHUNK_SWAP: {
        size_t a, b, l;
        a = prng_below(rng, (u32)n);
        b = prng_below(rng, (u32)n);
        l = 1 + prng_below(rng, 32);
        if (a > b) {
            size_t t = a;
            a = b;
            b = t;
        }
        if (b + l > n || a + l > b) {
            buf_append(out, in->data, n);
            break;
        }
        buf_append(out, in->data, a);
        buf_append(out, in->data + b, l);
        buf_append(out, in->data + a + l, b - a - l);
        buf_append(out, in->data + a, l);
        buf_append(out, in->data + b + l, n - b - l);
        break;
    }
    case MUT_TRUNCATE:
        /* Truncation is how "string literal spanning EOF" and "struct { at
         * EOF" are reached, and those are the classic recovery holes. */
        buf_append(out, in->data, 1 + prng_below(rng, (u32)n));
        break;
    case MUT_DELIM_SWAP: {
        u32 nswaps = 1 + prng_below(rng, 4);
        u32 k;
        buf_append(out, in->data, n);
        for (k = 0; k < nswaps; k++) {
            at = prng_below(rng, (u32)n);
            swap_delim(&out->data[at]);
        }
        break;
    }
    case MUT_KEYWORD_SPLICE: {
        const char *kw = kw_injections[prng_below(
            rng, (u32)(sizeof(kw_injections) / sizeof(kw_injections[0])))];
        at = prng_below(rng, (u32)n);
        buf_append(out, in->data, at);
        buf_append(out, kw, strlen(kw));
        buf_append(out, in->data + at, n - at);
        break;
    }
    case MUT_FILE_SPLICE:
        /* Two corpus files joined at a random point: the tail parses in a
         * context its author never wrote it for, which is exactly the
         * state the recovery paths must survive. */
        at = prng_below(rng, (u32)n);
        buf_append(out, in->data, at);
        if (other && other->len)
            buf_append(out, other->data + prng_below(rng, (u32)other->len), 1);
        if (other && other->len) {
            size_t from = prng_below(rng, (u32)other->len);
            buf_append(out, other->data + from, other->len - from);
        }
        break;
    case MUT_IDENT_TO_TYPE: {
        const char *ty = type_names[prng_below(
            rng, (u32)(sizeof(type_names) / sizeof(type_names[0])))];
        at = prng_below(rng, (u32)n);
        buf_append(out, in->data, at);
        buf_append(out, ty, strlen(ty));
        buf_append(out, " ", 1);
        buf_append(out, in->data + at, n - at);
        break;
    }
    case MUT_COUNT:
        break;
    }
    return kind;
}

/* FNV-1a over the mutation sequence: the --hash mode's digest. */
static u64 hash_step(u64 h, u64 v)
{
    int i;

    for (i = 0; i < 8; i++) {
        h ^= (v >> (i * 8)) & 0xff;
        h *= 0x100000001b3ull;
    }
    return h;
}

typedef struct {
    int exit_code;
    int term_signal;
    bool spawned;
    bool timed_out;
    u32 nerrors; /* "error:" occurrences on stderr */
    bool any_diag;
} RunResult;

/* Counts DIAGNOSTIC HEADERS, not the string "error:". The renderer echoes
 * the offending source line under every diagnostic, so an input that
 * happens to contain the text "error:" would otherwise inflate the count
 * and look like a cap leak — which is exactly what the first fuzz run
 * reported. A header is a line beginning at column 0 with either the
 * input path or "cgfried: ". */
static u32 count_error_headers(const Buf *b, const char *path)
{
    size_t plen = strlen(path);
    size_t i = 0;
    u32 count = 0;

    while (i < b->len) {
        size_t start = i;
        size_t end = start;

        while (end < b->len && b->data[end] != '\n')
            end++;
        {
            size_t linelen = end - start;
            const char *line = (const char *)b->data + start;
            bool header = (linelen > plen && memcmp(line, path, plen) == 0 &&
                           line[plen] == ':') ||
                          (linelen > 9 && memcmp(line, "cgfried: ", 9) == 0);

            if (header) {
                size_t k;
                for (k = 0; k + 7 <= linelen; k++)
                    if (memcmp(line + k, " error:", 7) == 0) {
                        count++;
                        break;
                    }
            }
        }
        i = end + 1;
    }
    return count;
}

static void run_frontend(const char *tool, const char *file, RunResult *rr)
{
    const char *argv[6];
    SpawnResult r;

    argv[0] = tool;
    argv[1] = "-fsyntax-only";
    argv[2] = "-fmax-errors=20";
    argv[3] = file;
    argv[4] = NULL;
    spawn_capture((char *const *)argv, 5, &r);

    memset(rr, 0, sizeof(*rr));
    rr->spawned = r.spawned;
    rr->timed_out = r.timed_out;
    rr->term_signal = r.exited ? 0 : r.term_signal;
    rr->exit_code = (r.spawned && r.exited) ? r.exit_code : -1;
    rr->nerrors = count_error_headers(&r.err, file);
    rr->any_diag = r.err.len > 0;
    spawn_result_free(&r);
}

/* Which invariant a run violated, or NULL if it is fine. */
static const char *violation(const RunResult *rr)
{
    if (!rr->spawned)
        return "spawn failed";
    if (rr->timed_out)
        return "hang (spawn timeout)";
    if (rr->term_signal != 0)
        return "killed by signal";
    if (rr->exit_code == 4)
        return "ICE (bad span or internal check)";
    if (rr->exit_code != 0 && rr->exit_code != 1)
        return "exit code outside {0,1}";
    if (rr->exit_code == 1 && !rr->any_diag)
        return "rejected the input without any diagnostic";
    /* -fmax-errors=20 was passed: 20 errors plus the "too many errors"
     * line is the ceiling. More means the cap leaked. */
    if (rr->nerrors > 21)
        return "-fmax-errors=20 not respected";
    return NULL;
}

static bool write_case(const char *path, const Buf *b)
{
    FILE *f = fopen(path, "wb");

    if (!f)
        return false;
    if (b->len)
        fwrite(b->data, 1, b->len, f);
    fclose(f);
    return true;
}

/* Delete-chunk minimization: halve the input repeatedly, keeping any cut
 * that preserves the failure. Crude, deterministic, and enough to turn a
 * 4 KB splice into something a human can read. */
static void minimize(const char *tool, const char *work, Buf *cur)
{
    size_t chunk = cur->len / 2;
    int rounds = 0;

    while (chunk > 0 && rounds < 200) {
        size_t at = 0;
        bool cut_any = false;

        while (at + chunk <= cur->len && rounds < 200) {
            Buf trial;
            RunResult rr;
            char path[600];

            buf_init(&trial);
            buf_append(&trial, cur->data, at);
            buf_append(&trial, cur->data + at + chunk, cur->len - at - chunk);
            snprintf(path, sizeof(path), "%s/min.c", work);
            rounds++;
            if (!write_case(path, &trial)) {
                buf_free(&trial);
                break;
            }
            run_frontend(tool, path, &rr);
            if (violation(&rr)) {
                buf_free(cur);
                *cur = trial;
                cut_any = true;
            } else {
                buf_free(&trial);
                at += chunk;
            }
        }
        if (!cut_any)
            chunk /= 2;
    }
}

int main(int argc, char **argv)
{
    Prng rng;
    StrVec seeds = {NULL, 0, 0};
    const char *cgf = NULL;
    bool hash_mode = false;
    u64 iters = 2000, seed0 = 1;
    u64 it;
    u64 digest = 0xcbf29ce484222325ull;
    int findings = 0;
    int i;
    char work[512];

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hash") == 0)
            hash_mode = true;
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

    /* readdir order is filesystem-dependent; sorting is what makes a seed
     * name the same input everywhere. */
    cgf_sort_stable(seeds.data, seeds.len, sizeof(char *), seed_path_cmp, NULL);
    if (!cgf || seeds.len == 0) {
        fprintf(stderr, "usage: fuzz_frontend [--iters N] [--seed S] "
                        "[--hash] <cgfried> <corpus-dir>...\n");
        return 1;
    }

    /* Pin the date macros for the child: __TIME__ would otherwise make an
     * input's behavior depend on the wall clock (findings F21). */
    setenv("SOURCE_DATE_EPOCH", "1000000000", 1);
    /* Turn on the compiler's internal span assertions — invariant 4. */
    setenv("CGF_FUZZ", "1", 1);

    snprintf(work, sizeof(work), "%s/fuzz-frontend", "build");
    mkdir("build", 0777);
    mkdir(work, 0777);

    for (it = 0; it < iters; it++) {
        u64 seed = seed0 + it;
        Buf src, other, mutated;
        char path[600];
        RunResult rr;
        const char *why;
        MutKind kind;
        u32 pick, pick2;

        rng.s = seed ? seed : 1; /* xorshift64* must never see zero state */
        pick = prng_below(&rng, (u32)seeds.len);
        pick2 = prng_below(&rng, (u32)seeds.len);
        if (!read_file(seeds.data[pick], &src))
            continue;
        if (!read_file(seeds.data[pick2], &other))
            buf_init(&other);
        kind = mutate(&rng, &src, &other, &mutated);
        buf_free(&src);
        buf_free(&other);

        if (hash_mode) {
            digest = hash_step(digest, (u64)kind);
            digest = hash_step(digest, (u64)mutated.len);
            digest = hash_step(digest, pick);
            buf_free(&mutated);
            continue;
        }

        snprintf(path, sizeof(path), "%s/case.c", work);
        if (!write_case(path, &mutated)) {
            buf_free(&mutated);
            continue;
        }

        run_frontend(cgf, path, &rr);
        why = violation(&rr);
        if (why) {
            char keep[700];

            minimize(cgf, work, &mutated);
            printf("FUZZ FINDING seed=%llu: %s\n", (unsigned long long)seed,
                   why);
            printf("  reproduce: build/fuzz_frontend --seed %llu --iters 1 "
                   "%s <corpus-dirs>\n",
                   (unsigned long long)seed, cgf);
            mkdir("tests/fuzz/crashes", 0777);
            snprintf(keep, sizeof(keep), "tests/fuzz/crashes/seed-%llu.c",
                     (unsigned long long)seed);
            write_case(keep, &mutated);
            findings++;
        }
        buf_free(&mutated);
    }

    if (hash_mode) {
        printf("fuzz_frontend: sequence digest %016llx (%llu iters from "
               "seed %llu)\n",
               (unsigned long long)digest, (unsigned long long)iters,
               (unsigned long long)seed0);
    } else {
        printf("fuzz_frontend: %llu iterations from seed %llu, %d findings\n",
               (unsigned long long)iters, (unsigned long long)seed0, findings);
    }
    for (i = 0; (size_t)i < seeds.len; i++)
        free(seeds.data[i]);
    StrVec_free(&seeds);
    return findings ? 1 : 0;
}
