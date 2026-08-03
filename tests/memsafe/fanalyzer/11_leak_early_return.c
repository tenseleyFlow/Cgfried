// CATEGORY: leak
void *malloc(unsigned long);
void free(void *);
void f(int fail) {
    void *p = malloc(8);
    if (fail)
        return;
    free(p);
}
