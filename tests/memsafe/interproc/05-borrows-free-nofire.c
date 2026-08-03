// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);
void free(void *);

static void inspect(void *p) CGF_BORROWS(1);
static void inspect(void *p)
{
    (void)p;
}

void borrows_free_nofire(void)
{
    void *p = malloc(8);
    inspect(p);
    free(p);
}
