// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void free(void *);
int nofire_old_failure(void)
{
    int *p = malloc(sizeof(int));
    *p = 3;
    int *q = realloc(p, 2 * sizeof(int));
    if (!q) {
        int v = *p;
        free(p);
        return v;
    }
    free(q);
    return 0;
}
