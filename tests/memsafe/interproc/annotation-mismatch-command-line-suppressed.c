// FLAGS: -fsyntax-only -Wno-mem-annotation-mismatch
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);

static void *suppressed(void *p) CGF_RETURNS_BORROWED(1);
static void *suppressed(void *p)
{
    (void)p;
    return malloc(8);
}
