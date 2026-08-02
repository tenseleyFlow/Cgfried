// FLAGS: -fsyntax-only -Wsign-conversion
// WARN_COUNT: 1

int sign_conversion_equal_width(void)
{
    // WARN_CHECK: sign-conversion conversion to 'int' from 'unsigned int' changes the sign
    return 0xffffffffu;
}
