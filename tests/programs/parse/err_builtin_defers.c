// FLAGS: --dump-ast
// ERROR_EXPECTED: Sprint 28
// `__builtin_va_list` and the va_* function builtins went live in Sprint
// 19; every OTHER __builtin_ name still defers loudly, naming the sprint
// that makes the builtin family real.
int f(void) { return __builtin_expect(1, 1); }
