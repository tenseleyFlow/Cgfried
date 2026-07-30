// FLAGS: -fsyntax-only
// WARNING_EXPECTED: 'g' is static but used in inline function 'f' which is not static
static int g = 5;
inline int f(void) { return g; }
