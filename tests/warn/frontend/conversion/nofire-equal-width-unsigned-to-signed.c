// FLAGS: -fsyntax-only -Woverflow
// WARN_COUNT: 0

int equal_width_unsigned_to_signed(void)
{
    int value = 0xffffffffu;
    return value;
}
