// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void free(void *);
int fire_old_success(void)
{
    int *p = malloc(sizeof(int));
    int *q = realloc(p, 2 * sizeof(int));
    if (q) {
        free(q);
        // WARN_CHECK: mem-use-after-free use of memory after it was freed
        return *p;
    }
    free(p);
    return 0;
}
