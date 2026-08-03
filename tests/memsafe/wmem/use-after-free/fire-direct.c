// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
// mem-trace: freed here
void *malloc(unsigned long);
void free(void *);

int fire_direct(void)
{
    int *p = malloc(sizeof(int));
    free(p);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    return *p;
}
