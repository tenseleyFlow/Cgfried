// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: allocated here
void *malloc(unsigned long);
void free(void *);

int fire_first_byte(void)
{
    char *p = malloc(8);
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    int value = p[0];
    free(p);
    return value;
}
