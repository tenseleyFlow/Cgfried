// CATEGORY: out-of-bounds
void *malloc(unsigned long);
void free(void *);
int f(void) {
    int *p = malloc(sizeof(*p));
    p[0] = 1;
    int value = p[-1];
    free(p);
    return value;
}
