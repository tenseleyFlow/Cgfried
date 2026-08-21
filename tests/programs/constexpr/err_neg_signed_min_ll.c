// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
enum { NEG_LLONG_MIN = -(-9223372036854775807LL - 1) };
