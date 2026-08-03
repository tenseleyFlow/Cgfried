// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

void *fire_return(void)
{
    void *p = malloc(8);
    free(p);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    return p;
}
