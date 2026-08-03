// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);
void free(void *);

static CGF_RETURNS_BORROWED(1) void *identity(void *p)
{
    return p;
}

int returns_borrowed_uaf_fire(void)
{
    int *p = malloc(sizeof(int));
    int *alias = identity(p);
    free(alias);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    return *p;
}
