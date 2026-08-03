// FLAGS: -fsyntax-only -Wno-mem-leak
// WARN_COUNT: 1
// EXIT_CODE: 0
typedef struct FILE FILE;
void *malloc(unsigned long);
void free(void *);
FILE *fopen(const char *, const char *);
int fclose(FILE *);

void file_input_borrow_fire(void)
{
    char *path = malloc(8);

    free(path);
    // WARN_CHECK: mem-use-after-free use of memory after it was freed
    FILE *stream = fopen(path, "r");
    if (stream)
        fclose(stream);
}
