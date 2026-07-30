// FLAGS: --dump-ast
// ERROR_EXPECTED: at most one
int a; int f(void){ return _Generic(a, default:1, default:2); }
