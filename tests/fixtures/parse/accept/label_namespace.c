int f(void) {
    int foo = 0;
    if (foo) goto foo;
foo:
    foo = 1;
    return foo;
}
