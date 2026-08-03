// CATEGORY: out-of-bounds
void *malloc(unsigned long);
void free(void *);
void f(void) {
    int *p = malloc(2 * sizeof(*p));
    p[2] = 1;
    free(p);
}
