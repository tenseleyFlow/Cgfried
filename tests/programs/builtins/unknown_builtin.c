// No table row = an honest error. There is deliberately no
// accept-anything-__builtin_ fallback: a silently accepted builtin that
// lowers to nothing is worse than a clean rejection.
// ERROR_EXPECTED: is not a builtin this compiler implements
int f(void)
{
    return __builtin_clz(8u);
}
