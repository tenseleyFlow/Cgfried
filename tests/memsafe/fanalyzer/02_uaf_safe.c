// CATEGORY: use-after-free
void *malloc(unsigned long);
void free(void *);
int f(void) {
    int *p = malloc(sizeof(*p));
    *p = 7;
    int value = *p;
    free(p);
    return value;
}
