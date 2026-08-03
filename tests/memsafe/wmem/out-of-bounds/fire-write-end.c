// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
void *malloc(unsigned long);
void free(void *);

void fire_write_end(void)
{
    char *p = malloc(8);
    // WARN_CHECK: mem-out-of-bounds outside the allocated object
    p[8] = 1;
    free(p);
}
