// CATEGORY: double-free
void *malloc(unsigned long);
void free(void *);
void f(void) {
    void *p = malloc(8);
    free(p);
    free(p);
}
