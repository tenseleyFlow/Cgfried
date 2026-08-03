// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void free(void *);
int nofire_new_use(void)
{
    int *p = malloc(sizeof(int));
    int *q = realloc(p, 2 * sizeof(int));
    if (q) {
        int v = q[0];
        free(q);
        return v;
    }
    free(p);
    return 0;
}
