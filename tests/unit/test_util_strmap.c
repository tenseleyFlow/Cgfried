#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unit.h"
#include "util/strmap.h"

void test_strmap_insertion_order(TestCtx *t)
{
    Strmap m;
    char key[32];
    int i;
    enum { N = 10000 };

    strmap_init(&m);
    for (i = 0; i < N; i++) {
        int n = snprintf(key, sizeof(key), "key%d", i);
        T_ASSERT(t, strmap_put(&m, key, (size_t)n,
                               (void *)(uintptr_t)(i + 1)) == NULL);
    }
    T_ASSERT_EQ_INT(t, strmap_len(&m), N);
    for (i = 0; i < N; i++) {
        int n = snprintf(key, sizeof(key), "key%d", i);
        T_ASSERT(t,
                 strmap_get(&m, key, (size_t)n) == (void *)(uintptr_t)(i + 1));
    }
    T_ASSERT(t, strmap_get(&m, "absent", 6) == NULL);
    T_ASSERT(t, !strmap_has(&m, "absent", 6));

    /* THE determinism property: iteration is exact insertion order. */
    {
        StrmapIter it = strmap_iter(&m);
        const char *k;
        size_t klen;
        void *v;

        for (i = 0; strmap_iter_next(&it, &k, &klen, &v); i++) {
            int n = snprintf(key, sizeof(key), "key%d", i);
            T_ASSERT(t, (size_t)n == klen && memcmp(k, key, klen) == 0);
            T_ASSERT(t, v == (void *)(uintptr_t)(i + 1));
        }
        T_ASSERT_EQ_INT(t, i, N);
    }
    strmap_free(&m);
}

void test_strmap_overwrite_keeps_position(TestCtx *t)
{
    Strmap m;

    strmap_init(&m);
    T_ASSERT(t, strmap_put(&m, "a", 1, (void *)(uintptr_t)1) == NULL);
    T_ASSERT(t, strmap_put(&m, "b", 1, (void *)(uintptr_t)2) == NULL);
    /* Overwrite returns the old value and keeps insertion position. */
    T_ASSERT(t, strmap_put(&m, "a", 1, (void *)(uintptr_t)9) ==
                    (void *)(uintptr_t)1);
    T_ASSERT_EQ_INT(t, strmap_len(&m), 2);
    {
        StrmapIter it = strmap_iter(&m);
        const char *k;
        size_t klen;
        void *v;

        T_ASSERT(t, strmap_iter_next(&it, &k, &klen, &v));
        T_ASSERT(t, klen == 1 && k[0] == 'a');
        T_ASSERT(t, v == (void *)(uintptr_t)9);
        T_ASSERT(t, strmap_iter_next(&it, &k, &klen, &v));
        T_ASSERT(t, klen == 1 && k[0] == 'b');
        T_ASSERT(t, !strmap_iter_next(&it, &k, &klen, &v));
    }
    strmap_free(&m);
}
