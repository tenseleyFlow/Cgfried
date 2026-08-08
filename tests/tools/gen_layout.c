/* gen-layout — the layout differential's generator.
 *
 * THE IDEA. We cannot ask gcc "what is your layout?" in any portable,
 * parseable way. But we CAN ask it to agree with ours: emit a random
 * struct definition followed by `_Static_assert` lines built from OUR
 * layout engine's numbers. If gcc accepts the file, gcc's layout equals
 * ours for that struct — a proof by the oracle's own constraint checker,
 * with no output format to parse and no version-specific dump to track.
 *
 * The generated file is checked by BOTH compilers: gcc proves we are
 * right, and cgfried proves our own folding agrees with our own layout
 * (which would otherwise be circular — so the assertions are emitted from
 * `-fdump-layout` output, not from the folding path they check).
 *
 * Determinism is the usual law: fixed integer seeds, our own PRNG, and a
 * finding reproduces from its seed alone.
 *
 * Usage: gen_layout --count N [--seed S] --out DIR
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "util/base.h"
#include "util/buf.h"

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

/* The scalar palette. `long double` is deliberately included: it is the
 * biggest cross-target divergence and the row most likely to be wrong. */
static const char *const scalars[] = {
    "char",
    "signed char",
    "unsigned char",
    "short",
    "unsigned short",
    "int",
    "unsigned int",
    "long",
    "unsigned long",
    "long long",
    "unsigned long long",
    "float",
    "double",
    "long double",
    "void *",
};

/* Types a bitfield may be declared with, and how many bits each holds. */
static const char *const bf_types[] = {
    "char", "unsigned char", "short", "unsigned short",
    "int",  "unsigned int",  "long",  "unsigned long"};
static const u32 bf_bits[] = {8, 8, 16, 16, 32, 32, 64, 64};

/* The one place this file spells the attribute: it is generated C TEXT rather
 * than a declaration of our own, which is what the ban is about. */
static const char ALIGNED_FMT[] =
    " __attribute__((__aligned__(%u)))"; /* check_bans allow */

static const char PACKED_SPELLING[] =
    " __attribute__((__packed__))"; /* check_bans allow */

/* `aligned(N)` on a member. N is drawn from BOTH sides of a typical natural
 * alignment on purpose: the rule that separates `aligned` from `_Alignas` is
 * that a weaker request is silently DECLINED, and a generator that only ever
 * asks for more never exercises the decline. */
static const char *member_aligned(Prng *rng, char *buf, size_t cap)
{
    static const unsigned pow2[] = {1, 2, 4, 8, 16};

    if (prng_below(rng, 6) != 0)
        return "";
    snprintf(buf, cap, ALIGNED_FMT, pow2[prng_below(rng, 5)]);
    return buf;
}

/* Member-level `packed` -- rule 4: the same rule applied to one member. It is
 * spelled with the underscored form because that is what real headers use. */
static const char *member_packed(Prng *rng)
{
    return prng_below(rng, 8) == 0 ? PACKED_SPELLING : "";
}

/* `no_bitfields` is set for a PACKED record. Rule 5 of
 * .docs/audits/packed-layout.md -- packed bitfields allocate with no
 * storage-unit alignment -- is not implemented, and the compiler refuses that
 * combination by name, so generating one would read as a lane failure rather
 * than as the honest refusal it is. */
static void emit_member(Prng *rng, Buf *b, u32 idx, u32 depth,
                        bool no_bitfields)
{
    u32 pick = prng_below(rng, 100);

    if (no_bitfields && pick >= 60 && pick < 90)
        pick = prng_below(rng, 60);
    if (pick < 45) {
        /* A plain scalar. */
        const char *ty =
            scalars[prng_below(rng, (u32)(sizeof(scalars) / sizeof(*scalars)))];

        {
            char ab[64];

            buf_printf(b, "  %s m%u%s%s;\n", ty, idx, member_packed(rng),
                       member_aligned(rng, ab, sizeof(ab)));
        }
    } else if (pick < 60) {
        /* An array — the stride check rides on these. */
        const char *ty =
            scalars[prng_below(rng, (u32)(sizeof(scalars) / sizeof(*scalars)))];

        {
            char ab[64];
            u32 n = 1 + prng_below(rng, 4);

            buf_printf(b, "  %s m%u[%u]%s%s;\n", ty, idx, n, member_packed(rng),
                       member_aligned(rng, ab, sizeof(ab)));
        }
    } else if (pick < 90) {
        /* A bitfield, occasionally unnamed or zero-width — the two forms
         * with their own rules (no alignment contribution, and forcing
         * the next field to a unit boundary). */
        u32 ti = prng_below(rng, (u32)(sizeof(bf_types) / sizeof(*bf_types)));
        u32 maxw = bf_bits[ti];
        u32 form = prng_below(rng, 10);

        if (form == 0) {
            buf_printf(b, "  %s :0;\n", bf_types[ti]);
        } else if (form == 1) {
            buf_printf(b, "  %s :%u;\n", bf_types[ti],
                       1 + prng_below(rng, maxw));
        } else {
            buf_printf(b, "  %s m%u:%u;\n", bf_types[ti], idx,
                       1 + prng_below(rng, maxw));
        }
    } else if (depth < 2) {
        /* A nested anonymous struct or union: layout nests normally even
         * though the member names flatten. */
        u32 n = 1 + prng_below(rng, 3);
        u32 k;

        buf_printf(b, "  struct {\n");
        for (k = 0; k < n; k++) {
            buf_printf(b, "  ");
            emit_member(rng, b, idx * 10 + k, depth + 1, no_bitfields);
        }
        /* A nested aggregate needs a named member for the same reason the
         * outer one does. */
        buf_printf(b, "    int inner%u;\n", idx);
        buf_printf(b, "  } n%u;\n", idx);
    } else {
        buf_printf(b, "  int m%u;\n", idx);
    }
}

int main(int argc, char **argv)
{
    Prng rng;
    u64 count = 500, seed0 = 1;
    const char *outdir = NULL;
    int i;
    u64 f;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--count") == 0 && i + 1 < argc)
            count = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
            seed0 = strtoull(argv[++i], NULL, 10);
        else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc)
            outdir = argv[++i];
    }
    if (!outdir) {
        fprintf(stderr, "usage: gen_layout --count N [--seed S] --out DIR\n");
        return 1;
    }
    mkdir(outdir, 0777);

    for (f = 0; f < count; f++) {
        Buf b;
        char path[1024];
        FILE *out;
        u32 nmembers;
        u32 k;
        bool is_union;
        bool packed;

        rng.s = seed0 + f;
        if (rng.s == 0)
            rng.s = 1;
        buf_init(&b);
        /* Every file is self-contained and header-free: the differential
         * must not depend on the host's headers being parseable by us. */
        buf_printf(&b, "/* generated by gen_layout, seed %llu */\n",
                   (unsigned long long)(seed0 + f));
        is_union = prng_below(&rng, 5) == 0;
        nmembers = 1 + prng_below(&rng, 5);
        packed = prng_below(&rng, 3) == 0;
        buf_printf(&b, "%s S {\n", is_union ? "union" : "struct");
        for (k = 0; k < nmembers; k++)
            emit_member(&rng, &b, k, 0, packed);
        /* At least one NAMED member, always: a struct made only of
         * unnamed bitfields is the GNU no-named-member extension (gcc
         * gives it size 0), not valid ISO C, and this differential is
         * about agreeing on VALID layouts. The extension has its own
         * fixture. */
        buf_printf(&b, "  int last_named;\n");
        {
            char ra[64];
            static const unsigned pow2[] = {1, 2, 4, 8, 16, 32};

            ra[0] = '\0';
            if (prng_below(&rng, 3) == 0)
                snprintf(ra, sizeof(ra), ALIGNED_FMT,
                         pow2[prng_below(&rng, 6)]);
            buf_printf(&b, "} %s%s;\n", packed ? PACKED_SPELLING : "", ra);
        }

        snprintf(path, sizeof(path), "%s/s%05llu.c", outdir,
                 (unsigned long long)f);
        out = fopen(path, "wb");
        if (!out) {
            fprintf(stderr, "gen_layout: cannot write %s\n", path);
            buf_free(&b);
            return 1;
        }
        fwrite(b.data, 1, b.len, out);
        fclose(out);
        buf_free(&b);
    }
    printf("gen_layout: wrote %llu files to %s (seed %llu)\n",
           (unsigned long long)count, outdir, (unsigned long long)seed0);
    return 0;
}
