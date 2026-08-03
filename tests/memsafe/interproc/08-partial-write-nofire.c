// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

static void write_first(char *p)
{
    p[0] = 1;
}

int partial_write_nofire(void)
{
    char *p = malloc(2);
    write_first(p);
    int value = p[0];
    free(p);
    return value;
}
