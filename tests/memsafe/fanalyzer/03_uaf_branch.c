// CATEGORY: use-after-free
void *malloc(unsigned long);
void free(void *);
int f(int release) {
    int *p = malloc(sizeof(*p));
    *p = 3;
    if (release)
        free(p);
    if (release)
        return *p;
    free(p);
    return 0;
}
