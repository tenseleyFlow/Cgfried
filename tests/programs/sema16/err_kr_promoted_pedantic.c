// FLAGS: -fsyntax-only -pedantic-errors
// ERROR_EXPECTED: promoted argument 'x' doesn't match prototype
// A K&R definition retains GCC's warning-by-default policy, while pedantic
// errors promote the same incompatible default-promotion diagnostic.
void f(float);
void f(x) float x; { (void)x; }
