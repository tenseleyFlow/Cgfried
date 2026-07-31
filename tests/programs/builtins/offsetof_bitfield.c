// A bit-field has no address, so offsetof of one is rejected (gcc says
// the same) rather than silently rounded down to its container.
// ERROR_EXPECTED: applied to a bit-field member
struct S {
    int a : 3;
    int b : 5;
};
unsigned long f(void)
{
    return __builtin_offsetof(struct S, b);
}
