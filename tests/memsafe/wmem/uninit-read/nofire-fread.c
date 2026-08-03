// FLAGS: -fsyntax-only -Werror=mem
// WARN_COUNT: 0
// EXIT_CODE: 0
typedef unsigned long size_t;
typedef struct file FILE;
void *malloc(size_t);
void free(void *);
size_t fread(void *, size_t, size_t, FILE *);
int nofire_fread(FILE *stream)
{
    char *p = malloc(4);
    (void)fread(p, 1, 4, stream);
    int v = p[3];
    free(p);
    return v;
}
