// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
int asprintf(char **, const char *, ...);
void free(void *);

void nofire_asprintf_failure(void)
{
    char *p;
    if (asprintf(&p, "%s", "text") < 0)
        return;
    free(p);
}
