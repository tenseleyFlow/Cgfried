// FLAGS: -fsyntax-only
// WARNING_EXPECTED: function declared 'noreturn' has a 'return' statement
_Noreturn void f(void) { return; }
