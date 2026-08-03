// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fdopen(int, const char *);
int fclose(FILE *);

int shortcircuit_null_nofire(int precondition, int fd)
{
    FILE *stream = 0;

    if (precondition || !(stream = fdopen(fd, "r")))
        return 1;
    fclose(stream);
    return 0;
}
