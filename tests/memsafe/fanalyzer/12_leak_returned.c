// CATEGORY: leak
void *malloc(unsigned long);
void *f(void) {
    return malloc(8);
}
