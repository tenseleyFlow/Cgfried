// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: cleanup argument not an identifier
// The argument position takes an IDENTIFIER, not an expression -- a grammar
// rule, and gcc has a dedicated message for breaking it. It is a different
// failure from naming something that is not a function (see
// attr_cleanup_not_function.c): here the parser never gets a name at all.
void f(int *p);

void g(void)
{
    int a __attribute__((cleanup(&f))) = 1;

    (void)a;
}
