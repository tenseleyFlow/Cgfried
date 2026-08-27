// FLAGS: -fsyntax-only
// ERROR_EXPECTED: promoted argument 'd' doesn't match prototype
// A prototype-first K&R definition may retain the traditional warning when
// only default promotion changes an otherwise matching declaration-list
// type. Unrelated parameter types are a constraint violation and must stop
// before the composite prototype ABI reaches lowering.
void f(int, double);
void f(i, d) int i; double *d; { (void)i; }
