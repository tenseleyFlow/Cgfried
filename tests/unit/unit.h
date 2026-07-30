#ifndef CGF_TEST_UNIT_H
#define CGF_TEST_UNIT_H

#include <stddef.h>
#include <string.h>

/* Unit-test harness. Registration is an explicit generated table
 * (build/gen/unit_registry.c, from scripts/gen_unit_registry.sh) — strict
 * C11 forbids GNU constructor attributes. Test functions are
 * `void test_<name>(TestCtx *t)` at the start of a line in
 * tests/unit/test_*.c; the generator scans for exactly that shape. */

typedef struct TestCtx {
    const char *name;
    int failures;
    int assertions;
} TestCtx;

typedef struct {
    const char *name;
    void (*fn)(TestCtx *);
} UnitTest;

extern const UnitTest cgf_unit_tests[];
extern const size_t cgf_unit_test_count;

void t_fail(TestCtx *t, const char *file, int line, const char *fmt, ...);

#define T_ASSERT(t, cond)                                                      \
    do {                                                                       \
        (t)->assertions++;                                                     \
        if (!(cond))                                                           \
            t_fail(t, __FILE__, __LINE__, "assertion failed: %s", #cond);      \
    } while (0)

#define T_ASSERT_EQ_INT(t, a, b)                                               \
    do {                                                                       \
        long long t_x = (long long)(a), t_y = (long long)(b);                  \
        (t)->assertions++;                                                     \
        if (t_x != t_y)                                                        \
            t_fail(t, __FILE__, __LINE__, "%s == %s: %lld != %lld", #a, #b,    \
                   t_x, t_y);                                                  \
    } while (0)

#define T_ASSERT_EQ_STR(t, a, b)                                               \
    do {                                                                       \
        const char *t_x = (a), *t_y = (b);                                     \
        (t)->assertions++;                                                     \
        if (strcmp(t_x, t_y) != 0)                                             \
            t_fail(t, __FILE__, __LINE__, "%s == %s: \"%s\" != \"%s\"", #a,    \
                   #b, t_x, t_y);                                              \
    } while (0)

#endif
