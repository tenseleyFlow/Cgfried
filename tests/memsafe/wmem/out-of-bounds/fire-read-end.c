// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
int fire_read_end(void)
{
    char *p = malloc(4);
    // WARN_CHECK: mem-out-of-bounds outside the allocated object
    int v = p[4];
    free(p);
    return v;
}
