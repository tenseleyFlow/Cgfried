// FLAGS: -fsyntax-only
// ERROR_EXPECTED: redefinition of 'struct R'
struct R { int x; };
struct R { int y; };
