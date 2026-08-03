// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
void *malloc(unsigned long);
void free(void *);
int nofire_written(void)
{
    int *p = malloc(sizeof(int));
    *p = 7;
    int v = *p;
    free(p);
    return v;
}
