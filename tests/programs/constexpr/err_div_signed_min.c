// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
enum E { A = (-9223372036854775807LL - 1) / -1 };
