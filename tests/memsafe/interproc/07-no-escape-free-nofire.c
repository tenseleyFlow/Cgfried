// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);
void free(void *);

static void transient(void *p) CGF_NO_ESCAPE(1);
static void transient(void *p)
{
    (void)p;
}

void no_escape_free_nofire(void)
{
    void *p = malloc(8);
    transient(p);
    free(p);
}
