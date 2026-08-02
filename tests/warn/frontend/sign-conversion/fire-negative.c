// FLAGS: -fsyntax-only -Wsign-conversion
// WARN_COUNT: 1
unsigned int sign_conversion_negative(void)
{
    // WARN_CHECK: sign-conversion conversion to 'unsigned int' from 'int' changes the sign
    return -1;
}
