// FLAGS: -fsyntax-only
// ERROR_EXPECTED: unknown type name 'my_t'
int f(void) {
    my_t a;
    my_t b = 1;
    my_t *c = &a;
    return 0;
}
