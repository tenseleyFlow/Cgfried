// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
typedef unsigned long size_t;
void *malloc(size_t);
void free(void *);
int snprintf(char *, size_t, const char *, ...);
int nofire_snprintf(void)
{
    char *p = malloc(8);
    (void)snprintf(p, 8, "%s", "ok");
    int v = p[0];
    free(p);
    return v;
}
