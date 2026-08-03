// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: function returns on this path without releasing it
void *malloc(unsigned long);

static void local_only(void *p)
{
    (void)p;
}

void local_only_fire(void)
{
    void *p = malloc(8);
    local_only(p);
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
