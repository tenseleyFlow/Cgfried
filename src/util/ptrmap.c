#include "util/ptrmap.h"

#include <stdint.h>
#include <string.h>

#include "diag.h"
#include "util/base.h"

#define PTRMAP_FIRST_SLOTS 8u

struct PtrmapSlot {
    const void *key;
    void *value;
};

static size_t pointer_hash(const void *key)
{
    u64 x = (u64)(uintptr_t)key;

    /* MurmurHash3's finalizer disperses the alignment-zero low bits common to
     * arena pointers. The layout is process-local and never iterated. */
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (size_t)x;
}

static size_t probe(const Ptrmap *m, const void *key)
{
    size_t mask = m->slot_count - 1;
    size_t slot = pointer_hash(key) & mask;

    while (m->slots[slot].key && m->slots[slot].key != key)
        slot = (slot + 1) & mask;
    return slot;
}

static void grow(Ptrmap *m)
{
    PtrmapSlot *old_slots = m->slots;
    size_t old_count = m->slot_count;
    size_t i;

    if (old_count > SIZE_MAX / 2)
        CGF_ICE("pointer map capacity overflow");
    m->slot_count = old_count ? old_count * 2 : PTRMAP_FIRST_SLOTS;
    if (m->slot_count > SIZE_MAX / sizeof(*m->slots))
        CGF_ICE("pointer map capacity overflow");
    m->slots = arena_alloc(m->arena, m->slot_count * sizeof(*m->slots),
                           _Alignof(PtrmapSlot));
    memset(m->slots, 0, m->slot_count * sizeof(*m->slots));
    for (i = 0; i < old_count; i++) {
        if (old_slots[i].key)
            m->slots[probe(m, old_slots[i].key)] = old_slots[i];
    }
}

void ptrmap_init(Ptrmap *m, Arena *arena)
{
    memset(m, 0, sizeof(*m));
    m->arena = arena;
}

void *ptrmap_get(const Ptrmap *m, const void *key)
{
    size_t slot;

    if (!key || m->slot_count == 0)
        return NULL;
    slot = probe(m, key);
    return m->slots[slot].key ? m->slots[slot].value : NULL;
}

void *ptrmap_put(Ptrmap *m, const void *key, void *value)
{
    size_t slot;

    if (!key)
        CGF_ICE("ptrmap_put: NULL key");
    if (!m->arena)
        CGF_ICE("ptrmap_put: uninitialized map");
    if (m->slot_count != 0) {
        slot = probe(m, key);
        if (m->slots[slot].key) {
            void *old = m->slots[slot].value;

            m->slots[slot].value = value;
            return old;
        }
    }
    /* Keep at least one quarter of the power-of-two table empty. Besides
     * bounding probe length, this comparison avoids overflow-prone ratio
     * multiplication at extreme capacities. */
    if (m->slot_count == 0 || m->len >= m->slot_count - m->slot_count / 4)
        grow(m);
    slot = probe(m, key);
    m->slots[slot].key = key;
    m->slots[slot].value = value;
    m->len++;
    return NULL;
}

size_t ptrmap_len(const Ptrmap *m)
{
    return m->len;
}
