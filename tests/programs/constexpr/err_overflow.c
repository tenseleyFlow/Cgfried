// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
enum E { A = 2147483647 + 1 };
