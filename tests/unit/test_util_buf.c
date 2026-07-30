#include <string.h>

#include "unit.h"
#include "util/buf.h"

void test_buf_basics(TestCtx *t)
{
    Buf b;

    buf_init(&b);
    buf_append(&b, "ab", 2);
    buf_printf(&b, "%d-%s", 42, "x");
    buf_push_u8(&b, 0xFF);
    T_ASSERT_EQ_INT(t, b.len, 7);
    T_ASSERT(t, memcmp(b.data, "ab42-x\xff", 7) == 0);
    buf_free(&b);
    T_ASSERT(t, b.data == NULL && b.len == 0);
}
