// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
int fclose(FILE *);

void freopen_failure_consumes_fire(void)
{
    FILE *old = fopen("input", "r");
    FILE *replacement;

    if (!old)
        return;
    replacement = freopen("other", "r", old);
    if (!replacement) {
        // WARN_CHECK: mem-double-free resource is closed more than once
        fclose(old);
    } else {
        fclose(replacement);
    }
}
