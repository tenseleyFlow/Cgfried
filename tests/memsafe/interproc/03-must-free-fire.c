// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: ownership taken here
#include <cgfried/memsafe.h>
void *malloc(unsigned long);

static void consume(void *p) CGF_TAKES_OWNERSHIP(1);
static void consume(void *p)
{
    (void)p;
}

int annotated_must_free_fire(void)
{
    int *p = malloc(sizeof(int));
    consume(p);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    return *p;
}
