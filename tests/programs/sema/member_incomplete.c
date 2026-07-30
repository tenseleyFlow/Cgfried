// FLAGS: -fsyntax-only
// ERROR_EXPECTED: has incomplete type
struct I;
struct H { struct I m; };
