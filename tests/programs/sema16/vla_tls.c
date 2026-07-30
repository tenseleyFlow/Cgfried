// FLAGS: -fsyntax-only
// ERROR_EXPECTED: thread storage
void f(int n){ static _Thread_local int a[n]; (void)a; }
