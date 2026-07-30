// FLAGS: -fsyntax-only
// ERROR_EXPECTED: shift count 40 is out of range
enum E { A = 1 << 40 };
