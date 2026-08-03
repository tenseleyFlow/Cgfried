// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

int fire_nonnull_compare(void *other)
{
    void *p = malloc(8);
    free(p);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    return p == other;
}
