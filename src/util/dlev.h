#ifndef CGF_UTIL_DLEV_H
#define CGF_UTIL_DLEV_H

#include <stdbool.h>
#include <stddef.h>

/* Damerau-Levenshtein edit distance, capped. Returns `cap + 1` for any
 * pair further apart than `cap` — the exact distance beyond the cap is
 * never interesting, and refusing to compute it is what keeps this cheap
 * enough to run against every visible symbol on an error path. */
unsigned dlev_distance(const char *a, size_t alen, const char *b, size_t blen,
                       unsigned cap);

/* True if `cand` is close enough to `typo` to be worth suggesting. Names
 * shorter than four characters use a tighter bound, because at distance 2
 * a two-letter name reaches nearly every other one and the suggestion
 * becomes noise. */
bool dlev_is_suggestion(const char *typo, size_t tlen, const char *cand,
                        size_t clen, unsigned *out_distance);

#endif
