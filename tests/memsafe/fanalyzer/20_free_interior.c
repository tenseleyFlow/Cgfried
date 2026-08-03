// CATEGORY: free-nonheap
void *malloc(unsigned long);
void free(void *);
void f(void) {
    char *p = malloc(8);
    free(p + 1);
}
