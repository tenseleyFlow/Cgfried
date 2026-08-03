// CATEGORY: leak
void *malloc(unsigned long);
void f(void) {
    (void)malloc(8);
}
