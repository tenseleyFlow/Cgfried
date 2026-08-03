// CATEGORY: out-of-bounds
void *malloc(unsigned long);
void free(void *);
int f(void) {
    int *p = malloc(2 * sizeof(*p));
    int *end = p + 2;
    int equal = end == p + 2;
    free(p);
    return equal;
}
