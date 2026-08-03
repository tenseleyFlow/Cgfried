// FLAGS: -fsyntax-only
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmem-annotation-mismatch"
static void *suppressed(void *p) CGF_RETURNS_BORROWED(1);
static void *suppressed(void *p)
{
    (void)p;
    return malloc(8);
}
#pragma GCC diagnostic pop
