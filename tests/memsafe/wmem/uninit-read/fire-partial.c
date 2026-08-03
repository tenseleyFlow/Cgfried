// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
int fire_partial(void)
{
    char *p = malloc(8);
    p[0] = 1;
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    int v = p[7];
    free(p);
    return v;
}
