// FLAGS: -fsyntax-only -Wmem-realloc-zero
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-flags: -Wmem-realloc-zero
// mem-trace: reallocated here; success or failure is pending
// mem-trace: zero allocation size passed here
void *malloc(unsigned long);
void *realloc(void *, unsigned long);

void fire_opt_in(void)
{
    void *p = malloc(8);
    // WARN_CHECK: mem-realloc-zero implementation-defined
    p = realloc(p, 0);
    (void)p;
}
