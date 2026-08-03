// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);

int nofire_null_compare(void)
{
    void *p = malloc(8);
    free(p);
    return p == 0;
}
