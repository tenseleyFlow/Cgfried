#include "util/dlev.h"

#include <string.h>

/* Damerau-Levenshtein with a cap. The cap is not an optimization detail:
 * a suggestion is only useful when it is CLOSE, and computing exact
 * distances for far-apart strings is both slow and pointless. Bailing as
 * soon as every cell in a row exceeds the cap turns the whole thing into
 * an O(n * cap) scan for the cases we care about.
 *
 * "Damerau" adds transposition — `flie` for `file` is one edit, not two —
 * which matters because transposition is one of the most common typos and
 * plain Levenshtein rates it the same as two unrelated substitutions. */

#define DLEV_MAX_LEN 128

unsigned dlev_distance(const char *a, size_t alen, const char *b, size_t blen,
                       unsigned cap)
{
    /* Two rows plus the one before them: transposition needs to look back
     * two rows, which is the only reason this is not a simple two-row
     * Levenshtein. */
    unsigned prev2[DLEV_MAX_LEN + 1];
    unsigned prev[DLEV_MAX_LEN + 1];
    unsigned cur[DLEV_MAX_LEN + 1];
    size_t i, j;

    if (alen > DLEV_MAX_LEN || blen > DLEV_MAX_LEN)
        return cap + 1; /* absurdly long identifiers are never suggestions */
    /* A length gap larger than the cap cannot be closed by any edits. */
    if (alen > blen ? alen - blen > cap : blen - alen > cap)
        return cap + 1;

    for (j = 0; j <= blen; j++)
        prev[j] = (unsigned)j;
    memset(prev2, 0, sizeof(prev2));

    for (i = 1; i <= alen; i++) {
        unsigned row_min;

        cur[0] = (unsigned)i;
        row_min = cur[0];
        for (j = 1; j <= blen; j++) {
            unsigned cost = a[i - 1] == b[j - 1] ? 0 : 1;
            unsigned del = prev[j] + 1;
            unsigned ins = cur[j - 1] + 1;
            unsigned sub = prev[j - 1] + cost;
            unsigned best = del < ins ? del : ins;

            if (sub < best)
                best = sub;
            /* Transposition: the two characters are swapped relative to
             * each other. */
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] &&
                a[i - 2] == b[j - 1]) {
                unsigned trans = prev2[j - 2] + 1;

                if (trans < best)
                    best = trans;
            }
            cur[j] = best;
            if (best < row_min)
                row_min = best;
        }
        if (row_min > cap)
            return cap + 1; /* nothing in this row can recover */
        memcpy(prev2, prev, (blen + 1) * sizeof(prev[0]));
        memcpy(prev, cur, (blen + 1) * sizeof(cur[0]));
    }
    return prev[blen];
}

bool dlev_is_suggestion(const char *typo, size_t tlen, const char *cand,
                        size_t clen, unsigned *out_distance)
{
    /* Short names need a tighter bound: at distance 2, `ab` reaches almost
     * every other two-letter name and the "suggestion" is noise. */
    unsigned cap = tlen < 4 ? 1 : 2;
    unsigned d;

    if (clen == 0 || tlen == 0)
        return false;
    if (tlen == clen && memcmp(typo, cand, tlen) == 0)
        return false; /* identical is not a suggestion */
    d = dlev_distance(typo, tlen, cand, clen, cap);
    if (d > cap)
        return false;
    if (out_distance)
        *out_distance = d;
    return true;
}
