// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
enum { NEG_INT_MIN = -(-2147483647 - 1) };
