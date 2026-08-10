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
    size_t align;
    unsigned char *payload;
};

void arena_init(Arena *a)
{
    a->head = NULL;
    a->next_block_size = ARENA_FIRST_BLOCK;
    a->peak_bytes = 0;
    a->reserved_bytes = 0;
    a->requested_bytes = 0;
    a->block_count = 0;
}

static ArenaBlock *arena_new_block(Arena *a, size_t min_payload, size_t align)
{
    size_t cap = a->next_block_size;
    size_t block_align = align;
    size_t alloc_size;
    ArenaBlock *b;

    if (block_align < _Alignof(max_align_t))
        block_align = _Alignof(max_align_t);
    while (cap < min_payload)
        cap *= 2;
    alloc_size = (cap + block_align - 1) & ~(block_align - 1);
    b = cgf_xmalloc(sizeof(ArenaBlock));
    b->payload = aligned_alloc(block_align, alloc_size);
    if (!b->payload)
        CGF_ICE("out of memory allocating %zu aligned arena bytes", alloc_size);
    b->next = a->head;
    b->cap = alloc_size;
    b->used = 0;
    b->align = block_align;
    a->head = b;
    a->reserved_bytes += alloc_size;
    a->block_count++;
    if (a->reserved_bytes > a->peak_bytes)
        a->peak_bytes = a->reserved_bytes;
    /* Geometric growth caps the block count at O(log total). */
    if (a->next_block_size < cap)
        a->next_block_size = cap;
    a->next_block_size *= 2;
    return b;
}

void *arena_alloc(Arena *a, size_t size, size_t align)
{
    ArenaBlock *b = a->head;
    size_t aligned;

    if (align == 0 || (align & (align - 1)) != 0)
        CGF_ICE("arena_alloc: alignment %zu is not a power of two", align);
    a->requested_bytes += size;

    if (b && align <= b->align) {
        aligned = (b->used + align - 1) & ~(align - 1);
        if (aligned <= b->cap && size <= b->cap - aligned) {
            b->used = aligned + size;
            return b->payload + aligned;
        }
    }
    /* Room for worst-case alignment slack in a fresh block. */
    b = arena_new_block(a, size, align);
    b->used = size;
    return b->payload;
}

char *arena_strndup(Arena *a, const char *s, size_t n)
{
    char *p = arena_alloc(a, n + 1, 1);

    if (n) /* memcpy from NULL is UB even for 0 bytes */
        memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char *arena_strdup(Arena *a, const char *s)
{
    return arena_strndup(a, s, strlen(s));
}

ArenaStats arena_stats(const Arena *a)
{
    ArenaStats stats;

    stats.peak_bytes = a->peak_bytes;
    stats.reserved_bytes = a->reserved_bytes;
    stats.requested_bytes = a->requested_bytes;
    stats.block_count = a->block_count;
    return stats;
}

void arena_free_all(Arena *a)
{
    ArenaBlock *b = a->head;

    while (b) {
        ArenaBlock *next = b->next;
        free(b->payload);
        free(b);
        b = next;
    }
    a->head = NULL;
    a->next_block_size = ARENA_FIRST_BLOCK;
    a->peak_bytes = 0;
    a->reserved_bytes = 0;
    a->requested_bytes = 0;
    a->block_count = 0;
}
