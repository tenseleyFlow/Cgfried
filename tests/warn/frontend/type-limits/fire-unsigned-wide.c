// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 1

int unsigned_wide_limit(unsigned value)
{
    // WARN_CHECK: type-limits comparison is always true due to limited range of data type
    return value < 18446744073709551615ULL;
}
