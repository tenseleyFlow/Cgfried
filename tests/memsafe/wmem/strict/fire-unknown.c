// FLAGS: -fsyntax-only -Wmem-strict
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-flags: -Wmem-strict
// mem-trace: allocated here
// mem-trace: freed here
void *malloc(unsigned long);
void free(void *);
void unknown(void *);
void fire_unknown(void)
{
    void *p = malloc(8);
    free(p);
    // WARN_CHECK: mem-use-after-free-unknown passing freed memory
    unknown(p);
}
