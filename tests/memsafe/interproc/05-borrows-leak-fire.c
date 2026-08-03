// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: function returns on this path without releasing it
#include <cgfried/memsafe.h>
void *malloc(unsigned long);

static void inspect(void *p) CGF_BORROWS(1);
static void inspect(void *p)
{
    (void)p;
}

void borrows_leak_fire(void)
{
    void *p = malloc(8);
    inspect(p);
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
