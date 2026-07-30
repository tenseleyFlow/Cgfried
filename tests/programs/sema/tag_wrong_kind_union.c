// FLAGS: -fsyntax-only
// ERROR_EXPECTED: defined as wrong kind of tag
struct U { int a; };
union U { int b; };
