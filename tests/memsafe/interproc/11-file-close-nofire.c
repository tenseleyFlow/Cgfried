// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);
int fclose(FILE *);

void file_close_nofire(void)
{
    FILE *f = fopen("input", "r");
    if (f)
        fclose(f);
}
