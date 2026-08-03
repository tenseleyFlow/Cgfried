// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *calloc(unsigned long, unsigned long);
void free(void *);
int nofire_calloc(void)
{
    int *p = calloc(1, sizeof(int));
    int v = *p;
    free(p);
    return v;
}
