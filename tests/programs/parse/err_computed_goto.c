// FLAGS: --dump-ast
// ERROR_EXPECTED: Sprint 55
void f(void *p){ goto *p; }
