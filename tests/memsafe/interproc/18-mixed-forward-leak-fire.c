// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: function returns on this path without releasing it
void *malloc(unsigned long);

static void *mixed(void *arg, int fresh)
{
    if (fresh)
        return malloc(8);
    return arg;
}

static void *forward_mixed(void *arg, int fresh)
{
    return mixed(arg, fresh);
}

void mixed_forward_leak_fire(void *arg, int fresh)
{
    void *result = forward_mixed(arg, fresh);

    (void)result;
    // WARN_CHECK: mem-leak allocated memory is not released before this return
    return;
}
