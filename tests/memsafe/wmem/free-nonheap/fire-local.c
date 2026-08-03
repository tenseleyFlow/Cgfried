// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: pointer is proven to designate non-heap storage
void free(void *);

void fire_local(void)
{
    int local = 0;
    // WARN_CHECK: mem-free-nonheap free called
    free(&local);
}
