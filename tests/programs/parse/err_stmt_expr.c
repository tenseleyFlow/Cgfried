// FLAGS: --dump-ast
// ERROR_EXPECTED: Sprint 55
int f(void){ return ({ 1; }); }
