// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void free(void *);
void fire_vla(int n)
{
    int values[n];
    // WARN_CHECK: mem-free-nonheap free called
    free(values);
}
