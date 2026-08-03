// CATEGORY: free-nonheap
void *malloc(unsigned long);
void free(void *);
void f(void) {
    void *p = malloc(8);
    free(p);
}
