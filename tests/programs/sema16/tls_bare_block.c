// FLAGS: -fsyntax-only
// ERROR_EXPECTED: requires 'static' or 'extern'
void f(void){ _Thread_local int x; }
