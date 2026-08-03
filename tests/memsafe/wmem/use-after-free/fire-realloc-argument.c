// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void free(void *);

void fire_realloc_argument(unsigned long n)
{
    void *p = malloc(8);
    free(p);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    void *q = realloc(p, n);
    (void)q;
}
