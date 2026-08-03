// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fdopen(int, const char *);

int shortcircuit_leak_fire(int precondition, int fd)
{
    FILE *stream = 0;

    if (precondition || !(stream = fdopen(fd, "r")))
        return 1;
    // WARN_CHECK: mem-leak opened resource is not closed before this return
    return 0;
}
