// A designator naming no member is an error at ITS location, not an ICE
// at lowering (the fold runs in sema for exactly this reason).
// ERROR_EXPECTED: 'struct S' has no member named 'nope'
struct S {
    int a;
};
unsigned long f(void)
{
    return __builtin_offsetof(struct S, nope);
}
