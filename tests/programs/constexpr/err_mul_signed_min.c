// FLAGS: -fsyntax-only
// ERROR_EXPECTED: overflow in constant expression
enum E { A = -1LL * (-9223372036854775807LL - 1) };
