#include <stdint.h>

#include "unit.h"
#include "util/arena.h"
#include "util/ptrmap.h"

void test_ptrmap_scale_and_replace(TestCtx *t)
{
    enum { N = 10000 };
    static int keys[N + 1];
    Arena arena;
    Ptrmap map;
    int i;

    arena_init(&arena);
    ptrmap_init(&map, &arena);
    T_ASSERT(t, ptrmap_get(&map, &keys[0]) == NULL);
    for (i = 0; i < N; i++)
        T_ASSERT(t, ptrmap_put(&map, &keys[i], (void *)(uintptr_t)(i + 1)) ==
                        NULL);
    T_ASSERT_EQ_INT(t, ptrmap_len(&map), N);
    for (i = 0; i < N; i++)
        T_ASSERT(t, ptrmap_get(&map, &keys[i]) == (void *)(uintptr_t)(i + 1));
    T_ASSERT(t, ptrmap_get(&map, &keys[N]) == NULL);
    T_ASSERT(t, ptrmap_put(&map, &keys[17], (void *)(uintptr_t)99) ==
                    (void *)(uintptr_t)18);
    T_ASSERT(t, ptrmap_get(&map, &keys[17]) == (void *)(uintptr_t)99);
    T_ASSERT_EQ_INT(t, ptrmap_len(&map), N);
    arena_free_all(&arena);
}
