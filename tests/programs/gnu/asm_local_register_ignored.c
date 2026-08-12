// FLAGS: -std=gnu17 -fsyntax-only
// WARNING_EXPECTED: ignoring asm specifier for non-register local variable 'x'
int f(void)
{
    long x __asm__("r10") = 1;

    return (int)x;
}
