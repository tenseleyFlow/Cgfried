// CATEGORY: free-nonheap
void free(void *);
void f(void) {
    free((void *)"not heap");
}
