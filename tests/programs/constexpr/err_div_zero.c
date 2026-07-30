// FLAGS: -fsyntax-only
// ERROR_EXPECTED: division by zero
enum E { A = 1/0 };
