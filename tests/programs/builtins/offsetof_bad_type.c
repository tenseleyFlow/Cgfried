// offsetof's first operand must be a struct or union.
// ERROR_EXPECTED: requires a struct or union type
unsigned long f(void)
{
    return __builtin_offsetof(int, x);
}
