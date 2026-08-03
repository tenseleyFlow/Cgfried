// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

void fire_affine_loop(void)
{
    char *p = malloc(8);
    for (int i = 8; i < 12; i++) {
        // WARN_CHECK: mem-out-of-bounds outside the allocated object
        p[i] = 0;
    }
    free(p);
}
