// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
void *memcpy(void *, const void *, unsigned long);
int nofire_memcpy(void)
{
    char src[4] = {1, 2, 3, 4};
    char *p = malloc(4);
    memcpy(p, src, 4);
    int v = p[3];
    free(p);
    return v;
}
