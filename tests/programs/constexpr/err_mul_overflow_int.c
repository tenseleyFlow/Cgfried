// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
enum E { A = 50000 * 50000 };
