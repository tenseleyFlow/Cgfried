// FLAGS: -fsyntax-only -std=c99
// WARN_COUNT: 1
int implicit_function_once(void)
{
    // WARN_CHECK: implicit-function-declaration implicit declaration of function 'only_once'
    int first = only_once();
    return first + only_once();
}
