// CATEGORY: free-nonheap
void free(void *);
void f(void) {
    int local = 0;
    free(&local);
}
