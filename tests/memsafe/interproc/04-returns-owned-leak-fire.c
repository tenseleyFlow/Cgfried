// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: function returns on this path without releasing it
#include <cgfried/memsafe.h>
void *malloc(unsigned long);

static CGF_RETURNS_OWNED void *make_buffer(void)
{
    return malloc(16);
}

void returns_owned_leak_fire(void)
{
    void *p = make_buffer();
    (void)p;
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
