// FLAGS: -std=gnu17
// ERROR_EXPECTED: unsupported register name 'not_a_register' on local register
// variable 'x' for this target
int f(void)
{
    register long x __asm__("not_a_register") = 1;

    __asm__ volatile("" : : "r"(x));
    return (int)x;
}
