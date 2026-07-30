#include "util/arena.h"

#include <stdlib.h>
#include <string.h>

#include "diag.h"
#include "util/base.h"

#define ARENA_FIRST_BLOCK (64u * 1024u)

struct ArenaBlock {
    ArenaBlock *next;
    size_t cap;
    size_t used;
    unsigned char payload[];
};

void arena_init(Arena *a)
{
    a->head = NULL;
    a->next_block_size = ARENA_FIRST_BLOCK;
}

static ArenaBlock *arena_new_block(Arena *a, size_t min_payload)
{
    size_t cap = a->next_block_size;
    ArenaBlock *b;

    while (cap < min_payload)
        cap *= 2;
    b = cgf_xmalloc(sizeof(ArenaBlock) + cap);
    b->next = a->head;
    b->cap = cap;
    b->used = 0;
    a->head = b;
    /* Geometric growth caps the block count at O(log total). */
    if (a->next_block_size < cap)
        a->next_block_size = cap;
    a->next_block_size *= 2;
    return b;
}

void *arena_alloc(Arena *a, size_t size, size_t align)
{
    ArenaBlock *b = a->head;
    uintptr_t base, aligned;

    if (align == 0 || (align & (align - 1)) != 0)
        CGF_ICE("arena_alloc: alignment %zu is not a power of two", align);

    if (b) {
        base = (uintptr_t)b->payload + b->used;
        aligned = (base + align - 1) & ~(uintptr_t)(align - 1);
        if (aligned + size <= (uintptr_t)b->payload + b->cap) {
            b->used = (size_t)(aligned + size - (uintptr_t)b->payload);
            return (void *)aligned;
        }
    }
    /* Room for worst-case alignment slack in a fresh block. */
    b = arena_new_block(a, size + align);
    base = (uintptr_t)b->payload;
    aligned = (base + align - 1) & ~(uintptr_t)(align - 1);
    b->used = (size_t)(aligned + size - base);
    return (void *)aligned;
}

char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *p = arena_alloc(a, n + 1, 1);

    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *arena_strdup(Arena *a, const char *s)
{
    return arena_strndup(a, s, strlen(s));
}

void arena_free_all(Arena *a)
{
    ArenaBlock *b = a->head;

    while (b) {
        ArenaBlock *next = b->next;
        free(b);
        b = next;
    }
    a->head = NULL;
    a->next_block_size = ARENA_FIRST_BLOCK;
}
