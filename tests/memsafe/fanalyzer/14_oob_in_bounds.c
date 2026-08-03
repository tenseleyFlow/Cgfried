// CATEGORY: out-of-bounds
void *malloc(unsigned long);
void free(void *);
void f(void) {
    int *p = malloc(2 * sizeof(*p));
    p[1] = 1;
    free(p);
}
