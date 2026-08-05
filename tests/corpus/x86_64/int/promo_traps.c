// OPT_EQ: all
// Integer promotions: the sign surprises.
//
// `signed char`, not plain `char`: plain char's signedness is the TARGET's
// choice (signed on x86_64-linux-gnu, unsigned on arm64-linux), so spelling
// it here would make this fixture's answer target-dependent for a reason
// that has nothing to do with promotion -- real aarch64 gcc exits 3 on the
// plain-char form. tests/corpus/char_sign/ owns that divergence on purpose.
// EXIT_CODE: 0
int main(void)
{
    signed char c = -1;
    unsigned short us = 65535;
    if (!((c < 0u) == 0))
        return 1; /* c promotes to int -1; -1 < 0u is
                     UNSIGNED: false */
    if ((us + 1) != 65536)
        return 2; /* unsigned short promotes SIGNED */
    if ((c >> 1) != -1)
        return 3; /* promoted arithmetic shift */
    return 0;
}
