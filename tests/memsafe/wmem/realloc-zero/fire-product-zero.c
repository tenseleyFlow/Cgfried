// FLAGS: -fsyntax-only -Wmem-realloc-zero
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void *reallocarray(void *, unsigned long, unsigned long);
void fire_product_zero(void)
{
    void *p = malloc(8);
    // WARN_CHECK: mem-realloc-zero implementation-defined
    p = reallocarray(p, 4, 0);
    (void)p;
}
