// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 0

int negative_unsigned_uac(unsigned value)
{
    return value < -1;
}
