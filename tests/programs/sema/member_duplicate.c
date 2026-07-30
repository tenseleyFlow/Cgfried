// FLAGS: -fsyntax-only
// ERROR_EXPECTED: duplicate member 'm'
struct C { int m; char m; };
