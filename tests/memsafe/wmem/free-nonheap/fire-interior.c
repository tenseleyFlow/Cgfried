// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void fire_interior(void)
{
    char *p = malloc(8);
    // WARN_CHECK: mem-free-nonheap free called
    free(p + 1);
}
