// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
int fire_offset(void)
{
    char *p = malloc(8);
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    int v = p[5];
    free(p);
    return v;
}
