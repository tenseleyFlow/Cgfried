// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
#include <cgfried/memsafe.h>
void *malloc(unsigned long);
void free(void *);

static CGF_RETURNS_OWNED void *make_buffer(void)
{
    return malloc(16);
}

void returns_owned_free_nofire(void)
{
    free(make_buffer());
}
