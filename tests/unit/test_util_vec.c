#include "unit.h"
#include "util/vec.h"

VEC_DECL(VecInt, int);

void test_vec_growth(TestCtx *t)
{
    VecInt v = {NULL, 0, 0};
    int i;

    for (i = 0; i < 100; i++)
        VecInt_push(&v, i);
    T_ASSERT_EQ_INT(t, v.len, 100);
    T_ASSERT(t, v.cap >= 100); /* 8 -> 16 -> 32 -> 64 -> 128: >3 regrowths */
    for (i = 0; i < 100; i++)
        T_ASSERT_EQ_INT(t, v.data[i], i);
    VecInt_free(&v);
    T_ASSERT(t, v.data == NULL && v.len == 0 && v.cap == 0);
}
