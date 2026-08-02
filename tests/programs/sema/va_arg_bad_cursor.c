// ERROR_EXPECTED: first argument to '__builtin_va_arg' is not a va_list
int f(int n, ...)
{
    __builtin_va_list ap;
    return __builtin_va_arg(sizeof ap, int);
}
