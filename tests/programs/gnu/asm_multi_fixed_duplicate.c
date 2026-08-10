// FLAGS: -fsyntax-only -std=gnu17
// ERROR_EXPECTED: both require the same fixed register
/* Distinct outputs cannot both occupy rax even if each is tied to an input. */
void f(int x, int y)
{
    int a;
    int b;

    __asm__("" : "=a"(a), "=a"(b) : "0"(x), "1"(y));
}
