// FLAGS: -fsyntax-only
// WARNING_EXPECTED: annotated cgf_borrows
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: ownership annotation is here
// mem-trace: contradicting body effect is here
#include <cgfried/memsafe.h>
void free(void *);

static void recursive_lie(void *p, int n) CGF_BORROWS(1);
// WARN_CHECK: mem-annotation-mismatch annotated cgf_borrows
static void recursive_lie(void *p, int n)
{
    if (n)
        recursive_lie(p, n - 1);
    else
        free(p);
}

static void wrapper(void *p, int n) CGF_BORROWS(1);
static void wrapper(void *p, int n)
{
    recursive_lie(p, n);
}
