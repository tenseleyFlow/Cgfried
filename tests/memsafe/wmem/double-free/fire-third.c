// FLAGS: -fsyntax-only
// WARN_COUNT: 2
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void fire_third(void)
{
    void *p = malloc(8);
    free(p);
    // WARN_CHECK: mem-double-free memory is freed more than once
    free(p);
    // WARN_CHECK: mem-double-free memory is freed more than once
    free(p);
}
