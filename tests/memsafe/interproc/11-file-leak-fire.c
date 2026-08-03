// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: resource opened here
// mem-trace: function returns on this path without releasing it
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);

void file_leak_fire(void)
{
    FILE *f = fopen("input", "r");
    (void)f;
    // WARN_CHECK: mem-leak opened resource is not closed before this return
    return;
}
