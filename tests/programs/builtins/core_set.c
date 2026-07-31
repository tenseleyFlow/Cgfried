// The whole Sprint 28 builtin set through the real pipeline, with the
// values gcc produces. The mem/str family lowers to LIBC CALLS in
// v0.1.0 (inline expansion is Phase 7/11), which is what the ASM_CHECK
// pins — pre-optimizing here would make -O0 output untrue to the source.
// EXIT_CODE: 0
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}memset
// ASM_CHECK(x86_64-linux-gnu): call{{[ \t]+}}strlen
int main(void)
{
    char buf[16];
    char cmp[16];
    double inf = __builtin_inf();
    float inff = __builtin_inff();
    double nan = __builtin_nan("");
    long *dyn = (long *)__builtin_alloca(8 * 4);
    int i;

    __builtin_memset(buf, 'x', 5);
    buf[5] = 0;
    __builtin_memcpy(cmp, buf, 6);
    __builtin_memmove(cmp, buf, 6);
    for (i = 0; i < 4; i++)
        dyn[i] = i * 11;

    if (__builtin_strlen(buf) != 5)
        return 1;
    if (__builtin_memcmp(buf, cmp, 6) != 0)
        return 2;
    if (dyn[3] != 33)
        return 3;
    /* Infinities and NaN come from the softfloat bit patterns, never a
     * host double: inf compares greater than the largest finite value,
     * and a NaN is the only value unequal to itself. */
    if (!(inf > 1e308))
        return 4;
    if (!(inff > 3.0e38f))
        return 5;
    if (!(nan != nan))
        return 6;
    if (__builtin_huge_val() != inf)
        return 7;
    /* __builtin_expect is an honest no-op: the VALUE is argument 0. */
    if (__builtin_expect(i == 4, 1) != 1)
        return 8;
    /* constant_p answers from the constant engine at this point in
     * compilation — 1 for a literal fold, 0 when it cannot know. */
    if (__builtin_constant_p(6 * 7) != 1)
        return 9;
    if (__builtin_constant_p(i) != 0)
        return 10;
    return 0;
}
