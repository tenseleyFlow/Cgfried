// FLAGS: -fsyntax-only -Wsign-conversion
// WARN_COUNT: 0
unsigned int sign_conversion_explicit(int value) { return (unsigned int)value; }
