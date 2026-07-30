#include <stdint.h>
#include <string.h>

#include "unit.h"
#include "util/arena.h"

void test_arena_alignment(TestCtx *t)
{
    Arena a;
    void *p1, *p16, *p64;

    arena_init(&a);
    p1 = arena_alloc(&a, 1, 1);
    T_ASSERT(t, p1 != NULL);
    p16 = arena_alloc(&a, 32, 16);
    T_ASSERT(t, ((uintptr_t)p16 & 15u) == 0);
    /* Oversize allocation forces a fresh block and still aligns. */
    p64 = arena_alloc(&a, 200 * 1024, 64);
    T_ASSERT(t, ((uintptr_t)p64 & 63u) == 0);
    memset(p64, 0xAB, 200 * 1024);
    arena_free_all(&a);
    T_ASSERT(t, a.head == NULL);
}

void test_arena_strdup(TestCtx *t)
{
    Arena a;

    arena_init(&a);
    T_ASSERT_EQ_STR(t, arena_strdup(&a, "hello"), "hello");
    T_ASSERT_EQ_STR(t, arena_strndup(&a, "worldly", 5), "world");
    T_ASSERT_EQ_STR(t, arena_strndup(&a, "", 0), "");
    arena_free_all(&a);
}
