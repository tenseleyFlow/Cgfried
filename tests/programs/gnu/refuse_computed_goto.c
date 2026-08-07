// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: computed goto is not supported
// A REFUSED row of docs/gnu-extensions.md, and one of two diagnostics that
// have to exist: `goto *p` is the jump, `&&label` is the address, and a
// program can contain either without the other.
void f(void)
{
    void *p = (void *)0;

    goto *p;
}
