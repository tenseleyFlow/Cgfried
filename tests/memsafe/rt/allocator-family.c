void *malloc(unsigned long);
void *calloc(unsigned long, unsigned long);
void *realloc(void *, unsigned long);
void *reallocarray(void *, unsigned long, unsigned long);
char *strdup(const char *);
char *strndup(const char *, unsigned long);
void *aligned_alloc(unsigned long, unsigned long);
int posix_memalign(void **, unsigned long, unsigned long);
void free(void *);

int main(void)
{
    char *a = malloc(8);
    char *b = calloc(4, 4);
    char *c = strdup("safe");
    char *d = strndup("runtime", 3);
    char *e = aligned_alloc(64, 64);
    char *f = (void *)0;

    if (!a || !b || !c || !d || !e)
        return 2;
    a = realloc(a, 16);
    b = reallocarray(b, 8, 4);
    if (!a || !b || posix_memalign((void **)&f, 128, 33) != 0)
        return 3;
    if (((unsigned long)e & 63UL) || ((unsigned long)f & 127UL))
        return 4;
    if (c[0] != 's' || d[2] != 'n' || d[3] != '\0')
        return 5;
    free(a);
    free(b);
    free(c);
    free(d);
    free(e);
    free(f);
    free((void *)0);
    return 0;
}
