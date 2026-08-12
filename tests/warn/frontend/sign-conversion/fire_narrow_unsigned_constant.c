// FLAGS: -fsyntax-only -Wsign-conversion
// WARN_COUNT: 2

// WARN_CHECK: sign-conversion conversion to 'int' from 'unsigned long' changes the sign
int sign_conversion_narrow_unsigned = 2147483648UL;
// WARN_CHECK: sign-conversion conversion to 'signed char' from 'unsigned int' changes the sign
signed char sign_conversion_narrow_byte = 255U;
