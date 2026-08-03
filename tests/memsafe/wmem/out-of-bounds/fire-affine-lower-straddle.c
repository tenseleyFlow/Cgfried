// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

void fire_affine_lower_straddle(void)
{
    char *p = malloc(8);
    // WARN_CHECK: mem-out-of-bounds outside the allocated object
    *(int *)(p - 1) = 0;
    free(p);
}
