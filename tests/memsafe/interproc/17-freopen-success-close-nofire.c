// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);
FILE *freopen(const char *, const char *, FILE *);
int fclose(FILE *);

void freopen_success_close_nofire(void)
{
    FILE *old = fopen("input", "r");
    FILE *replacement;

    if (!old)
        return;
    replacement = freopen("other", "r", old);
    if (replacement)
        fclose(replacement);
}
