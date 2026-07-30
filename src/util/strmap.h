#ifndef CGF_UTIL_STRMAP_H
#define CGF_UTIL_STRMAP_H

#include <stdbool.h>
#include <stddef.h>

#include "util/base.h"

/* String -> void* map that ITERATES IN INSERTION ORDER: entries live in an
 * append-only array, the hash table stores indices into it, and iteration
 * walks the array. This is non-negotiable, repo-wide: hash-order iteration
 * reaching any output (symbol, diagnostic, or section order) is the classic
 * nondeterminism bug and would break the byte-identical stage1 == stage2
 * bootstrap (Sprint 58). FNV-1a, no seed randomization — determinism beats
 * DoS-hardening on trusted input. Keys are copied and owned by the map. */

typedef struct {
    char *key;
    size_t key_len;
    void *val;
} StrmapEntry;

typedef struct {
    StrmapEntry *entries; /* insertion order, append-only */
    size_t len;
    size_t cap;
    u32 *slots;        /* hash table of entry-index + 1; 0 = empty */
    size_t slot_count; /* power of two */
} Strmap;

void strmap_init(Strmap *m);
/* Inserts or replaces; returns the previous value (NULL if new). A replaced
 * key keeps its original insertion position. */
void *strmap_put(Strmap *m, const char *key, size_t key_len, void *val);
void *strmap_get(const Strmap *m, const char *key, size_t key_len);
bool strmap_has(const Strmap *m, const char *key, size_t key_len);
size_t strmap_len(const Strmap *m);
void strmap_free(Strmap *m);

typedef struct {
    const Strmap *m;
    size_t i;
} StrmapIter;

StrmapIter strmap_iter(const Strmap *m);
/* Yields entries in put order; returns false when exhausted. */
bool strmap_iter_next(StrmapIter *it, const char **key, size_t *key_len,
                      void **val);

#endif
