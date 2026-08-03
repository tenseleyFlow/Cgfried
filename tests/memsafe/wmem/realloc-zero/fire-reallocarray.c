// FLAGS: -fsyntax-only -Wmem-realloc-zero
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void *reallocarray(void *, unsigned long, unsigned long);
void fire_reallocarray(void)
{
    void *p = malloc(8);
    // WARN_CHECK: mem-realloc-zero implementation-defined
    p = reallocarray(p, 0, 4);
    (void)p;
}
