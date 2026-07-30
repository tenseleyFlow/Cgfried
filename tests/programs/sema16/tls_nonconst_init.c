// FLAGS: -fsyntax-only
// ERROR_EXPECTED: not a constant expression
int g; _Thread_local int x = g;
