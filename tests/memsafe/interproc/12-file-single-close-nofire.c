// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fdopen(int, const char *);
int fclose(FILE *);

void file_single_close_nofire(int fd)
{
    FILE *f = fdopen(fd, "r");
    if (f)
        fclose(f);
}
