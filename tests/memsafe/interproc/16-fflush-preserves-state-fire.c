// FLAGS: -fsyntax-only
// WARN_COUNT: 1
// EXIT_CODE: 0
typedef struct FILE FILE;
FILE *fopen(const char *, const char *);
int fflush(FILE *);
int fclose(FILE *);

void fflush_preserves_state_fire(void)
{
    FILE *stream = fopen("output", "w");

    if (!stream)
        return;
    fflush(stream);
    fclose(stream);
    // WARN_CHECK: mem-double-free resource is closed more than once
    fclose(stream);
}
