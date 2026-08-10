// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: every extra output names one fixed x86 register and has
// exactly one matching input
/* A fixed register alone is insufficient: without a tied input nothing keeps
 * that physical register reserved at the atomic asm point. */
void f(void)
{
    int a;
    int b;

    __asm__("" : "=a"(a), "=c"(b));
}
