// FLAGS: --dump-ast
// ERROR_EXPECTED: only allowed in a function definition
// An identifier list is legal only in a DEFINITION (6.7.6.3p3). Here `a`
// and `b` are not typedef names, so this is an identifier list in a
// declaration — the diagnostic must say so.
int f(a, b);
