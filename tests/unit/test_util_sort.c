#include "unit.h"
#include "util/sort.h"

typedef struct {
    int key;
    int seq;
} Pair;

static int pair_cmp(const void *a, const void *b, void *ctx)
{
    const Pair *pa = a, *pb = b;

    (*(int *)ctx)++;
    return pa->key < pb->key ? -1 : pa->key > pb->key ? 1 : 0;
}

void test_sort_stability(TestCtx *t)
{
    Pair items[100];
    int i, cmp_calls = 0;

    for (i = 0; i < 100; i++) {
        items[i].key = i % 5;
        items[i].seq = i;
    }
    cgf_sort_stable(items, 100, sizeof(Pair), pair_cmp, &cmp_calls);
    T_ASSERT(t, cmp_calls > 0); /* ctx pointer really threads through */
    for (i = 1; i < 100; i++) {
        T_ASSERT(t, items[i - 1].key <= items[i].key);
        if (items[i - 1].key == items[i].key)
            T_ASSERT(t, items[i - 1].seq < items[i].seq); /* stability */
    }
}

void test_sort_degenerate(TestCtx *t)
{
    Pair items[2] = {{2, 0}, {1, 1}};
    int cmp_calls = 0;

    cgf_sort_stable(items, 0, sizeof(Pair), pair_cmp, &cmp_calls);
    cgf_sort_stable(items, 1, sizeof(Pair), pair_cmp, &cmp_calls);
    T_ASSERT_EQ_INT(t, cmp_calls, 0);
    cgf_sort_stable(items, 2, sizeof(Pair), pair_cmp, &cmp_calls);
    T_ASSERT_EQ_INT(t, items[0].key, 1);
    T_ASSERT_EQ_INT(t, items[1].key, 2);
}
