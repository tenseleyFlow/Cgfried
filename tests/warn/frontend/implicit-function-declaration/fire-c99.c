// FLAGS: -fsyntax-only -std=c99
// WARN_COUNT: 1
int implicit_function_c99(void)
{
    // WARN_CHECK: implicit-function-declaration implicit declaration of function 'missing_function'
    return missing_function();
}
