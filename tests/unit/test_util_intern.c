#include <string.h>

#include "unit.h"
#include "util/arena.h"
#include "util/intern.h"

void test_intern_ids(TestCtx *t)
{
    Arena a;
    Interner in;
    u32 id_foo, id_bar, id_foo2;

    arena_init(&a);
    intern_init(&in, &a);
    id_foo = intern_cstr(&in, "foo");
    id_bar = intern_cstr(&in, "bar");
    id_foo2 = intern(&in, "foobar", 3); /* length-bounded: "foo" */
    T_ASSERT_EQ_INT(t, id_foo, 1);      /* id 0 is reserved invalid */
    T_ASSERT_EQ_INT(t, id_bar, 2);
    T_ASSERT_EQ_INT(t, id_foo2, id_foo);
    T_ASSERT_EQ_STR(t, intern_str(&in, id_foo), "foo");
    T_ASSERT_EQ_STR(t, intern_str(&in, id_bar), "bar");
    T_ASSERT_EQ_INT(t, intern_count(&in), 2);
    T_ASSERT_EQ_INT(t, intern_lookups(&in), 3);
    T_ASSERT_EQ_INT(t, intern_hits(&in), 1);
    intern_free(&in);
    arena_free_all(&a);
}
