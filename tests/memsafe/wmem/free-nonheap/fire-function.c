// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void free(void *);
static void target(void)
{
}
void fire_function(void)
{
    // WARN_CHECK: mem-free-nonheap free called
    free((void *)target);
}
