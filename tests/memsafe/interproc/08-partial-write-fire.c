// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

static void write_first(char *p)
{
    p[0] = 1;
}

int partial_write_fire(void)
{
    char *p = malloc(2);
    write_first(p);
    // WARN_CHECK: mem-uninit-read read of uninitialized heap memory
    int value = p[1];
    free(p);
    return value;
}
