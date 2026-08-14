// FLAGS: --target=arm64-linux -fsyntax-only -std=gnu17
// ERROR_EXPECTED: register name 'x12' on local register variable 'x' is
// reserved by the target backend
int f(void)
{
    register long x __asm__("x12") = 1;

    __asm__ volatile("" : : "r"(x));
    return (int)x;
}
