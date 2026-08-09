// FLAGS: -fsyntax-only -std=gnu17 -Werror=incompatible-pointer-types
// ERROR_EXPECTED: passing argument 1 of 'takes_long'
// The parameter type check is the ordinary assignment check against `&var`,
// which is why an ARRAY variable reports the real type of its address rather
// than anything cleanup-specific. Promoted here with -Werror= because an
// incompatible pointer is a warning in this compiler (the Sprint 13 severity
// table), so a plain compile would succeed and prove nothing.
void takes_long(long *p);

void g(void)
{
    int a __attribute__((cleanup(takes_long))) = 1;

    (void)a;
}
