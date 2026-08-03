// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void *memset(void *, int, unsigned long);
int nofire_memset(void)
{
    char *p = malloc(8);
    memset(p, 0, 8);
    int v = p[7];
    free(p);
    return v;
}
