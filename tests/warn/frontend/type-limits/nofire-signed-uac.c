// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 0

int signed_unsigned_uac(int value)
{
    return value < 18446744073709551615ULL;
}
