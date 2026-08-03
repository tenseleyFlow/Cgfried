// CATEGORY: use-after-free
void *malloc(unsigned long);
void free(void *);
int f(void) {
    int *p = malloc(sizeof(*p));
    free(p);
    return *p;
}
