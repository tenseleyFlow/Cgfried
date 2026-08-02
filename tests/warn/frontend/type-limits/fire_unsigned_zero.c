// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 1
int unsigned_is_nonnegative(unsigned value)
{
    // WARN_CHECK: type-limits comparison is always true due to limited range of data type
    return value >= 0;
}
