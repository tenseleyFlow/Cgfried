// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: function returns on this path without releasing it
void *malloc(unsigned long);

static void observe(void *p)
{
    (void)p;
}

void direct_leak_fire(void)
{
    void *p = malloc(8);
    observe(p);
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
