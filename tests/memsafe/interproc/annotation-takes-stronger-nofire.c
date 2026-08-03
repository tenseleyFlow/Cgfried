// FLAGS: -fsyntax-only -Werror=mem-annotation-mismatch
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>

static void consume_later(void *p) CGF_TAKES_OWNERSHIP(1);
static void consume_later(void *p)
{
    (void)p;
}
