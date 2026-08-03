// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

static void dispose(void *p)
{
    free(p);
}

void helper_free_nofire(void)
{
    void *p = malloc(8);
    dispose(p);
}
