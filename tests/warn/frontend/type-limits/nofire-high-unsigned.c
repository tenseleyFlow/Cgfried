// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 0

int high_unsigned_limit(unsigned value)
{
    return value >= 0xbff00000u;
}
