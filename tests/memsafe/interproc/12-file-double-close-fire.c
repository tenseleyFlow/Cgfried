// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
// mem-trace: resource opened here
// mem-trace: pointer is non-null on this branch
// mem-trace: freed here
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);
int fclose(FILE *);

void file_double_close_fire(void)
{
    FILE *f = fopen("input", "r");
    if (f) {
        fclose(f);
        // WARN_CHECK: mem-double-free resource is closed more than once
        fclose(f);
    }
}
