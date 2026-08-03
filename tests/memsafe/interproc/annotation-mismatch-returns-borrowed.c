// FLAGS: -fsyntax-only
// WARNING_EXPECTED: annotated cgf_returns_borrowed
// WARN_COUNT: 1
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);

static void *fresh(void *p) CGF_RETURNS_BORROWED(1);
// WARN_CHECK: mem-annotation-mismatch annotated cgf_returns_borrowed
static void *fresh(void *p)
{
    (void)p;
    return malloc(8);
}
