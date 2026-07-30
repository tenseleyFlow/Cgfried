// FLAGS: -fsyntax-only
// WARNING_EXPECTED: promoted argument 'x' doesn't match prototype
// THE 6.7.6.3p15 trap: a K&R definition is compatible with a prototype
// only on PROMOTED parameter types, and K&R float promotes to double —
// so `void f(float);` and `void f(x) float x;` describe two different
// functions calling-convention-wise. Same for char and short.
void f(float);
void f(x) float x; { (void)x; }
