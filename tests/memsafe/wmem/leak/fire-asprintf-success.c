// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
int asprintf(char **, const char *, ...);

void fire_asprintf_success(void)
{
    char *p;
    if (asprintf(&p, "%s", "text") >= 0) {
        // WARN_CHECK: mem-leak allocated memory
        return;
    }
}
