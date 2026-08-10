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

void test_arena_stats(TestCtx *t)
{
    Arena a;
    ArenaStats before, after;

    arena_init(&a);
    before = arena_stats(&a);
    T_ASSERT_EQ_INT(t, before.peak_bytes, 0);
    T_ASSERT_EQ_INT(t, before.block_count, 0);
    (void)arena_alloc(&a, 17, 1);
    (void)arena_alloc(&a, 33, 16);
    after = arena_stats(&a);
    T_ASSERT(t, after.peak_bytes >= 64u * 1024u);
    T_ASSERT_EQ_INT(t, after.reserved_bytes, after.peak_bytes);
    T_ASSERT_EQ_INT(t, after.requested_bytes, 50);
    T_ASSERT_EQ_INT(t, after.block_count, 1);
    arena_free_all(&a);
    after = arena_stats(&a);
    T_ASSERT_EQ_INT(t, after.peak_bytes, 0);
    T_ASSERT_EQ_INT(t, after.requested_bytes, 0);
}
