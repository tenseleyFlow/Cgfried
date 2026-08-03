// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);
void free(void *);

static CGF_RETURNS_BORROWED(1) void *identity(void *p)
{
    return p;
}

void returns_borrowed_free_nofire(void)
{
    void *p = malloc(8);
    free(identity(p));
}
