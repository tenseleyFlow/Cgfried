// CATEGORY: double-free
void *malloc(unsigned long);
void free(void *);
void f(void) {
    void *p = malloc(8);
    void *q = p;
    free(p);
    free(q);
}
