// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

void fire_size_expression(void)
{
    int *p = malloc(2 * sizeof(*p));
    // WARN_CHECK: mem-out-of-bounds outside the allocated object
    p[2] = 1;
    free(p);
}
