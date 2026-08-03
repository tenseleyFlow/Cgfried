// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void *realloc(void *, unsigned long);
void free(void *);
void nofire_nonzero(void)
{
    void *p = malloc(8);
    void *q = realloc(p, 16);
    if (q)
        free(q);
    else
        free(p);
}
