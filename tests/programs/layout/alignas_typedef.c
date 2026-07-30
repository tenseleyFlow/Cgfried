// FLAGS: -fsyntax-only
// ERROR_EXPECTED: cannot appear on a typedef
_Alignas(16) typedef int T;
