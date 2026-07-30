/* cgf-ppdiff — the preprocessor differential harness.
 *
 * Compares `cgf -E -P` against gcc and clang at the TOKEN level. Textual
 * diffing is a false-positive factory: the three implementations lay out
 * whitespace and blank lines differently, all conforming. What IS semantic
 * is whitespace that keeps two tokens apart when their concatenation would
 * re-lex as one (`- -` vs `--`) — that case is compared.
 *
 * Oracles are discovered via CGF_DIFF_GCC / CGF_DIFF_CLANG (default "gcc"
 * and "clang"); a missing oracle emits a HARNESS_SKIP line with a computed
 * count — never a silent pass.
 *
 * Usage: cgf-ppdiff [--std <std>] <cgfried> <file>...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "driver/toolchain.h"
#include "pp/pp.h"
#include "spawn.h"
#include "util/arena.h"
#include "util/intern.h"

typedef struct {
    Arena arena;
    Interner in;
    Preprocessor pp;
} LexCtx;

static void quiet_sink(void *user, const Diag *d, const DiagCtx *dc)
{
    (void)user;
    (void)d;
    (void)dc;
}

static void lexctx_init(LexCtx *c)
{
    DiagCtx *dc;
    DiagSink sink;

    memset(c, 0, sizeof(*c));
    arena_init(&c->arena);
    dc = diag_ctx_new(&c->arena);
    sink.handle = quiet_sink;
    sink.user = NULL;
    diag_set_sink(dc, sink);
    intern_init(&c->in, &c->arena);
    pp_init(&c->pp, &c->arena, dc, &c->in);
}

static void lexctx_free(LexCtx *c)
{
    intern_free(&c->in);
    pp_loc_free(&c->pp.loc);
    strmap_free(&c->pp.macros);
    arena_free_all(&c->arena);
}

VEC_DECL(TokVec, PpToken);

/* Tokenizes -E output with our own pp lexer (phase 3 only: the text is
 * already preprocessed, so no directives can appear). */
static u32 tokenize(LexCtx *c, const char *label, const char *text, size_t len,
                    TokVec *out)
{
    SourceFile *sf = pp_source_add_buffer(&c->pp, label, text, len);
    PpLexer lx;
    PpToken t;

    pp_lexer_init(&lx, &c->pp, sf);
    while (pp_lex_token(&lx, &t))
        TokVec_push(out, t);
    buf_free(&lx.scratch);
    return (u32)out->len;
}

static int run_capture(const char *const *argv, Buf *out, bool *spawned)
{
    SpawnResult r;
    int code;

    spawn_capture((char *const *)argv, 60, &r);
    *spawned = r.spawned;
    code = (r.spawned && r.exited) ? r.exit_code : -1;
    buf_init(out);
    buf_append(out, r.out.data, r.out.len);
    spawn_result_free(&r);
    return code;
}

int main(int argc, char **argv)
{
    const char *std_flag = "-std=c17";
    const char *incdir = NULL;
    const char *xfail_path = NULL;
    Buf xfail;
    const char *cgf = NULL;
    const char *gcc = cgf_env("CGF_DIFF_GCC") ? cgf_env("CGF_DIFF_GCC") : "gcc";
    const char *clang =
        cgf_env("CGF_DIFF_CLANG") ? cgf_env("CGF_DIFF_CLANG") : "clang";
    int i, files = 0, diffs = 0, xfailed = 0;
    int skipped_gcc = 0, skipped_clang = 0, compared = 0;

    buf_init(&xfail);
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--std") == 0 && i + 1 < argc) {
            std_flag = argv[++i];
        } else if (strcmp(argv[i], "-I") == 0 && i + 1 < argc) {
            incdir = argv[++i];
        } else if (strcmp(argv[i], "--xfail") == 0 && i + 1 < argc) {
            xfail_path = argv[++i];
        } else if (!cgf) {
            cgf = argv[i];
        } else {
            files++;
        }
    }
    if (!cgf || files == 0) {
        fprintf(stderr, "usage: cgf-ppdiff [--std <std>] [-I <dir>] "
                        "[--xfail <list>] <cgfried> <file>...\n");
        return 1;
    }
    if (xfail_path) {
        /* One basename + reason per line; every entry is tracked debt. */
        FILE *xf = fopen(xfail_path, "rb");
        char line[512];
        if (xf) {
            while (fgets(line, sizeof(line), xf))
                buf_append(&xfail, line, strlen(line));
            fclose(xf);
        }
    }

    for (i = 1; i < argc; i++) {
        const char *file;
        Buf ours, theirs;
        bool spawned;
        int oracle_n;
        const char *oracles[2];

        if (strcmp(argv[i], "--std") == 0) {
            i++;
            continue;
        }
        if (strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "--xfail") == 0) {
            i++;
            continue;
        }
        if (argv[i] == cgf)
            continue;
        file = argv[i];
        {
            /* xfail by basename: known divergence, tracked in the list. */
            const char *slash = strrchr(file, '/');
            const char *base = slash ? slash + 1 : file;
            size_t blen = strlen(base);
            const char *p = (const char *)xfail.data;
            size_t left = xfail.len;
            bool skip = false;
            while (left >= blen) {
                if (memcmp(p, base, blen) == 0 &&
                    (p == (const char *)xfail.data || p[-1] == '\n') &&
                    (p[blen] == ' ' || p[blen] == '\n')) {
                    skip = true;
                    break;
                }
                p++;
                left--;
            }
            if (skip) {
                xfailed++;
                continue;
            }
        }

        {
            const char *a[8];
            int an = 0;
            a[an++] = cgf;
            a[an++] = "-E";
            a[an++] = "-P";
            a[an++] = std_flag;
            if (incdir) {
                a[an++] = "-I";
                a[an++] = incdir;
            }
            a[an++] = file;
            a[an] = NULL;
            if (run_capture(a, &ours, &spawned) != 0 || !spawned) {
                printf("PPDIFF-ERROR %s: cgf -E failed\n", file);
                buf_free(&ours);
                diffs++;
                continue;
            }
        }

        oracles[0] = gcc;
        oracles[1] = clang;
        for (oracle_n = 0; oracle_n < 2; oracle_n++) {
            const char *a[9];
            int an = 0;
            LexCtx c;
            TokVec ta = {NULL, 0, 0}, tb = {NULL, 0, 0};
            u32 na, nb, k;
            bool mismatch = false;

            a[an++] = oracles[oracle_n];
            a[an++] = "-E";
            a[an++] = "-P";
            a[an++] = std_flag;
            if (incdir) {
                a[an++] = "-I";
                a[an++] = incdir;
            }
            a[an++] = file;
            a[an] = NULL;
            if (run_capture(a, &theirs, &spawned) != 0 || !spawned) {
                if (!spawned) {
                    if (oracle_n == 0)
                        skipped_gcc++;
                    else
                        skipped_clang++;
                } else {
                    /* Oracle rejected the input (e.g. #pragma dependency
                     * on a missing file): not our bug, not a comparison. */
                    printf("PPDIFF-ORACLE-ERROR %s (%s)\n", file,
                           oracles[oracle_n]);
                }
                buf_free(&theirs);
                continue;
            }

            lexctx_init(&c);
            na = tokenize(&c, "<ours>", (const char *)ours.data, ours.len, &ta);
            nb = tokenize(&c, "<theirs>", (const char *)theirs.data, theirs.len,
                          &tb);
            for (k = 0; k < na && k < nb; k++) {
                if (ta.data[k].kind != tb.data[k].kind ||
                    strcmp(ta.data[k].spelling, tb.data[k].spelling) != 0) {
                    printf("PPDIFF %s (%s): token %u differs: ours '%s' vs "
                           "'%s'\n",
                           file, oracles[oracle_n], (unsigned)k,
                           ta.data[k].spelling, tb.data[k].spelling);
                    mismatch = true;
                    break;
                }
                /* Semantic whitespace: if printing k and k+1 adjacently
                 * would re-lex as one token, the space must match. */
                if (k + 1 < na && k + 1 < nb) {
                    bool merge_ours = pp_tokens_would_merge(&c.pp, &ta.data[k],
                                                            &ta.data[k + 1]);
                    bool sp_ours = (ta.data[k + 1].flags & PPTOK_F_SPACE) != 0;
                    bool sp_theirs =
                        (tb.data[k + 1].flags & PPTOK_F_SPACE) != 0;
                    if (merge_ours && sp_ours != sp_theirs) {
                        printf("PPDIFF %s (%s): semantic spacing at token "
                               "%u ('%s' '%s')\n",
                               file, oracles[oracle_n], (unsigned)k,
                               ta.data[k].spelling, ta.data[k + 1].spelling);
                        mismatch = true;
                        break;
                    }
                }
            }
            if (!mismatch && na != nb) {
                printf("PPDIFF %s (%s): token count %u vs %u\n", file,
                       oracles[oracle_n], (unsigned)na, (unsigned)nb);
                mismatch = true;
            }
            if (mismatch)
                diffs++;
            else
                compared++;

            TokVec_free(&ta);
            TokVec_free(&tb);
            lexctx_free(&c);
            buf_free(&theirs);
        }
        buf_free(&ours);
    }

    /* Skips are never silent: computed counts, asserted by check_skips. */
    if (skipped_gcc)
        printf("HARNESS_SKIP suite=ppdiff test=gcc-oracle count=%d "
               "reason=\"gcc not found\"\n",
               skipped_gcc);
    if (skipped_clang)
        printf("HARNESS_SKIP suite=ppdiff test=clang-oracle count=%d "
               "reason=\"clang not found\"\n",
               skipped_clang);
    printf("ppdiff: %d comparisons clean, %d diffs, %d xfail (%s)\n", compared,
           diffs, xfailed, std_flag);
    buf_free(&xfail);
    return diffs ? 1 : 0;
}
