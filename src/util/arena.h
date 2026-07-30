#ifndef CGF_UTIL_ARENA_H
#define CGF_UTIL_ARENA_H

#include <stddef.h>

/* Phase-lifetime bump allocator: the compiler's allocation strategy is bulk
 * free at phase boundaries, never per-node free. */
typedef struct ArenaBlock ArenaBlock;

/* Tagged so headers that only pass Arena* (e.g. diag.h) can forward-declare
 * `struct Arena` without pulling this header in. */
typedef struct Arena {
    ArenaBlock *head;
    size_t next_block_size;
} Arena;

void arena_init(Arena *a);
/* align must be a power of two (ICE otherwise): a misaligned pointer load is
 * undefined behavior that detonates on arm64 even when x86 shrugs it off. */
void *arena_alloc(Arena *a, size_t size, size_t align);
char *arena_strdup(Arena *a, const char *s);
char *arena_strndup(Arena *a, const char *s, size_t n);
void arena_free_all(Arena *a);

#endif
