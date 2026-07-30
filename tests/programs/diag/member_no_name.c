// FLAGS: -fsyntax-only
// ERROR_EXPECTED: expected a member name after '->'
struct S { int m; } *sp;
void f(void){ sp->; }
