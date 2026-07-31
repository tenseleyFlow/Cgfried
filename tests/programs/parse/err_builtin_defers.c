// FLAGS: --dump-ast
// ERROR_EXPECTED: is not a builtin this compiler implements
// Sprint 28 made the table in src/builtins.def the whole contract: a
// name WITH a row parses and types, a name without one still defers
// loudly. There is deliberately no accept-anything-__builtin_ fallback —
// a silently accepted builtin that lowers to nothing is worse than a
// clean rejection.
int f(void) { return __builtin_clz(8u); }
