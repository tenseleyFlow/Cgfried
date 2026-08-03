// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);

static void recurse(void *p, int n)
{
    if (n)
        recurse(p, n - 1);
}

void recursion_top_nofire(int n)
{
    void *p = malloc(8);
    recurse(p, n);
}
