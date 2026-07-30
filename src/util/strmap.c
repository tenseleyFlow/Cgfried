#include "util/strmap.h"

#include <stdlib.h>
#include <string.h>

#define STRMAP_FIRST_SLOTS 64u

static u64 fnv1a(const char *s, size_t len)
{
    u64 h = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < len; i++) {
        h ^= (u8)s[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void strmap_init(Strmap *m)
{
    memset(m, 0, sizeof(*m));
}

/* Find the slot holding key, or the empty slot where it would go. */
static size_t probe(const Strmap *m, const char *key, size_t key_len)
{
    size_t mask = m->slot_count - 1;
    size_t i = (size_t)fnv1a(key, key_len) & mask;

    for (;;) {
        u32 idx = m->slots[i];
        if (idx == 0)
            return i;
        {
            const StrmapEntry *e = &m->entries[idx - 1];
            if (e->key_len == key_len && memcmp(e->key, key, key_len) == 0)
                return i;
        }
        i = (i + 1) & mask;
    }
}

static void grow_slots(Strmap *m)
{
    size_t new_count = m->slot_count ? m->slot_count * 2 : STRMAP_FIRST_SLOTS;
    size_t i;

    free(m->slots);
    m->slots = cgf_xmalloc(new_count * sizeof(u32));
    memset(m->slots, 0, new_count * sizeof(u32));
    m->slot_count = new_count;
    /* Reinsertion walks the insertion-order array, so slot layout is a pure
     * function of the put sequence — deterministic by construction. */
    for (i = 0; i < m->len; i++) {
        const StrmapEntry *e = &m->entries[i];
        m->slots[probe(m, e->key, e->key_len)] = (u32)(i + 1);
    }
}

void *strmap_put(Strmap *m, const char *key, size_t key_len, void *val)
{
    size_t slot;

    /* Grow at ~70% load. */
    if (m->slot_count == 0 || (m->len + 1) * 10 >= m->slot_count * 7)
        grow_slots(m);

    slot = probe(m, key, key_len);
    if (m->slots[slot] != 0) {
        StrmapEntry *e = &m->entries[m->slots[slot] - 1];
        void *old = e->val;
        e->val = val;
        return old;
    }

    if (m->len == m->cap) {
        m->cap = m->cap ? m->cap * 2 : 16;
        m->entries = cgf_xrealloc(m->entries, m->cap * sizeof(StrmapEntry));
    }
    {
        StrmapEntry *e = &m->entries[m->len];
        e->key = cgf_xmalloc(key_len + 1);
        memcpy(e->key, key, key_len);
        e->key[key_len] = '\0';
        e->key_len = key_len;
        e->val = val;
    }
    m->len++;
    m->slots[slot] = (u32)m->len;
    return NULL;
}

void *strmap_get(const Strmap *m, const char *key, size_t key_len)
{
    if (m->slot_count == 0)
        return NULL;
    {
        u32 idx = m->slots[probe(m, key, key_len)];
        return idx ? m->entries[idx - 1].val : NULL;
    }
}

bool strmap_has(const Strmap *m, const char *key, size_t key_len)
{
    if (m->slot_count == 0)
        return false;
    return m->slots[probe(m, key, key_len)] != 0;
}

size_t strmap_len(const Strmap *m)
{
    return m->len;
}

void strmap_free(Strmap *m)
{
    size_t i;

    for (i = 0; i < m->len; i++)
        free(m->entries[i].key);
    free(m->entries);
    free(m->slots);
    memset(m, 0, sizeof(*m));
}

StrmapIter strmap_iter(const Strmap *m)
{
    StrmapIter it = {m, 0};
    return it;
}

bool strmap_iter_next(StrmapIter *it, const char **key, size_t *key_len,
                      void **val)
{
    const StrmapEntry *e;

    if (it->i >= it->m->len)
        return false;
    e = &it->m->entries[it->i++];
    if (key)
        *key = e->key;
    if (key_len)
        *key_len = e->key_len;
    if (val)
        *val = e->val;
    return true;
}
