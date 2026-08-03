// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
char *strdup(const char *);
void free(void *);
int nofire_strdup(void)
{
    char *p = strdup("ok");
    int v = p[0];
    free(p);
    return v;
}
