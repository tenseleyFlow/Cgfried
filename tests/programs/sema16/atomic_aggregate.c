// FLAGS: -fsyntax-only
// ERROR_EXPECTED: outside v0.1.0 scope
struct S { int x; }; _Atomic struct S s;
