// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void fire_negative(void)
{
    char *p = malloc(4);
    // WARN_CHECK: mem-out-of-bounds outside the allocated object
    p[-1] = 0;
    free(p);
}
