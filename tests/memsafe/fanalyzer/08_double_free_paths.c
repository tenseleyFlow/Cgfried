// CATEGORY: double-free
void *malloc(unsigned long);
void free(void *);
void f(int first) {
    void *p = malloc(8);
    if (first)
        free(p);
    else
        free(p);
}
