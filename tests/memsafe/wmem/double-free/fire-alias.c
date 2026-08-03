// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: freed here
void *malloc(unsigned long);
void free(void *);
void fire_alias(void)
{
    void *p = malloc(8);
    void *q = p;
    free(p);
    // WARN_CHECK: mem-double-free memory is freed more than once
    free(q);
}
