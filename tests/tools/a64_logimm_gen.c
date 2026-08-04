#include <stdio.h>
#include <stdlib.h>

#include "cg/arm64/mir.h"

static u64 low_mask(unsigned bits)
{
    return bits == 64 ? ~(u64)0 : ((u64)1 << bits) - 1;
}

static u64 ror(u64 value, unsigned rotate, unsigned width)
{
    u64 mask = low_mask(width);

    rotate &= width - 1;
    value &= mask;
    if (!rotate)
        return value;
    return ((value >> rotate) | (value << (width - rotate))) & mask;
}

static u64 replicate(u64 elem, unsigned esize)
{
    u64 out = 0;
    unsigned bit;

    for (bit = 0; bit < 64; bit += esize)
        out |= elem << bit;
    return out;
}

static void fail(const char *why)
{
    fprintf(stderr, "a64_logimm_gen: %s\n", why);
    exit(1);
}

int main(int argc, char **argv)
{
    FILE *text, *encoded;
    unsigned esize, ones, rotate, count = 0;
    u64 seen[5334];
    int close_bad;

    if (argc != 3) {
        fprintf(stderr, "usage: a64_logimm_gen text.s encoded.s\n");
        return 2;
    }
    text = fopen(argv[1], "w");
    encoded = fopen(argv[2], "w");
    if (!text || !encoded)
        fail("cannot open output");
    if (fputs(".text\n.p2align 2\n_s47_logical_immediates:\n", text) == EOF ||
        fputs(".text\n.p2align 2\n_s47_logical_immediates_encoded:\n",
              encoded) == EOF)
        fail("write failed");

    for (esize = 2; esize <= 64; esize *= 2) {
        for (ones = 1; ones < esize; ones++) {
            for (rotate = 0; rotate < esize; rotate++) {
                u64 elem = ror(low_mask(ones), rotate, esize);
                u64 value = replicate(elem, esize);
                u32 packed, instruction;
                unsigned i;

                if (!a64_logical_imm_encode(value, 64, &packed))
                    fail("production encoder rejected a constructed pattern");
                for (i = 0; i < count; i++)
                    if (seen[i] == value)
                        fail("constructed patterns are not unique");
                seen[count] = value;
                instruction = 0x92000000u | packed << 10;
                /* afs-as currently parses unsuffixed integer tokens through
                 * i64, so high-bit patterns use their equivalent negative
                 * spelling.  Both assemblers encode the same u64 logical
                 * mask, and no pattern is skipped. */
                if (value <= 0x7fffffffffffffffull) {
                    if (fprintf(text, "    and x0, x0, #0x%016llx\n",
                                (unsigned long long)value) < 0)
                        fail("write failed");
                } else if (fprintf(text, "    and x0, x0, #-%llu\n",
                                   (unsigned long long)(~value + 1)) < 0) {
                    fail("write failed");
                }
                if (fprintf(encoded, "    .long 0x%08x\n", instruction) < 0)
                    fail("write failed");
                count++;
            }
        }
    }
    if (count != 5334)
        fail("constructed pattern count is not 5334");
    close_bad = fclose(text) != 0;
    close_bad |= fclose(encoded) != 0;
    if (close_bad)
        fail("close failed");
    printf("a64_logimm_gen: %u production-accepted patterns emitted\n", count);
    return 0;
}
