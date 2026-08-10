#ifndef CGF_UTIL_PTRMAP_H
#define CGF_UTIL_PTRMAP_H

#include <stddef.h>

#include "util/arena.h"

/* Arena-backed pointer map for interned identities. Keys are borrowed and
 * compared by identity; the table has no iteration API, so hash order can
 * never leak into compiler output. Growth leaves old slot arrays in the
 * arena, matching the phase-lifetime ownership of its users. */
typedef struct PtrmapSlot PtrmapSlot;

typedef struct {
    PtrmapSlot *slots;
    size_t len;
    size_t slot_count;
    Arena *arena;
} Ptrmap;

void ptrmap_init(Ptrmap *m, Arena *arena);
void *ptrmap_get(const Ptrmap *m, const void *key);
/* Inserts or replaces key and returns the previous value, or NULL. */
void *ptrmap_put(Ptrmap *m, const void *key, void *value);
size_t ptrmap_len(const Ptrmap *m);

#endif
