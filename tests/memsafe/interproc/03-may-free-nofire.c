// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

static void maybe_dispose(void *p, int yes)
{
    if (yes)
        free(p);
}

int inferred_may_free_nofire(int yes)
{
    int *p = malloc(sizeof(int));
    *p = 7;
    maybe_dispose(p, yes);
    return *p;
}
