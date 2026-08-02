// FLAGS: -fsyntax-only -Wtype-limits
// WARN_COUNT: 1

int type_limits_unsigned_char(unsigned char value)
{
    // WARN_CHECK: type-limits comparison is always false due to limited range of data type
    return value > 300;
}
