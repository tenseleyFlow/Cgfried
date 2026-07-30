// FLAGS: --dump-ast
// ERROR_EXPECTED: Sprint 55
int f(int a){ return a ?: 1; }
