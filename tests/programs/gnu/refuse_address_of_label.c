// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: address-of-label operator '&&' is not supported
// The other half of refuse_computed_goto.c. `&&` lexes as ONE token, so
// this reaches the primary-expression parser rather than the unary path --
// without its own case it reported a bare "expected an expression".
void f(void)
{
    void *p = &&lab;

    (void)p;
lab:;
}
