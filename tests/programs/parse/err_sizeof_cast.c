// FLAGS: --dump-ast
// ERROR_EXPECTED: expected ';'
typedef int T; int x; int f(void){ return sizeof (T)(x); }
